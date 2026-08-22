# VisionFive 2 (`riscv64`) Standby Firewall & HA Configuration (`vf2-firewall.md`) — Pure OpenRC + nftables Edition

#### The VisionFive 2 HAS NO RTC

Ensure an NTP client runs at boot so DHCPv6/SLAAC leases and certificates behave reliably across power cycles.

---

## 1. Network Interface Topology

| Purpose | VF2 Physical Interface | Logical / Virtual Interface | VIP / Network |
| --- | --- | --- | --- |
| **WAN Parent** | `end0` | — | None (Trunk transport)
| **WAN (ISP)** | `end0` | `vlan0` (VLAN tag 100) | Dynamic ISP DHCP / SLAAC / PD
| **LAN** | `end1` | — | `192.168.51.61/24` *(Node IP)*<br> |
| **LAN IPv4 VIP** | `end1` | `vrrp.52` | `192.168.51.254/24`<br> |
| **LAN IPv6 VIP** | `end1` | `vrrp.53` | `fe80::1:254/64`<br> |

---

## 2. Package Installation

On the `amd64` Build Host (`pptgentoo`):

```bash
riscv64-unknown-linux-gnu-emerge -av sys-cluster/keepalived net-misc/radvd net-dns/dnsmasq net-firewall/nftables net-misc/dhcpcd net-misc/netifrc sys-apps/busybox
emaint binhost -f
```

On the VisionFive 2 (`riscv64`):

```bash
emaint binhost -s
emerge -avK sys-cluster/keepalived net-misc/radvd net-dns/dnsmasq net-firewall/nftables net-misc/dhcpcd net-misc/netifrc sys-apps/busybox
```

---

## 3. Kernel Sysctl Overrides (`/etc/sysctl.d/90-vrrp.conf`)

```ini
# Global Packet Forwarding
net.ipv4.ip_forward = 1
net.ipv6.conf.all.forwarding = 1
net.ipv6.conf.default.forwarding = 1

# Force kernel to accept Router Advertisements on WAN despite forwarding=1
net.ipv6.conf.all.accept_ra = 0
net.ipv6.conf.default.accept_ra = 0
net.ipv6.conf.vlan0.accept_ra = 0
net.ipv6.conf.vlan0.autoconf = 0

# LAN Interface Controls
net.ipv6.conf.eth1.accept_ra = 0
net.ipv6.conf.eth1.autoconf = 0

# Force immediate Neighbor Discovery updates when VRRP transitions
net.ipv6.conf.all.ndisc_notify = 1
net.ipv6.conf.default.ndisc_notify = 1

# Loose Reverse Path Filtering for Keepalived / VMAC compatibility (IPv4)
net.ipv4.conf.all.rp_filter = 2
net.ipv4.conf.default.rp_filter = 2
net.ipv4.conf.vlan0.rp_filter = 2
net.ipv4.conf.eth1.rp_filter = 2

# Prevent host interfaces from responding to ARP for Virtual MAC addresses
net.ipv4.conf.all.arp_ignore = 1
net.ipv4.conf.all.arp_announce = 2
```

Apply immediately:

```bash
sysctl --system
```

---

## 4. OpenRC Native Interface Setup (`/etc/conf.d/net`)

Configure the static LAN interface on `end1` and parent link `end0`. The `vlan0` interface remains inactive on boot until triggered by Keepalived upon failover.

```bash
# LAN static IP
config_end1="192.168.51.45/24"

# WAN parent carrier
config_end0="null"

# WAN VLAN interface definition (will be brought up dynamically by keepalived)
vlans_end0="100"
vlan100_name="vlan0"
mac_vlan0="00:01:2e:78:05:ac"
dhcp_vlan0="nodhcp"
config_vlan0="null"
```
Create OpenRC symlinks and start the LAN interface:

```bash
cd /etc/init.d
ln -s net.lo net.end0
ln -s net.lo net.end1
ln -s net.lo net.vlan0

rc-update add net.end0 default
rc-update add net.end1 default
rc-service net.end0 start
rc-service net.end1 start
```

Configure NTP server:

```bash
rc-update add busybox-ntpd default
```

Configure syslogd:

Edit the file /etc/conf.d/busybox-syslogd:

