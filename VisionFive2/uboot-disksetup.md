The disk configuration was based on ChatGPT deduction from uboot environment.

Yes — your U-Boot env basically *tells you* the disk layout it expects.

### What the env is telling us

* `partitions=...` is the smoking gun:

  ```
  name=loader1,start=17K,size=1M,type=${type_guid_gpt_loader1};
  name=loader2,size=4MB,type=${type_guid_gpt_loader2};
  name=system,size=-,bootable,type=${type_guid_gpt_system};
  ```

  with the GUIDs:

  * `type_guid_gpt_loader1 = 5B193300-FC78-40CD-8002-E86C45580B47`
  * `type_guid_gpt_loader2 = 2E54B353-1271-4842-806F-E436D6AF6985`
  * `type_guid_gpt_system  = 0FC63DAF-8483-4772-8E79-3D69D8477DE4` (Linux filesystem)

* It boots from `mmc0` (`boot_targets=mmc0 dhcp`) and looks for **distro boot**:

  * Extlinux path: `extlinux/extlinux.conf` on a bootable partition (`scan_dev_for_extlinux` + `boot_extlinux`)
  * EFI path: `/efi/boot/bootriscv64.efi` (`boot_efi_binary`)

* `fdtfile=starfive/starfive_visionfive2.dtb` and the distro boot helpers expect `/dtbs/${fdtfile}` to exist on the same partition they read from.

> So the intended card is **GPT with three partitions**:
>
> 1. `loader1` (1 MiB at offset 17 KiB, special GUID)
> 2. `loader2` (4 MiB, special GUID)
> 3. `system` (rest of the card, **bootable**, Linux-fs GUID) — this holds `/boot/` (extlinux or EFI), `/dtbs/`, kernel, and your rootfs (Gentoo).

You **do not need** a separate FAT/ESP unless you really want to boot via UEFI conventions. U-Boot’s distro boot will happily read `extlinux.conf` from ext4.

---

## Quick, reproducible setup (on your Linux host)

> Replace `/dev/sdX` with your SD device. **Double-check** before running.

1. Partition the card (exact GUIDs, bootable flag on p3):

```bash
sudo sgdisk --zap-all /dev/sdX

# GPT + three partitions
sudo sgdisk \
  --new=1:34:+1MiB        --change-name=1:loader1 --typecode=1:5B193300-FC78-40CD-8002-E86C45580B47 \
  --new=2:0:+4MiB         --change-name=2:loader2 --typecode=2:2E54B353-1271-4842-806F-E436D6AF6985 \
  --new=3:0:0             --change-name=3:system  --typecode=3:0FC63DAF-8483-4772-8E79-3D69D8477DE4 \
  /dev/sdX

# Mark p3 bootable (legacy BIOS bootable attribute in GPT — matches env's "bootable")
sudo sgdisk --attributes=3:set:2 /dev/sdX   # attribute bit 2 is "legacy BIOS bootable" in sgdisk
```

*(The `start=17K` detail in env is U-Boot’s internal template; `sgdisk` chooses safe aligned LBAs; it’s fine.)*

2. Make filesystem for `system` and mount:

```bash
sudo mkfs.ext4 -L system /dev/sdX3
sudo mkdir -p /mnt/vf2
sudo mount /dev/sdX3 /mnt/vf2
sudo mkdir -p /mnt/vf2/boot/extlinux /mnt/vf2/dtbs
```

3. Copy kernel, DTBs, and (optionally) initramfs:

```bash
# Example paths — adjust to your Gentoo build artifacts
sudo cp /path/to/Image             /mnt/vf2/boot/Image
sudo cp -r /path/to/dtbs/*         /mnt/vf2/dtbs/
# optional
# sudo cp /path/to/initramfs.cpio.gz /mnt/vf2/boot/initramfs.cpio.gz
```

4. Create `extlinux.conf` that matches your env (`fdtfile` etc.):

```bash
cat <<'EOF' | sudo tee /mnt/vf2/boot/extlinux/extlinux.conf
timeout 3
menu title Gentoo on VisionFive 2

label gentoo
    menu label Gentoo
    kernel /boot/Image
    # If you built an initramfs, uncomment the next line:
    # initrd /boot/initramfs.cpio.gz
    devicetree /dtbs/starfive/starfive_visionfive2.dtb
    append console=ttyS0,115200 root=/dev/mmcblk0p3 rootwait earlycon=sbi rw
EOF
```

That’s it. Eject the card and boot. U-Boot will scan `mmc0`, find `extlinux/extlinux.conf` on the bootable `system` partition, load `/boot/Image`, `/dtbs/...dtb`, and pass your `append` line as kernel cmdline.

---

## Notes, gotchas, and options

* **About the `fatbootpart` lines**: you’ll see helpers like `fdt_loaddtb=fatload ...` in your env. Those only matter for the `bootcmd_distro` path that forces FAT. The *generic* distro boot path (`boot_extlinux` via `scan_dev_for_extlinux`) accepts `any` fs and will read ext4 just fine. With the layout above, the extlinux path wins and works.
* **EFI boot (optional)**: If you *prefer* UEFI-style boot, you can add a small FAT32 ESP (not present in the provided `partitions=` template) and drop `BOOTRISCV64.EFI` at `/EFI/BOOT/`. That would be a **fourth** partition of type `c12a7328-f81f-11d2-ba4b-00a0c93ec93b` (ESP). Not required for VF2 + U-Boot; extlinux is simpler.
* **Root device**: on VF2, the SD usually shows up as `/dev/mmcblk0`, partition 3 → `/dev/mmcblk0p3` (what I used in `append`).
* **Loader1/Loader2**: these are reserved slots some vendors use for SPL/TF-A variants. You can leave them empty; U-Boot can boot from SD just fine using the `system` partition with extlinux.
* **Gentoo specifics**: once booted, you can keep `/boot` on the same ext4 partition as `/` (simple) or split later. The env does not force a split.

If you want, share your kernel/DTB paths and I’ll pre-fill an `extlinux.conf` that matches your exact locations and kernel args (e.g., enabling your preferred console, loglevel, cgroups, etc.).
