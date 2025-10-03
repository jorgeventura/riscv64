
# Compile bundle it with OpenSBI, and boot it **from microSD** without touching QSPI. Here’s the clean, upstream-friendly path that works great on the VisionFive 2 (JH7110).

---

# 1) Toolchain + env

```bash
# Example prefixes; use your actual cross toolchain
export CROSS_COMPILE=riscv64-unknown-linux-gnu-
export ARCH=riscv
```

---

# 2) Build OpenSBI (FW_DYNAMIC)

Build a generic OpenSBI **fw_dynamic.bin** (no payload yet). U-Boot will pack this into a FIT image later.

```bash
git clone https://github.com/riscv/opensbi.git
cd opensbi
make PLATFORM=generic FW_TEXT_START=0x40000000
# result: build/platform/generic/firmware/fw_dynamic.bin
```

(Yes, VisionFive 2 uses GENERIC platform here; upstream U-Boot expects `fw_dynamic.bin`.) ([U-Boot Documentation][1])

---

# 3) Build U-Boot for VisionFive 2

```bash
cd ..
git clone https://source.denx.de/u-boot/u-boot.git
cd u-boot
# VisionFive 2 defconfig:
make starfive_visionfive2_defconfig

# Point U-Boot build at the OpenSBI fw_dynamic.bin you built above:
make -j$(nproc) OPENSBI=../opensbi/build/platform/generic/firmware/fw_dynamic.bin
```

This produces:

* **`spl/u-boot-spl.bin.normal.out`** (the SPL)
* **`u-boot.itb`** (a FIT image that already contains OpenSBI + U-Boot) ([U-Boot Documentation][1])

> Notes:
>
> * Upstream U-Boot now ships **`u-boot.itb`** as the OpenSBI+U-Boot bundle. Older StarFive docs sometimes call a similar file `visionfive2_fw_payload.img`; use `u-boot.itb` when building upstream as above. ([U-Boot Documentation][1])

---

# 4) Prepare a **microSD** with the right GPT layout

The ROM/SPL expect two raw partitions:

* **Partition 1** (SPL): type GUID `2E54B353-1271-4842-806F-E436D6AF6985`
* **Partition 2** (FIT / U-Boot+OpenSBI): type GUID `BC13C2FF-59E6-4262-A352-B275FD6F7172`

Example (replace `/dev/sdX` with your card device):

```bash
sudo wipefs -a /dev/sdX
sudo sgdisk -og /dev/sdX

# p1: small (say 4 MiB) for SPL
sudo sgdisk -n1:2048:+4M  -t1:2E54B353-1271-4842-806F-E436D6AF6985 -c1:"spl"

# p2: small (say 16 MiB) for FIT (u-boot.itb)
sudo sgdisk -n2:0:+16M   -t2:BC13C2FF-59E6-4262-A352-B275FD6F7172 -c2:"uboot"

sudo partprobe /dev/sdX
```

Those GUIDs and the “SPL in p1 / FIT in p2” expectations are documented in the upstream U-Boot VisionFive 2 page. ([U-Boot Documentation][1])

---

# 5) Write your freshly built images to the partitions

```bash
# Write SPL to p1
sudo dd if=spl/u-boot-spl.bin.normal.out of=/dev/sdX1 conv=fsync

# Write FIT (OpenSBI+U-Boot) to p2
sudo dd if=u-boot.itb of=/dev/sdX2 conv=fsync
sync
```

StarFive’s quick-start similarly shows writing SPL to `p1` and the payload to `p2`. ([RVspace Documentation Center][2])

---

# 6) Boot from SD (no QSPI changes)

* Set the VF2 boot DIP/SW to **SD boot**.
* Connect serial, power on.
* You should see your new U-Boot banner (build date, git hash).

StarFive’s docs cover the boot-mode switch location and SD boot details. ([RVspace Documentation Center][3])

---

## Alternative: build a “payload” via OpenSBI instead of U-Boot FIT

If you prefer the older StarFive flow:

```bash
# Build U-Boot first to get u-boot.bin
make -j$(nproc)

# Then build OpenSBI with U-Boot as payload:
cd ../opensbi
make PLATFORM=generic FW_TEXT_START=0x40000000 \
     FW_PAYLOAD_PATH=../u-boot/u-boot.bin
# result: build/.../fw_payload.bin

# Some StarFive guides then convert/rename to visionfive2_fw_payload.img,
# but with upstream U-Boot it's simpler to use u-boot.itb as above.
```

This “FW_PAYLOAD_PATH” technique is the standard OpenSBI payload method, used widely (VF2 docs and U-Boot board docs show it). ([RVspace Documentation Center][4])

---

## Verify & iterate

At the U-Boot prompt:

```bash
version
bdinfo
printenv
```

