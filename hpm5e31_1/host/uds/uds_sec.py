#!/usr/bin/env python3
"""uds_sec.py - UDS 0x27 安全访问算法的离线校验（Python 参考实现）

算法（与 components/app_uds/src/app_uds.c 的 app_uds_calc_key() 逐位一致）：

    k = seed
    for i in range(8):
        k = ROL32(k, 1)
        k ^= 0x3A5C9E17
        if k & 1: k += 0x5A5A5A5A
    k ^= level * 0x01020304
    k = ROL32(k, 7)
    return k & 0xFFFFFFFF

说明：这是**示例算法**，不是安全强度算法（真实项目用 AES-CMAC / 客户私有算法 +
可变种子长度）。放在书里的目的是把"种子-密钥"流程讲清楚。

用法：
  python3 uds_sec.py --seed 0xD2E3FB68 --level 1
  python3 uds_sec.py --selftest        # 跑固定向量，C/Python/Rust 三份实现共用
"""
import argparse
import sys

VECTORS = [
    # (seed, level, key) —— 由 C 端与 Rust 端共同验证
    (0x00000000, 1, None),
    (0x00000001, 1, None),
    (0x12345678, 1, None),
    (0xFFFFFFFF, 1, None),
    (0xD2E3FB68, 1, None),
    (0x00000001, 2, None),
    (0x12345678, 2, None),
    (0xFFFFFFFF, 2, None),
]


def security_key(seed, level):
    k = seed & 0xFFFFFFFF
    for _ in range(8):
        k = ((k << 1) | (k >> 31)) & 0xFFFFFFFF
        k ^= 0x3A5C9E17
        k &= 0xFFFFFFFF
        if k & 1:
            k = (k + 0x5A5A5A5A) & 0xFFFFFFFF
    k ^= (level * 0x01020304) & 0xFFFFFFFF
    k = ((k << 7) | (k >> 25)) & 0xFFFFFFFF
    return k


def main():
    ap = argparse.ArgumentParser(description="UDS 0x27 种子-密钥算法（Python 参考实现）")
    ap.add_argument("--seed", default=None, help="4 字节种子，如 0xD2E3FB68")
    ap.add_argument("--level", type=int, default=1, choices=[1, 2], help="安全等级（1 或 2）")
    ap.add_argument("--selftest", action="store_true", help="打印固定测试向量表")
    args = ap.parse_args()

    if args.selftest:
        print("%-12s %-4s %-12s" % ("seed", "lvl", "key"))
        for seed, level, _ in VECTORS:
            print("0x%08X   %-4d 0x%08X" % (seed, level, security_key(seed, level)))
        return 0
    if args.seed is None:
        ap.print_help()
        return 1
    seed = int(args.seed, 0)
    print("seed=0x%08X level=%d -> key=0x%08X" % (seed, args.level, security_key(seed, args.level)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
