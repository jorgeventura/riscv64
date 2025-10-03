#!/bin/bash

# from your kernel tree
make -j32 ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- Image modules dtbs


KREL=$(make -s kernelrelease)

sudo make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     INSTALL_MOD_PATH=/mnt/gentoo modules_install

# generate module deps inside the target root:
sudo depmod -a -b /mnt/gentoo "${KREL}"

# Install DTBS
sudo make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     INSTALL_DTBS_PATH=/mnt/gentoo/dtbs dtbs_install

sudo install -D -m 0644 arch/riscv/boot/Image /mnt/gentoo/boot/Image-${KREL}
sudo ln -sf Image-${KREL} /mnt/gentoo/boot/Image      # stable name for extlinux
sudo install -m 0644 System.map /mnt/gentoo/boot/System.map-${KREL}
sudo install -m 0644 .config     /mnt/gentoo/boot/config-${KREL}

