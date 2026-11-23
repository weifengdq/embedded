# components/gs_usb —— gs_usb / candleLight 协议层（纯软件）

> 上层：`samples/app/gs_usb_candle/`（CherryUSB vendor class + 4 路 MCAN）。
> 本组件只做**协议事实与编解码**，不依赖 USB 栈、不依赖 HPM SDK，可单独编译/单测。

## 1. 内容

| 文件 | 作用 |
|---|---|
| `inc/gs_usb.h` | 请求号、feature/mode/frame 标志、长度常量、packed 结构体（config/bt_const/bt_const_ext/bittiming/mode/state）、编解码与位时序换算声明 |
| `src/gs_usb_proto.c` | BT_CONST/BT_CONST_EXT/DEVICE_CONFIG 填充、内核位时序→MCAN 字段换算、DLC 换算、RX 帧编码、TX 帧解析、状态映射 |
| `gs_usb.cmake` | sdk_inc + sdk_app_src 接入片段 |

## 2. 关键常量（内核 7.0.0 事实）

```c
GS_USB_VID 0x1209 / GS_USB_PID 0x2323
GS_USB_EP_IN 0x81 / GS_USB_EP_OUT 0x02 / MPS 512(HS)
GS_USB_CHANNELS 4 / GS_USB_ICOUNT 3
GS_USB_RX_CLASSIC_LEN 24   /* 12 头 + 8 数据 + 4 时间戳 */
GS_USB_RX_FD_LEN 80        /* 12 + 64 + 4 */
GS_USB_TX_CLASSIC_LEN 20   /* 12 + 8（无时间戳） */
GS_USB_TX_FD_LEN 76        /* 12 + 64 */
GS_CAN_FEATURE_DEFAULT 13595
```

## 3. API

```c
/* 控制请求应答数据 */
void gs_usb_fill_bt_const(struct gs_device_bt_const *c);
void gs_usb_fill_bt_const_ext(struct gs_device_bt_const_ext *c);
void gs_usb_fill_device_config(struct gs_device_config *c);   /* icount=3 */

/* 内核 gs_device_bittiming → MCAN 1 基位时序字段 */
void gs_usb_bt_to_mcan(const struct gs_device_bittiming *bt, uint32_t *prescaler,
                       uint32_t *num_seg1, uint32_t *num_seg2, uint32_t *num_sjw);

/* 帧编解码 */
uint32_t gs_usb_encode_frame(uint8_t *out, uint32_t echo_id, const struct gs_can_frame *f, uint32_t ts_us);
int      gs_usb_decode_tx(const uint8_t *in, uint32_t len, uint32_t *echo_id,
                          struct gs_can_frame *f, uint8_t *channel, uint8_t *frame_flags);
uint8_t  gs_usb_dlc_to_len(uint8_t dlc);  uint8_t gs_usb_len_to_dlc(uint8_t len);
uint8_t  gs_usb_frame_flags(const struct gs_can_frame *f);

/* 状态映射：MCAN → gs_device_state.state（0=active 1=warning 2=passive 3=bus-off 4=stopped） */
uint32_t gs_usb_map_state(bool bus_off, bool error_passive, bool error_warning, bool started);
```

## 4. 用法（在例程 CMake 里）

```cmake
# find_package(hpm-sdk) 之后
include(${CMAKE_CURRENT_LIST_DIR}/../../../components/gs_usb/gs_usb.cmake)
```

## 5. 协议要点（实现时必须遵守，否则内核会 detach/报错）

1. 控制请求**通道号在 wValue**（BITTIMING/MODE/BERR/DATA_BITTIMING/BT_CONST/BT_CONST_EXT/GET_STATE/IDENTIFY/USER_ID/TERMINATION），
   HOST_FORMAT/DEVICE_CONFIG 用 wValue=1、wIndex=接口号。
2. **TIMESTAMP 请求只回 4 字节 le32**（1 MHz 计数低 32 位）。
3. RX/echo 帧长度：经典 24 B、FD 80 B（带 4 B 时间戳）；TX 帧：经典 20 B、FD 76 B。
4. `channel` 必须 < 4，`can_dlc` 合法（经典 0..8、FD 0..15），`echo_id` < 10（GS_MAX_TX_URBS）。
5. 未知/不支持的请求（含 SET/GET_TERMINATION）一律 STALL，不要返回垃圾数据。

## 6. 测试

协议层本身无硬件依赖，可用例程 `samples/app/gs_usb_candle` 上板联调；
帧编解码与 DLC 换算可用主机侧 `host/gs_usb/` 的同名断言脚本做交叉校验（见该目录 README）。

## 7. 参考来源

- 内核 7.0.0 `drivers/net/can/usb/gs_usb.c`（协议权威，`ref2/gs_usb_ref/`）
- `docs/gs_usb开发约定.md`（团队契约，含 wValue / TIMESTAMP 4B 更正）
- `Roboparty/roboparty_EtherCANFD`、`zhangjiance/candleLight_fw_hpm_port`（行为对照，GPL；本组件不含其代码）
