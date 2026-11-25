# tools/a2l —— A2L 工具链（生成 → 校验 → pyxcp 使用）

A2L（ASAM MCD-2 MC）是"ECU 内存地图"：变量的**绝对地址**、类型、换算、采集事件都在里面。
地址必须与烧进 Flash 的 ELF 一致，**手工抄地址是标定失效的头号原因**，所以本章用脚本生成。

## 全流程

~~~bash
# 1) 编译（得到带 .symtab 的 ELF）
cd hpm5e31_1
bash scripts/build.sh samples/app/xcp_can_cal

# 2) 生成 A2L：ELF 符号 -> 绝对地址
python3 tools/gen_a2l.py --elf build/xcp_can_cal-debug/output/demo.elf \
        --vars tools/a2l/xcp_can_cal.json \
        --out samples/app/xcp_can_cal/xcp_can_cal.a2l
#    -> 已生成 ...：MEASUREMENT 6 / CHARACTERISTIC 6 / AXIS_PTS 1 / EVENT 1，告警 0 条

# 3) 校验（DanielT/a2ltool，Rust）
a2ltool samples/app/xcp_can_cal/xcp_can_cal.a2l --check
#    -> Consistency check complete. No problems found.

# 4) 上位机用同一份 A2L
python3 host/xcp/xcp_cal.py --a2l samples/app/xcp_can_cal/xcp_can_cal.a2l --channel vcan0
~~~

## 变量描述表（\`tools/a2l/*.json\`）

| 字段 | 说明 |
|---|---|
| \`project\` / \`module\` | PROJECT/HEADER/MODULE（\`project_no\` 是**标识符**，如 PRJ1） |
| \`memory_segments\` | MOD_PAR MEMORY_SEGMENT（RAM 0x01200000+256KB、FLASH 0x80000000+1MB） |
| \`xcp\` | CAN ID（0x555/0x556）、MAX_CTO/MAX_DTO、时间戳模式、可选命令列表（写进 IF_DATA XCP） |
| \`daq\` / \`events\` | DAQ 配置类型、MAX_DAQ、事件通道（名字/周期/单位/优先级） |
| \`measurements\` | 观测变量：\`symbol\`、\`type\`、\`conv\`、\`unit\`、min/max、\`event\` |
| \`characteristics\` | 标定量：\`kind\`=VALUE/CURVE，CURVE 需 \`axis_ref\` + \`points\` |
| \`axes\` | AXIS_PTS（\`points\` 决定数组长度；生成器校验 ELF 符号大小 = points × 元素大小） |

## 生成器实现要点

1. **纯 Python 解析 ELF32**（\`.symtab\` + 字符串表），不依赖 readelf/nm；
2. **类型/大小一致性检查**：符号大小与声明类型不符时告警（\`--strict\` 直接报错），防"数组当标量"；
3. **AXIS_PTS 自动配 \`RL_AXIS_*\` 记录布局**（含 \`AXIS_PTS_X\`），否则 a2ltool 一致性检查不过；
4. \`--no-ifdata\` 可关掉 IF_DATA XCP；\`--list-syms\` 单独查符号地址/大小。

## a2ltool（Rust）构建

~~~bash
cd $HOME/tmp/tp-xcp/a2ltool          # 从 ref2 复制出来，避免污染参考仓库
export https_proxy=http://<proxy>:7890
cargo build --release                            # 约 50 s
./target/release/a2ltool <file.a2l> --check
~~~

a2ltool 还有 \`--elffile <elf> --update\`（按 ELF 刷新 A2L 地址）、\`--merge\`（合并 A2L）等能力；
若以后改成"C 源码注解生成 A2L"路线，可用 \`--elffile --update\` 保持地址同步。

## 遇到过的语法问题（a2ltool 报错原文 → 修法）

| 报错 | 原因 | 修法 |
|---|---|---|
| \`expected Identifier, got Number ("1") inside PROJECT_NO\` | \`PROJECT_NO\` 值必须是标识符 | \`PROJECT_NO PRJ1\` |
| \`expected End, got Number ("-1") inside MEMORY_SEGMENT\` | 位置参数到 SIZE 为止 | 去掉多写的 5 个偏移量 |
| \`expected Identifier, got Number ("0") inside AXIS_PTS\` | AXIS_PTS 位置参数含 \`max_diff\` | \`record_layout 0 conversion max_points lower upper\` |
| \`Referenced RECORD_LAYOUT RL_UWORD does not have AXIS_PTS_X\` | 轴表要用带 AXIS_PTS_X 的布局 | 生成 \`RL_AXIS_UWORD\` |
