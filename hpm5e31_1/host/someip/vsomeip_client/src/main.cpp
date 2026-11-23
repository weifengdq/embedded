/*
 * vsomeip_client —— 上位机 SOME/IP 客户端（vsomeip 3.7.6）
 *
 * 对照价值：板端 components/app_someip 是"手写最小实现"（~1500 行 C），
 * 本程序用工业级中间件 vsomeip 做同一件事（SD 发现 + RPC + 事件订阅），
 * 两边能互通，说明板端实现的线上格式是规范的（第 35 章"对比"一节）。
 *
 * 流程：
 *   1. 读 VSOMEIP_CONFIGURATION 配置（unicast 192.168.2.1、SD 组播 224.244.224.245:30490）
 *   2. 等 service 0x1234 / instance 0x0001 变 available（由板端 OfferService 触发）
 *   3. 依次调用 method 0x0001 add / 0x0002 get_version / 0x0003 set_led /
 *      0x0004/0x0005 temperature getter/setter / 0x0006 get_time
 *   4. subscribe eventgroup 0x0001，统计 6 s 内事件条数/周期
 *   5. 打印 PASS/FAIL 汇总
 *
 * 构建/运行见 README.md 与 run.sh
 */
#include <vsomeip/vsomeip.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr vsomeip::service_t SVC = 0x1234;
constexpr vsomeip::instance_t INST = 0x0001;
constexpr vsomeip::eventgroup_t EG = 0x0001;
constexpr vsomeip::major_version_t MAJOR = 0x01;

constexpr vsomeip::method_t M_ADD = 0x0001;
constexpr vsomeip::method_t M_GET_VERSION = 0x0002;
constexpr vsomeip::method_t M_SET_LED = 0x0003;
constexpr vsomeip::method_t M_TEMP_GET = 0x0004;
constexpr vsomeip::method_t M_TEMP_SET = 0x0005;
constexpr vsomeip::method_t M_GET_TIME = 0x0006;

constexpr vsomeip::event_t E_COUNTER = 0x8001;
constexpr vsomeip::event_t E_ADC = 0x8002;
constexpr vsomeip::event_t E_TEMP = 0x8004;

std::shared_ptr<vsomeip::runtime> g_rt;
std::shared_ptr<vsomeip::application> g_app;

std::atomic<bool> g_available{false};
std::atomic<uint32_t> g_ev_counter{0};
std::atomic<uint32_t> g_ev_adc{0};
std::atomic<uint32_t> g_ev_temp{0};

std::mutex g_mtx;
std::condition_variable g_cv;
std::shared_ptr<vsomeip::message> g_resp;

int g_pass = 0;
int g_fail = 0;

void check(const char *name, bool ok, const char *detail = "")
{
    std::printf("  %s %-26s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
    }
}

void put32(std::vector<vsomeip::byte_t> &v, uint32_t x)
{
    v.push_back(static_cast<vsomeip::byte_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<vsomeip::byte_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<vsomeip::byte_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<vsomeip::byte_t>(x & 0xFF));
}

uint32_t get32(const std::vector<vsomeip::byte_t> &v, size_t off = 0)
{
    if (v.size() < off + 4) {
        return 0;
    }
    return (static_cast<uint32_t>(v[off]) << 24) | (static_cast<uint32_t>(v[off + 1]) << 16) |
           (static_cast<uint32_t>(v[off + 2]) << 8) | static_cast<uint32_t>(v[off + 3]);
}