If all looks good booting from SD, you can later flash the same **SPL** and **u-boot.itb** into QSPI at offsets `0x0` and `0x100000` respectively (via `sf probe; sf update ...`). ([en.opensuse.org][5])

---

If you want, tell me your exact toolchain prefix (you’re on Gentoo with crossdev, right?) and I’ll drop in a ready-to-paste script that builds OpenSBI + U-Boot and writes the card in one go.

[1]: https://docs.u-boot.org/en/latest/board/starfive/visionfive2.html?utm_source=chatgpt.com "StarFive VisionFive2 - The U-Boot Documentation"
[2]: https://doc-en.rvspace.org/VisionFive2/Quick_Start_Guide/VisionFive2_SDK_QSG/updating_spl_and_u_boot_of_sd_card_and_emmc.html?utm_source=chatgpt.com "Updating SPL and U-Boot of SD Card and eMMC"
[3]: https://doc-en.rvspace.org/VisionFive2/Boot_UG/VisionFive2_SDK_QSG/boot_mode_settings.html?utm_source=chatgpt.com "VisionFive 2 Boot Mode Settings"
[4]: https://doc-en.rvspace.org/VisionFive2/SWTRM/VisionFive2_SW_TRM/compiling_opensbi%20-%20vf2.html?utm_source=chatgpt.com "Compiling OpenSBI"
[5]: https://en.opensuse.org/VisionFive2%3ATips?utm_source=chatgpt.com "VisionFive2:Tips"



# Prepare to reflash QSPI

1. **MMC index** — if your microSD shows up as **mmc 1**, just use `mmc 1:<part>` everywhere (e.g., `mmc 1:3`).

2. **Same RAM address** — you’re right that my earlier “load twice in a row” snippet looked wrong. The correct pattern is:
   **load → flash → load → flash**.
   You *can* reuse the same RAM buffer address (e.g., `$kernel_addr_r`) **as long as you flash the first image before loading the second**, so nothing important gets overwritten. The upstream VF2 docs show this exact flow. ([docs.u-boot.org][1])

Here’s a clean, copy-paste sequence that uses your **mmc 1:3** partition and the recommended U-Boot vars:

```bash
# 0) Make sure you're running the NEW U-Boot from microSD
mmc list
mmc dev 1             # use the card that holds your files
sf probe              # init SPI NOR (QSPI)

# 1) SPL -> QSPI @ 0x0
load mmc 1:3 $kernel_addr_r u-boot-spl.bin.normal.out
setenv spl_size $filesize
sf update $kernel_addr_r 0x0 $spl_size

# (optional) verify SPL by readback + CRC32
crc32 $kernel_addr_r $spl_size; setenv spl_crc_src $crc32
sf read $fdt_addr_r 0x0 $spl_size
crc32 $fdt_addr_r $spl_size; echo SPL CRC src=$spl_crc_src dst=$crc32

# 2) U-Boot FIT (OpenSBI+U-Boot) -> QSPI @ 0x100000
load mmc 1:3 $kernel_addr_r u-boot.itb
setenv fit_size $filesize
sf update $kernel_addr_r 0x100000 $fit_size

# (optional) verify FIT too
crc32 $kernel_addr_r $fit_size; setenv fit_crc_src $crc32
sf read $fdt_addr_r 0x100000 $fit_size
crc32 $fdt_addr_r $fit_size; echo FIT CRC src=$fit_crc_src dst=$crc32

# 3) (optional) clear any stale saved env and reboot
env erase
reset
```

Notes & why this is safe:

* The **offsets** on VF2 QSPI are **`0x0` for SPL** and **`0x100000` for the FIT image**. That’s exactly what the upstream U-Boot VF2 page documents under “Flashing a new U-Boot version.” ([docs.u-boot.org][1])
* `sf update` will erase/program as needed (see the U-Boot `sf` command docs). ([docs.u-boot.org][2])
* Using `$kernel_addr_r`/`$fdt_addr_r` avoids hardcoding a RAM address; it’s the style used in the official doc example (`load mmc 1:1 $kernel_addr_r …; sf update $kernel_addr_r …`). ([docs.u-boot.org][1])

If you’d like, I can wrap those into a tiny `update.scr` so you just `load mmc 1:3 $scriptaddr update.scr; source $scriptaddr` from the SD-booted U-Boot and it will reflash QSPI automatically.

And again—thanks for flagging the address overlap confusion. The sequential **load→flash→load→flash** is the correct approach.

[1]: https://docs.u-boot.org/en/latest/board/starfive/visionfive2.html "StarFive VisionFive2 — Das U-Boot unknown version documentation"
[2]: https://docs.u-boot.org/en/latest/usage/cmd/sf.html?utm_source=chatgpt.com "sf command — Das U-Boot unknown version documentation"