```text
# Options to pass to syslogd
# -O: File to log to (default: /var/log/messages)
# -s: Max file size in KB before rotating (e.g., 200KB)
# -b: Number of rotated files to keep
# -C: Use a circular memory buffer instead of disk writes (view with logread)
SYSLOGD_OPTS="-O /var/log/messages -s 256 -b 2"

# Options to pass to klogd (captures kernel printk messages into syslog)
KLOGD_OPTS=""
```

```bash
rc-update add busybox-syslogd default
```

Load macvlan Kernel Module at Boot:

```bash
echo "macvlan" > /etc/modules-load.d/network.conf
rc-update add modules boot
```

---

## 5. DHCPCD Configuration for WAN (`/etc/dhcpcd.conf`)

Ensure `dhcpcd` manages DHCPv4 and Prefix Delegation on `vlan0` when active, with the cloned DUID:

```conf
# Use the hardware address of the interface for the Client ID.
#clientid
# or
# Use the same DUID + IAID as set in DHCPv6 for DHCPv4 ClientID as per RFC4361.
# Some non-RFC compliant DHCP servers do not reply with this set.
# In this case, comment out duid and enable clientid above.
duid 00:03:00:01:00:01:2e:78:05:ac

# Persist interface configuration when dhcpcd exits.
persistent

# vendorclassid is set to blank to avoid sending the default of
# dhcpcd-<version>:<os>:<machine>:<platform>
vendorclassid

# A list of options to request from the DHCP server.
option domain_name_servers, domain_name, domain_search
option static_routes, classless_static_routes
# Respect the network MTU. This is applied to DHCP routes.
option interface_mtu

# Request a hostname from the network
option host_name

# Most distributions have NTP support.
#option ntp_servers

# A ServerID is required by RFC2131.
require dhcp_server_identifier

# Generate SLAAC address using the Hardware Address of the interface
#slaac hwaddr
# OR generate Stable Private IPv6 Addresses based from the DUID
slaac private

# Add end1, end0, and VRRP interfaces to denyinterfaces
denyinterfaces end0 end1 vrrp.*

# WAN Interface configuration
interface vlan0
    ipv6rs
    ia_na 1
    ia_pd 1/::/56
```

---

## 6. Native nftables Firewall (`/etc/nftables.nft`)

```nft
#!/usr/sbin/nft -f

flush ruleset

table inet filter {
    chain input {
        type filter hook input priority filter; policy drop;

        # Established & Related traffic
        ct state established,related accept
        ct state invalid drop

        # Loopback
        iifname "lo" accept

        # VRRP Protocol (v4 & v6)
        ip protocol vrrp accept
        ip6 nexthdr 112 accept
        ip daddr 224.0.0.18 accept
        ip6 daddr ff02::12 accept

        # ICMP / ICMPv6 (Inbound to Router)
        ip protocol icmp accept
        ip6 nexthdr ipv6-icmp accept

        # Inbound Management & Local Services from LAN
        iifname { "end1", "vrrp.52", "vrrp.53" } tcp dport 22 accept
        iifname { "end1", "vrrp.52", "vrrp.53" } udp dport { 53, 67, 547 } accept
        iifname { "end1", "vrrp.52", "vrrp.53" } tcp dport 53 accept

        # Allow DHCPv4 and DHCPv6 replies on WAN
        iifname "vlan0" udp sport 67 udp dport 68 accept
        iifname "vlan0" udp sport 547 udp dport 546 accept
    }

    chain forward {
        type filter hook forward priority filter; policy drop;

        # TCP MSS Clamping to prevent MTU blackholes
        tcp flags syn / syn,rst tcp option maxseg size set rt mtu

        # Established & Related forwarding (Both Directions)
        ct state established,related accept

        # Allow ICMP & ICMPv6 forwarding (Required for PMTU discovery)
        ip protocol icmp accept
        ip6 nexthdr ipv6-icmp accept

        # LAN to WAN forwarding (IPv4 & IPv6)
        iifname { "end1", "vrrp.52", "vrrp.53" } oifname "vlan0" accept
    }

    chain output {
        type filter hook output priority filter; policy accept;
    }
}

table ip nat {
    chain postrouting {
        type nat hook postrouting priority srcnat; policy accept;
        oifname "vlan0" masquerade
    }
}
```