float get_f32(const std::vector<vsomeip::byte_t> &v, size_t off = 0)
{
    uint32_t u = get32(v, off);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

void put_f32(std::vector<vsomeip::byte_t> &v, float f)
{
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    put32(v, u);
}

/* vsomeip 是异步的：统一在 ANY_METHOD 回调里区分 RESPONSE/ERROR 与 NOTIFICATION */
void on_message(const std::shared_ptr<vsomeip::message> &msg)
{
    const auto type = msg->get_message_type();
    if ((type == vsomeip::message_type_e::MT_RESPONSE) || (type == vsomeip::message_type_e::MT_ERROR)) {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_resp = msg;
        g_cv.notify_one();
        return;
    }
    if (type == vsomeip::message_type_e::MT_NOTIFICATION) {
        switch (msg->get_method()) {
        case E_COUNTER: g_ev_counter++; break;
        case E_ADC: g_ev_adc++; break;
        case E_TEMP: g_ev_temp++; break;
        default: break;
        }
    }
}

/* 同步 RPC 包装：发 REQUEST，等匹配的 RESPONSE/ERROR（1 s 超时） */
bool rpc(vsomeip::method_t method, const std::vector<vsomeip::byte_t> &req,
         std::vector<vsomeip::byte_t> &out, vsomeip::return_code_e &rc, double &rtt_ms)
{
    auto msg = g_rt->create_request();
    msg->set_service(SVC);
    msg->set_instance(INST);
    msg->set_method(method);
    msg->set_payload(g_rt->create_payload(req));
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_resp.reset();
    }
    const auto t0 = std::chrono::steady_clock::now();
    g_app->send(msg);
    std::unique_lock<std::mutex> lk(g_mtx);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (!g_resp) {
        if (g_cv.wait_until(lk, deadline) == std::cv_status::timeout && !g_resp) {
            return false;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    rtt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    rc = static_cast<vsomeip::return_code_e>(g_resp->get_return_code());
    std::shared_ptr<vsomeip::payload> p = g_resp->get_payload();
    const vsomeip::byte_t *d = p->get_data();
    out.assign(d, d + p->get_length());
    return true;
}

}  // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    std::printf("=== vsomeip 客户端（vsomeip %s）===\n", "3.7.6");
    const char *cfg = std::getenv("VSOMEIP_CONFIGURATION");
    std::printf("config: %s\n", cfg ? cfg : "(默认 /etc/vsomeip/vsomeip.json)");

    g_rt = vsomeip::runtime::get();
    g_app = g_rt->create_application("someip_client");
    if (!g_app->init()) {
        std::printf("FAIL vsomeip init（配置路径/网卡 IP 不对？）\n");
        return 2;
    }
    g_app->register_state_handler([](vsomeip::state_type_e st) {
        std::printf("[vsomeip] state = %s\n", (st == vsomeip::state_type_e::ST_REGISTERED) ? "REGISTERED" : "DEREGISTERED");
    });
    g_app->register_availability_handler(
        SVC, INST,
        [](vsomeip::service_t s, vsomeip::instance_t i, bool available) {
            std::printf("[vsomeip] service 0x%04X.%04X %s\n", s, i, available ? "AVAILABLE" : "GONE");
            g_available = available;
        },
        MAJOR, vsomeip::ANY_MINOR);
    g_app->register_message_handler(SVC, INST, vsomeip::ANY_METHOD, on_message);

    g_app->start();

    /* 1. 等服务可用（板端 OfferService -> vsomeip SD 判定 available） */
    const auto t_wait0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 200 && !g_available; i++) {   /* 最多等 20 s */
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const double wait_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_wait0).count();
    if (!g_available) {
        std::printf("FAIL 20 s 内服务未 available（板端 someip_rpc 在跑吗？SD 组播路由/IGMP 通吗？）\n");
        g_app->stop();
        return 3;
    }
    std::printf("[vsomeip] 服务发现耗时 %.1f ms（从 start 到 available）\n", wait_ms);

    std::vector<vsomeip::byte_t> req;
    std::vector<vsomeip::byte_t> rsp;
    vsomeip::return_code_e rc = vsomeip::return_code_e::E_NOT_OK;
    double rtt = 0.0;

    /* 2. add(2,3) */
    req.clear();
    put32(req, 2);
    put32(req, 3);
    bool ok = rpc(M_ADD, req, rsp, rc, rtt);
    check("add(2,3)", ok && rc == vsomeip::return_code_e::E_OK && get32(rsp) == 5,
          ok ? ("value=" + std::to_string(get32(rsp)) + " rtt=" + std::to_string(rtt).substr(0, 5) + "ms").c_str() : "timeout");

    /* 3. get_version：u32 长度 + 字符 */
    rsp.clear();
    ok = rpc(M_GET_VERSION, {}, rsp, rc, rtt);
    std::string version;
    if (ok && rsp.size() >= 4) {
        const uint32_t n = get32(rsp);
        if (rsp.size() >= 4 + n) {
            version.assign(reinterpret_cast<const char *>(&rsp[4]), n);
        }
    }
    check("get_version", ok && !version.empty(), version.empty() ? "timeout" : ("'" + version + "'").c_str());

    /* 4. set_led(1)/(0) */
    req.assign(1, 0x01);
    rsp.clear();
    ok = rpc(M_SET_LED, req, rsp, rc, rtt);
    const bool led_on = ok && !rsp.empty() && rsp[0] == 1;
    req.assign(1, 0x00);
    rsp.clear();
    ok = rpc(M_SET_LED, req, rsp, rc, rtt);
    const bool led_off = ok && !rsp.empty() && rsp[0] == 0;
    check("set_led(1)/(0)", led_on && led_off, "");

    /* 5. temperature 字段 getter / setter */
    rsp.clear();
    ok = rpc(M_TEMP_GET, {}, rsp, rc, rtt);
    const float t_get = get_f32(rsp);
    req.clear();
    put_f32(req, 42.25f);
    rsp.clear();
    ok = rpc(M_TEMP_SET, req, rsp, rc, rtt);
    const float t_set = get_f32(rsp);
    check("temperature getter/setter", ok && t_get > 15.0f && t_get < 60.0f && t_set > 42.0f && t_set < 42.5f,
          ("get=" + std::to_string(t_get).substr(0, 6) + " set=" + std::to_string(t_set).substr(0, 6)).c_str());

    /* 6. get_time(us) + RTT 统计 */
    std::vector<double> rtts;
    for (int i = 0; i < 50; i++) {
        req.clear();
        put32(req, 1);
        put32(req, 2);
        rsp.clear();
        if (rpc(M_ADD, req, rsp, rc, rtt)) {
            rtts.push_back(rtt);
        }
    }
    if (!rtts.empty()) {
        double sum = 0.0;
        double minv = 1e9;
        double maxv = 0.0;
        for (double v : rtts) {
            sum += v;
            minv = (v < minv) ? v : minv;
            maxv = (v > maxv) ? v : maxv;
        }
        std::printf("[vsomeip] RPC RTT n=%zu min=%.3f avg=%.3f max=%.3f ms\n", rtts.size(), minv, sum / rtts.size(), maxv);
    } else {
        std::printf("[vsomeip] RPC RTT 采样失败\n");
    }

    /* 7. 订阅 eventgroup + 统计事件 */
    g_app->subscribe(SVC, INST, EG, MAJOR);
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(6));
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const uint32_t c = g_ev_counter.load();
    const uint32_t a = g_ev_adc.load();
    const uint32_t t = g_ev_temp.load();
    std::printf("[vsomeip] 事件订阅 %.2f s：counter(0x8001)=%u 条（%.1f ms/条） adc(0x8002)=%u 条（%.1f ms/条） temp(0x8004)=%u 条\n",
                secs, c, c ? (secs * 1000.0 / c) : 0.0, a, a ? (secs * 1000.0 / a) : 0.0, t);
    check("event subscribe+rx", c >= 20 && a >= 200 && t >= 5,
          ("counter=" + std::to_string(c) + " adc=" + std::to_string(a) + " temp=" + std::to_string(t)).c_str());

    g_app->unsubscribe(SVC, INST, EG);
    g_app->stop();

    std::printf("\n=== 汇总: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}