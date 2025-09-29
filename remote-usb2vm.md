# Configure remote usb to VM

Yes, you can absolutely achieve remote USB passthrough to your Proxmox VM without needing to connect via a SPICE client. What you're looking for is a "USB over IP" solution, which does exactly what you described: a daemon on your client machine shares a USB device over the network, and the VM connects to it.

The most common and open-source way to do this on Linux is with **USB/IP**.

---

### Using USB/IP for Network-Based USB Passthrough

USB/IP works by creating a server on the machine with the physical USB device (your Gentoo client) and a client inside the virtual machine.1 The VM's kernel is tricked into thinking the network-attached USB device is physically plugged in.2

Here is the general workflow to get it set up:

#### 1. On Your Gentoo Client (The "Server")

This is the machine where your USB device is physically connected.3

1. **Install USB/IP Tools:** Emerge the necessary package.4

   ```Bash
   sudo emerge --ask sys-apps/usbip
   ```

2. **Load Kernel Modules:** Load the modules required to host/share a USB device.
   ```Bash
   sudo modprobe usbip_core
   sudo modprobe usbip_host
   ```

3. **Start the Daemon:** Start the usbipd daemon, which listens for incoming connections. Running it in the foreground with debug (-d) is useful for the initial setup.
   ```Bash
   sudo usbipd -d
   ```

4. **List Local USB Devices:** Find the busid of the device you want to share.

   ```Bash
   usbip list --local
   ```

   You'll get an output like this. The 1-4 is the busid.
   
   - busid 1-4 (0525:a4a2)
     Linux Foundation : 2.0 root hub (0525:a4a2)

6. **Bind the Device for Sharing:** Use the busid from the previous step to make the device available for sharing over the network.

   ```Bash
   sudo usbip bind --busid=1-4
   ```

   After binding, your local OS can no longer use the device directly; it's now managed by USB/IP.

---

#### **\#\#\# 2\. Inside Your Proxmox VM (The "Client")**

Now, configure your Gentoo VM to attach to the shared device.5

1. **Install USB/IP Tools:** You need the same user-space tools inside the VM.

   ```Bash
   sudo emerge --ask sys-apps/usbip
   ```

3. **Load Kernel Module:** Load the virtual host controller module.6

   ```Bash
   sudo modprobe vhci-hcd
   ```

4. **List Remote Devices:** Probe your client machine's IP address to see the shared USB devices.

   ```Bash
   usbip list --remote=<IP_OF_YOUR_GENTOO_CLIENT>
   ```

6. **Attach the Device:** Use the IP address and the busid to attach the remote device to your VM.7

   ```Bash
   sudo usbip attach --remote=<IP_OF_YOUR_GENTOO_CLIENT> --busid=1-4
   ```

After running the attach command, you can run dmesg or lsusb inside your VM. You should see the kernel detect a new USB device as if you had just plugged it in physically.

---

### **\#\# Alternative Option: VirtualHere**

If you run into issues with USB/IP or want a more polished, commercial solution, **VirtualHere** is a popular alternative.

* **How it Works:** It uses the same server/client model as USB/IP. You run the VirtualHere Server on your client machine and the VirtualHere Client in your VM.
* **Advantages:** It's often considered easier to set up, has a graphical interface, and may offer better performance for latency-sensitive devices.
* **Disadvantages:** It is commercial software, though it offers a trial to share a single device for free.

For a pure Linux/Gentoo environment, USB/IP is the standard and most flexible approach.

This video provides a helpful overview of how to create a bootable USB drive, which is a foundational skill for managing different Linux environments like the one you are using.
Creating a Bootable USB for Gentoo Linux
~                                                                                                                                                                                                                                                             