Enable and start `nftables` via OpenRC:

```bash
rc-update add nftables default
rc-service nftables start
```

---

## 7. Keepalived Standby Configuration (`/etc/keepalived/keepalived.conf`)

```vrrp
global_defs {
    router_id FW_RISCV64
    vrrp_version 3
    enable_script_security
    script_user root
    vrrp_garp_master_refresh 15
}

# Synchronize IPv4 and IPv6 state transitions together
vrrp_sync_group VG_LAN {
    group {
        VI_LAN_IPV4
        VI_LAN_IPV6
    }
    notify "/root/notify-keepalived.sh"
    notify_stop "/root/notify-keepalived.sh GROUP VG_LAN STOP"
}

# 1. LAN IPv4 Instance
vrrp_instance VI_LAN_IPV4 {
    state BACKUP
    interface end1
    virtual_router_id 52
    priority 120
    advert_int 1
    use_vmac vrrp.52

    virtual_ipaddress {
        192.168.51.254/24 dev end1
    }
}

# 2. LAN IPv6 Instance
vrrp_instance VI_LAN_IPV6 {
    state BACKUP
    interface end1
    virtual_router_id 53
    priority 120
    advert_int 1
    use_vmac vrrp.53
    native_ipv6

    virtual_ipaddress {
        fe80::1:254/64 dev end1
    }
}
```

---

## 8. Router Advertisement Daemon (`/etc/radvd.conf`)

```conf
interface vrrp.53
{
    AdvSendAdvert on;
    MinRtrAdvInterval 3;
    MaxRtrAdvInterval 10;
    AdvDefaultPreference high;
    AdvSourceLLAddress on;

   # Global IPv6 Prefix
    prefix 2600:6c60:6640:117::/64
    {
        AdvOnLink on;
        AdvAutonomous on;
        AdvRouterAddr on;
    };
};
```

---

## 9. OpenRC Transition Script (`/root/notify-keepalived.sh`)

Ensure `dnsmasq`, `radvd`, and `net.vlan0` are not in the default runlevel (`rc-update del dnsmasq default`, `rc-update del radvd default`).

```bash
#!/bin/bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

TYPE=$1
NAME=$2
STATE=$3

logger -t keepalived-script "Script triggered: TYPE=$TYPE NAME=$NAME STATE=$STATE"

case "$STATE" in
    "MASTER")
        logger -t keepalived-script "Transitioning to MASTER - Starting net.vlan0, dnsmasq, and radvd"
        rc-service net.vlan0 start 2>/dev/null || true
        rc-service dnsmasq restart 2>/dev/null || rc-service dnsmasq start 2>/dev/null || true
        rc-service radvd restart 2>/dev/null || rc-service radvd start 2>/dev/null || true
        ;;

    "BACKUP"|"FAULT"|"STOP")
        logger -t keepalived-script "Transitioning to $STATE - Tearing down net.vlan0, radvd, and dnsmasq"
        rc-service radvd stop 2>/dev/null || true
        rc-service dnsmasq stop 2>/dev/null || true
        rc-service net.vlan0 stop 2>/dev/null || true
        ip -4 addr flush dev vlan0 2>/dev/null || true
        ip -6 addr flush dev vlan0 scope global 2>/dev/null || true
        ;;

    *)
        logger -t keepalived-script "Unknown state '$STATE' received."
        exit 1
        ;;
esac
exit 0
```

Make executable:

```bash
chmod +x /root/notify-keepalived.sh
```

---

## 10. Startup and Verification

1. **Add Keepalived to Boot and Start:**

```bash
rc-update add keepalived default
rc-service keepalived start

```

2. **Verify Backup Status:**

```bash
tail -f /var/log/messages | grep -E "(Keepalived|keepalived-script)"

```

*Ensure the board initializes in `BACKUP` state without starting `vlan0`, `dnsmasq`, or `radvd`.*

3. **Simulate Failover:** Stop `keepalived` on `fw1`:

```bash
rc-service keepalived stop

```

*Verify that `rem64` executes the transition script, brings up `net.vlan0` via OpenRC, acquires a dynamic lease, runs `radvd`/`dnsmasq`, and forwards packets.*
