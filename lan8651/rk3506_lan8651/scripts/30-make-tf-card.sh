#!/bin/bash
# 在 Linux 上制作并写入 TF/SD 启动卡（RK3506 / Rockchip 全盘镜像方案）
#
# 原理：
#   1) update.img (RKFW) --programmer_image_tool -t emmc--> out_image.bin
#      （原始设备镜像：sector64 处 RKNS loader + GPT 分区表）
#   2) dd 到 TF 卡
#   3) 重建 GPT 到实际卡容量（全新表，rootfs 撑满；保留 PARTUUID 以支持 root=PARTUUID）
#   4) e2fsck + resize2fs 扩展 rootfs 文件系统
#
# 用法：
#   sudo bash scripts/30-make-tf-card.sh [-y] [-f] /dev/sdX [raw_image_or_update.img]
#     -y  跳过交互确认（危险）
#     -f  允许对“较大且疑似非可移动”的设备操作
#
# ⚠️ 会清空 /dev/sdX 上所有数据！务必确认设备名（用 lsblk 核对 removable=1 的卡）。
#
# 说明：
#   * 镜像里的 GPT 面向 2 TB、rootfs 为 grow 标记，直接 dd 后分区会越界，
#     因此必须按实际卡容量重建 GPT。
#   * rootfs 的 PARTUUID 必须为 614e0000-0000-4b53-8000-1d28000054a9
#     （来自 parameter-EMMC.txt 的 uuid:rootfs），否则内核 root=PARTUUID 无法挂载。
#   * 用 `sgdisk -o` 全新重建（而非 -d/-e/-n），可避免旧固件残留的备份 GPT 干扰，
#     保证主/备 GPT 完全一致。
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

YES=0; FORCE=0
while [ $# -gt 0 ]; do
  case "$1" in
    -y) YES=1; shift;;
    -f) FORCE=1; shift;;
    -*) die "未知参数：$1";;
    *) break;;
  esac
done

DEV="${1:-}"
IMG_ARG="${2:-}"

[ -n "$DEV" ] || die "用法: sudo bash $0 [-y] /dev/sdX [raw_image_or_update.img]"
[ "$(id -u)" = "0" ] || die "需要 root（写块设备）"
[ -b "$DEV" ] || die "$DEV 不是块设备"

# ---------- 安全校验 ----------
SIZE_BYTES=$(blockdev --getsize64 "$DEV")
SIZE_GB=$((SIZE_BYTES/1000/1000/1000))
DEVB=$(basename "$DEV")
REMOVABLE=$(cat "/sys/class/block/$DEVB/removable" 2>/dev/null || echo 0)
MODEL=$(cat "/sys/class/block/$DEVB/device/model" 2>/dev/null || echo "?")
log "目标设备: $DEV  容量: ${SIZE_GB} GB  removable=$REMOVABLE  model=$MODEL"

for mp in / /boot /boot/efi /mnt/sda3 /mnt/sda2 /mnt/sda; do
  src=$(findmnt -no SOURCE --target "$mp" 2>/dev/null | sed 's/[0-9]*$//' || true)
  [ -n "$src" ] && [ "$src" = "$DEV" ] && die "拒绝对承载 $mp 的设备 $DEV 操作！"
done

if [ "$REMOVABLE" != "1" ] && [ "$FORCE" != "1" ]; then
  warn "$DEV 报告为非可移动设备。若确为读卡器请加 -f 强制。"
  die "已中止（安全保护）。"
fi

lsblk "$DEV" || true
if [ "$YES" != "1" ]; then
  read -r -p "确认清空 $DEV 并写入镜像？输入 YES 继续: " a
  [ "$a" = "YES" ] || die "已取消"
fi

# ---------- 准备原始镜像 ----------
WORK_TF="$WORK_DIR/tf_card"
mkdir -p "$WORK_TF"
RAW="$WORK_TF/out_image.bin"

gen_raw() {
  local src="$1"
  PT_DIR="$WORK_DIR/tools/programmer_image_tool"; mkdir -p "$PT_DIR"
  tar xf "$SDK_DIR/tools/linux/programming_image_tool/programmer_image_tool_v1.26_linux.tar" -C "$PT_DIR"
  local tool; tool="$(find "$PT_DIR" -name programmer_image_tool -type f | head -1)"; chmod +x "$tool"
  local tmpdir="$WORK_TF/gen"; mkdir -p "$tmpdir"; cp -f "$src" "$tmpdir/update.img"
  ( cd "$tmpdir" && "$tool" -i update.img -t emmc ) >"$WORK_TF/convert.log" 2>&1
  local out; out="$(find "$PT_DIR" -name out_image.bin | head -1)"
  [ -n "$out" ] || die "生成原始镜像失败，见 $WORK_TF/convert.log"
  mv -f "$out" "$RAW"
}

