//! uds27check —— UDS 0x27 种子-密钥算法离线校验（Rust 参考实现）
//!
//! 与 C 端 components/app_uds/src/app_uds.c 的 app_uds_calc_key()、Python 端
//! host/uds/common.py 的 security_key() 必须逐位一致。三份实现互为对照，
//! 用来防止"上位机算 key 与 ECU 算 key 不一致"这类最难查的联调问题。
//!
//! 用法：
//!   cargo run --release -- --seed 0xD2E3FB68 --level 1
//!   cargo run --release -- --selftest
//!
//! 无第三方依赖（纯 std），离线可编译。

fn security_key(seed: u32, level: u8) -> u32 {
    let mut k = seed;
    for _ in 0..8 {
        k = k.rotate_left(1);
        k ^= 0x3A5C_9E17;
        if k & 1 != 0 {
            k = k.wrapping_add(0x5A5A_5A5A);
        }
    }
    k ^= (level as u32).wrapping_mul(0x0102_0304);
    k.rotate_left(7)
}

fn selftest() -> i32 {
    // 与 Python uds_sec.py --selftest 完全相同的 8 组向量
    let vectors: [(u32, u8, u32); 8] = [
        (0x0000_0000, 1, 0x024B_1865),
        (0x0000_0001, 1, 0x77D5_9865),
        (0x1234_5678, 1, 0x64BE_9E71),
        (0xFFFF_FFFF, 1, 0x01EF_031D),
        (0xD2E3_FB68, 1, 0x2B58_9DBE),
        (0x0000_0001, 2, 0xF4D7_1E64),
        (0x1234_5678, 2, 0xE7BC_1870),
        (0xFFFF_FFFF, 2, 0x82ED_851C),
    ];
    let mut fail = 0;
    println!("{:<12} {:<4} {:<12} {}", "seed", "lvl", "key", "check");
    for (seed, level, want) in vectors {
        let got = security_key(seed, level);
        let ok = got == want;
        if !ok {
            fail += 1;
        }
        println!("0x{:08X}   {:<4} 0x{:08X}   {}", seed, level, got, if ok { "OK" } else { "MISMATCH" });
    }
    if fail == 0 {
        println!("[uds27check] PASS 8/8 与 Python/C 参考实现一致");
        0
    } else {
        println!("[uds27check] FAIL {} 组不一致", fail);
        1
    }
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--selftest") || args.is_empty() {
        std::process::exit(selftest());
    }
    let mut seed: Option<u32> = None;
    let mut level: u8 = 1;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--seed" => {
                i += 1;
                let s = args.get(i).expect("--seed 需要参数");
                let s = s.trim_start_matches("0x").trim_start_matches("0X");
                seed = Some(u32::from_str_radix(s, 16).expect("seed 需为 16 进制"));
            }
            "--level" => {
                i += 1;
                level = args.get(i).expect("--level 需要参数").parse().expect("level 需为 1 或 2");
            }
            other => {
                eprintln!("未知参数: {}", other);
                std::process::exit(2);
            }
        }
        i += 1;
    }
    let seed = seed.expect("缺少 --seed");
    println!("seed=0x{:08X} level={} -> key=0x{:08X}", seed, level, security_key(seed, level));
}
