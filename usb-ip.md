Client (importing the USB device). [1]
------------------------------
## Step 1: Install usbip on Both Host and VM via Portage
Instead of building it from the kernel source tree manually, use Portage to compile the official tools on both machines. [2] 
Enable the usbip USE flag on sys-apps/linux-misc-apps: [2] 

echo "sys-apps/linux-misc-apps usbip" >> /etc/portage/package.use/usbip
emerge --ask sys-apps/linux-misc-apps

------------------------------
## Step 2: Configure the Kernels## On the Laptop (Server/Host)
The physical laptop needs the core USB/IP host driver (usbip-core and usbip-host). Run make menuconfig in /usr/src/linux:

Device Drivers --->
  [*] USB support --->
    <M> USB/IP support
      <M>   Host driver

## On the Hyper-V VM (Client/Guest)
The virtual machine needs the virtual controller interface driver (vhci-hcd): [1] 

Device Drivers --->
  [*] USB support --->
    <M> USB/IP support
      <M>   VHCI hcd (Virtual Host Controller Interface)

Don't forget to run make && make modules_install && make install and reboot on both machines if you compile them as built-in (*), or load them using modprobe if compiled as modules (M).
------------------------------
## Step 3: Export the USB Drive from the Laptop
Plug the USB drive into your physical Gentoo laptop and run the server daemon.

   1. Load the host kernel modules:
   
   ```bash
   sudo modprobe usbip-core
   sudo modprobe usbip-host
   ```
   
   2. Start the network daemon (binds to port 3240 by default):
   
   ```bash
   sudo usbipd -D
   ```

   Or

   ```bash
   sudo systemctl start usbipd.service
   ```
   
   3. Find the Bus ID of your USB drive:
   
   ```bash
   usbip list -l
   ```
   
   Look for your drive in the list. Note the Bus ID (e.g., 1-1.2).
   4. Bind and export the device:
   
   ```bash
   sudo usbip bind -b 1-1.2
   ```
   
   
------------------------------
## Step 4: Import and Mount inside the Hyper-V VM
Switch to your Gentoo Virtual Machine. Ensure the VM can ping your laptop's network IP address across your Hyper-V virtual switch.

   1. Load the client kernel module:
   
   ```bash
   sudo modprobe vhci-hcd
   ```
   
   2. List the exported devices coming from the laptop's IP address:
   
   ```bash
   usbip list -r <LAPTOP_IP_ADDRESS>
   ```
   
   3. Attach the remote USB drive:
   
   ```bash
   sudo usbip attach -r <LAPTOP_IP_ADDRESS> -b 1-1.2
   ```
   
   4. Verify and Mount:
   Run lsusb or dmesg | tail to see the virtual attachment. The drive will show up under /dev/sdX. You can now mount it normally:
   
   ```bash
   sudo mount /dev/sdb1 /mnt/usb
   ```
   
   
When you are completely finished with the drive, disconnect it safely from the VM using sudo usbip detach -p 00 (where 00 is the port number displayed in usbip port).
Are you using a custom manual kernel configuration on these Gentoo installations, or are you utilizing the binary distribution kernel (sys-kernel/gentoo-kernel-bin)?

[1] [https://wiki.gentoo.org](https://wiki.gentoo.org/wiki/Hyper-V)
[2] [https://packages.gentoo.org](https://packages.gentoo.org/useflags/usbip)