if [ -n "$IMG_ARG" ]; then
  src="$IMG_ARG"
  [ -f "$src" ] || die "找不到输入文件：$src"
  case "$src" in
    *out_image.bin) RAW="$src";;
    *) log "由 $src 生成原始镜像..."; gen_raw "$src";;
  esac
else
  UPD="$SDK_DIR/output/update/Image/update.img"
  if [ ! -f "$RAW" ] || { [ -f "$UPD" ] && [ "$UPD" -nt "$RAW" ]; }; then
    [ -f "$UPD" ] || die "既无 $RAW 也无 $UPD，请先编译并指定镜像"
    if [ -f "$RAW" ]; then warn "检测到 $UPD 比 $RAW 新，重新生成原始镜像"; fi
    log "由 $UPD 生成原始镜像..."; gen_raw "$UPD"
  else
    log "复用已有原始镜像（如需强制更新请删除 $RAW 或显式传入 update.img）"
  fi
fi
log "使用原始镜像：$RAW ($(stat -c%s "$RAW") bytes, md5=$(md5sum "$RAW" | awk '{print $1}'))"

# ---------- 卸载目标设备上的分区 ----------
for p in "$DEV"?*; do
  [ -b "$p" ] || continue
  findmnt -no TARGET --source "$p" >/dev/null 2>&1 && umount "$p" 2>/dev/null || true
done

# ---------- 写入 ----------
log "写入 $DEV ..."
dd if="$RAW" of="$DEV" bs=4M conv=fsync status=progress
sync
partprobe "$DEV" 2>/dev/null || true
sleep 2

# ---------- 全新重建 GPT（按实际卡容量，rootfs 撑满） ----------
log "重建 GPT（容量 ${SIZE_GB}GB）..."
sgdisk -o "$DEV" >/dev/null
sgdisk \
 -n 1:16384:24575    -c 1:uboot    -t 1:f808d051-1602-4dcd-9452-f9637fefc49a -u 1:b750e44e-833f-4a30-c38c-b117241d84d4 \
 -n 2:24576:32767    -c 2:misc     -t 2:c6d08308-e418-4124-8890-f8411e3d8d87 -u 2:a1c81622-7741-47ad-b846-c6972488d396 \
 -n 3:32768:163839   -c 3:boot     -t 3:2a583e58-486a-4bd4-ace4-8d5454e97f5c -u 3:43784a32-a03d-4ade-92c6-ede64ff9b794 \
 -n 4:163840:425983  -c 4:recovery -t 4:6115f139-4f47-4baf-8d23-b6957eaee4b3 -u 4:000b305f-484a-4582-9090-4ad0099d47bd \
 -n 5:425984:688127  -c 5:oem      -t 5:a83fba16-d354-45c5-8b44-3ec50832d363 -u 5:24eeb649-277f-4c11-ffeb-d9f20027a83b \
 -n 6:688128:2785279 -c 6:userdata -t 6:500e2214-b72d-4cc3-d7c1-8419260130f5 -u 6:1cac805f-726a-495a-fd35-821355a6e7e8 \
 -n 7:2785280:0      -c 7:rootfs   -t 7:e099da71-5450-44ea-aa9f-1b771c582805 -u 7:614e0000-0000-4b53-8000-1d28000054a9 \
 "$DEV" >/dev/null
partprobe "$DEV" 2>/dev/null || true
sleep 2
sgdisk -v "$DEV" 2>&1 | tail -2

# ---------- 扩展 rootfs 文件系统 ----------
P7="${DEV}7"; [ -b "$P7" ] || P7=$(ls "${DEV}"*7 2>/dev/null | head -1)
if [ -n "$P7" ] && [ -b "$P7" ]; then
  log "扩展 rootfs 文件系统：$P7"
  e2fsck -f -y "$P7" >/dev/null 2>&1 || true
  resize2fs "$P7" 2>&1 | tail -2 || warn "resize2fs 失败（不影响启动）"
fi

log "完成。最终布局："
lsblk -o NAME,SIZE,FSTYPE,LABEL,PARTUUID,MOUNTPOINT "$DEV" || true
log "把 TF 卡插入板卡 CARD1，串口(1500000)观察启动日志。"
