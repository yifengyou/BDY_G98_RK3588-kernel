#!/bin/bash

set -ex

WORKDIR=$(pwd)

#make ARCH=arm64 \
#  CROSS_COMPILE=aarch64-linux-gnu- \
#  KBUILD_BUILD_USER="builder" \
#  KBUILD_BUILD_HOST="kdevbuilder" \
#  LOCALVERSION=-kdev \
#  mrproper

# build kernel Image
make ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KBUILD_BUILD_USER="builder" \
  KBUILD_BUILD_HOST="kdevbuilder" \
  LOCALVERSION=-kdev \
  bdy_g98_rk3588_recovery_defconfig

# check kver
KVER=$(make LOCALVERSION=-kdev kernelrelease)
KVER="${KVER/kdev*/kdev}"
if [[ "$KVER" != *kdev ]]; then
  echo "ERROR: KVER does not end with 'kdev'"
  exit 1
fi
echo "KVER: ${KVER}"

make ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KBUILD_BUILD_USER="builder" \
  KBUILD_BUILD_HOST="kdevbuilder" \
  LOCALVERSION=-kdev \
  dtbs \
  -j$(nproc)

make ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KBUILD_BUILD_USER="builder" \
  KBUILD_BUILD_HOST="kdevbuilder" \
  LOCALVERSION=-kdev \
  KCFLAGS="-Wno-unused-function" \
  -j$(nproc)

make ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KBUILD_BUILD_USER="builder" \
  KBUILD_BUILD_HOST="kdevbuilder" \
  LOCALVERSION=-kdev \
  KCFLAGS="-Wno-unused-function" \
  modules -j$(nproc)

make ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KBUILD_BUILD_USER="builder" \
  KBUILD_BUILD_HOST="kdevbuilder" \
  LOCALVERSION=-kdev \
  INSTALL_MOD_PATH=$(pwd)/kos \
  modules_install

# update rootfs modules
rm -rf rootfs/var/cache/apk/*
rm -rf rootfs/lib/modules/*
rm -f rootfs/root/.ash_history
cp -a kos/lib/modules/* rootfs/lib/modules/
du -sh rootfs

# rebuild kernel archive rootfs
make ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KBUILD_BUILD_USER="builder" \
  KBUILD_BUILD_HOST="kdevbuilder" \
  LOCALVERSION=-kdev \
  KCFLAGS="-Wno-unused-function" \
  -j$(nproc)

cat include/config/kernel.release
ls -alh arch/arm64/boot/Image

rm -rf ${WORKDIR}/output
rm -rf recovery
mkdir -p recovery

# kernel image
xz --format=lzma -k -c arch/arm64/boot/Image >recovery/kernel.lzma
ls -alh recovery/kernel.lzma

# only-for emmc dtb
cd recovery
dd if=/dev/zero of=recovery.img bs=1M count=28
mkimage -A arm64 -O linux -T kernel -C lzma \
  -a 0x40080000 -e 0x40080000 \
  -n "Recovery Kernel" \
  -d kernel.lzma kernel-uImage.lzma
mkfs.ext4 \
  -O '^metadata_csum,^has_journal,^resize_inode' \
  -N 16 -m 0 \
  -L recovery \
  recovery.img

cleanup() { mountpoint -q /mnt && umount /mnt || true; }
trap cleanup EXIT

mount recovery.img /mnt
df -h
cp -a ${WORKDIR}/arch/arm64/boot/dts/rockchip/rk3588-bdy-g98-only-emmc.dtb \
  /mnt/rk3588-bdy-g98.dtb
cp -a kernel-uImage.lzma /mnt
df -h

tee /mnt/recovery.conf <<'EOF'
label RK3588 Linux recovery
    kernel kernel-uImage.lzma
    fdt rk3588-bdy-g98.dtb
    append console=ttyS2,1500000n8 earlycon=uart8250,mmio32,0xfeb50000 rootwait rw
EOF
umount /mnt
sync
mkdir -p ${WORKDIR}/output/
cp -a recovery.img ${WORKDIR}/output/BDY_G98_RECOVERY_ONLY_EMMC.img
sync

# add only spi recovery
mount recovery.img /mnt
cp -a ${WORKDIR}/arch/arm64/boot/dts/rockchip/rk3588-bdy-g98-only-spi.dtb \
  /mnt/rk3588-bdy-g98.dtb
sync
umount /mnt
sync
cp -a recovery.img ${WORKDIR}/output/BDY_G98_RECOVERY_ONLY_SPI.img

ls -alh ${WORKDIR}/output/*.img
sha256sum ${WORKDIR}/output/*.img

echo "All done!All ok!"
