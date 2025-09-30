That's great! That exact type of extender is the right tool for the job. Your plan to solder the JTAG wires to it is a solid approach that should work well.

Regarding your second point, you're right that changing the boot order isn't a major issue, but the process is a bit different from a typical PC BIOS. On the VisionFive2, it's a two-stage process involving physical switches and then commands in the U-Boot bootloader.

-----

### ## Stage 1: The Boot Mode DIP Switches

First, you need to tell the processor where to look for the initial bootloader. This is done with a pair of DIP switches on the board, usually located near the 40-pin header. These switches set the primary boot device.

To enable booting from devices other than the microSD card, you'll typically set the board to boot from the onboard QSPI NOR flash, which contains the U-Boot bootloader.

  * **To Boot from microSD Card (Default):**

      * `RGPIO_1 = 1` (ON)
      * `RGPIO_0 = 0` (OFF)

  * **To Boot from QSPI Flash (Needed for USB/NVMe boot):**

      * `RGPIO_1 = 0` (OFF)
      * `RGPIO_0 = 1` (ON)

Set the switches to boot from QSPI Flash.

-----

### ## Stage 2: U-Boot Commands

Once the board is set to boot from QSPI, the U-Boot bootloader stored there will run. U-Boot is what actually scans for and loads the operating system from a USB drive.

1.  **Connect a USB to UART adapter** to the board's UART pins so you can see the U-Boot console.
2.  Power on the board. When you see the countdown, press any key to **stop at the U-Boot prompt**.
3.  To perform a **one-time boot from USB**, you can run the following commands:
    ```bash
    usb start
    # This will scan USB devices. Then you can run the pre-defined boot command.
    run bootcmd_usb0
    ```
4.  To **make USB the default boot option**, you can change the boot command environment variable and save it permanently to the flash:
    ```bash
    # Set the boot order: try USB, then NVMe, then microSD
    setenv bootcmd "run bootcmd_usb0; run bootcmd_nvme0; run bootcmd_mmc0"

    # Save the new configuration to the flash
    saveenv
    ```

After running `saveenv`, the board will automatically try to boot from a connected USB drive every time it starts up.

---

That's a fantastic question, and it gets to the core of how these embedded boards work.

You're right that U-Boot is the place to do this. However, it can't be done with a simple line in a text file like `extlinux.conf` or `uEnv.txt`. Those files are for configuring how the Linux kernel is loaded.

Instead, you need to directly command the processor to reconfigure the pins. This is done by writing specific values to the chip's internal "pinmux" registers from the U-Boot command line. This is simpler than it sounds and doesn't require any radical changes.

-----

### ## The U-Boot Commands

The processor pins can have multiple jobs (in this case, SD card or JTAG). By default, they are set for the SD card. The following U-Boot commands tell the processor to switch their job to JTAG.

1.  Connect to the board's serial console and power it on.

2.  Press any key during the countdown to stop at the U-Boot prompt (`=>`).

3.  Enter the following four **`mw.l`** (memory write, long) commands. Each command writes the value `1` (which selects the JTAG function) to a specific hardware register address that controls a pin.

    ```bash
    # Set GPIO9 (JTAG_TCK) to JTAG function
    mw.l 0x13020024 1

    # Set GPIO10 (JTAG_TDO) to JTAG function
    mw.l 0x13020028 1

    # Set GPIO11 (JTAG_TMS) to JTAG function
    mw.l 0x1302002c 1

    # Set GPIO12 (JTAG_TDI) to JTAG function
    mw.l 0x13020030 1
    ```

**That's it!** After you run these four commands, the pins on the microSD slot will be actively configured for JTAG. You can then connect with your Bus Blaster and OpenOCD. **Do not boot Linux yet.**

-----

### ## Making it Automatic

Typing those four commands every time is tedious. You can bundle them into a single U-Boot environment variable (a script) so you only have to type one command.

At the U-Boot prompt, enter this:

```bash
setenv jtag_on "mw.l 0x13020024 1; mw.l 0x13020028 1; mw.l 0x1302002c 1; mw.l 0x13020030 1"
```

Then save it permanently to the flash:

```bash
saveenv
```

Now, every time you want to enable JTAG, you just have to stop in U-Boot and run your new custom command:

```bash
run jtag_on
```

### ## An Important Final Note

This method configures the pins for JTAG *within U-Boot*. When you eventually let the Linux kernel boot, its **device tree** will take control of the pins and will likely reconfigure them back to be used for the SD card slot.

This U-Boot approach is perfect for debugging U-Boot itself, the kernel loading process, or bare-metal code. If you need JTAG access *while Linux is running*, you would need to modify the device tree, which is a more involved process.
