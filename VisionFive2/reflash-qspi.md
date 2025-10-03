Great catch on both points.

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

