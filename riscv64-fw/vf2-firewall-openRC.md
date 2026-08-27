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
net.ipv6.conf.end1.accept_ra = 2
net.ipv6.conf.end1.autoconf = 1

# Force immediate Neighbor Discovery updates when VRRP transitions
net.ipv6.conf.all.ndisc_notify = 1
net.ipv6.conf.default.ndisc_notify = 1

# Loose Reverse Path Filtering for Keepalived / VMAC compatibility (IPv4)
net.ipv4.conf.all.rp_filter = 2
net.ipv4.conf.default.rp_filter = 2
net.ipv4.conf.end1.rp_filter = 2
net.ipv4.conf.vlan0.rp_filter = 2

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

---

## 5. DHCPCD Configuration for WAN (`/etc/dhcpcd.conf`)

Ensure `dhcpcd` manages DHCPv4 and Prefix Delegation on `vlan0` when active, with the cloned DUID:

```conf
# Restrict dhcpcd to only the interfaces it needs to manage
allowinterfaces vlan0 vrrp.53

# Do not solicit router advertisements on the LAN
noipv6rs vrrp.53
nodad vrrp.53

# Configure the WAN Interface
interface vlan0
  ipv6rs                 # Accept Router Advertisements from the ISP
  ia_na 1                # Request a normal global IPv6 address for the WAN interface
  ia_pd 2 vrrp.53/0      # Request a Prefix Delegation (/56) and assign Subnet 0 to vrrp.53
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
        #ip protocol icmp accept
        #ip6 nexthdr ipv6-icmp accept

	# 1. Essential ICMP/ICMPv6 forwarding (Strictly for network health, PMTU, etc.)
        ip protocol icmp icmp type { destination-unreachable, time-exceeded, parameter-problem } accept
        ip6 nexthdr ipv6-icmp icmpv6 type { destination-unreachable, packet-too-big, time-exceeded, parameter-problem } accept

        # 2. Allow incoming Pings to LAN Global IPv6 Addresses
        # --> COMMENT OUT the line below to block the outside world from pinging your network!
        iifname "vlan0" ip6 nexthdr ipv6-icmp icmpv6 type echo-request accept

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
    priority 205
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
    priority 205
    advert_int 1

    use_vmac vrrp.53
    native_ipv6

    virtual_ipaddress {
        fe80::1:254/64 dev end1 nodad
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

   # Global IPv6 Prefix "::/64" to dynamically copy the interface's prefix
    prefix ::/64
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

# 1. Prevent concurrent script execution using a file lock
LOCKFILE="/run/keepalived-notify.lock"
exec 200>"$LOCKFILE"
flock -n 200 || {
    logger -t keepalived-script "Another instance is running; exiting."
    exit 0
}

# Redirect all script command outputs and errors to a debug log
exec >> /tmp/notify-debug.log 2>&1
set -x

TYPE=$1
NAME=$2
STATE=$3

logger -t keepalived-script "Executing: TYPE=$TYPE NAME=$NAME STATE=$STATE"

case "$STATE" in
    "MASTER")
        logger -t keepalived-script "Handling MASTER state"

        # Terminate any lingering dhcpcd
        pkill -9 -x dhcpcd 2>/dev/null || killall -9 dhcpcd 2>/dev/null || true
        rm -f /run/dhcpcd/*.pid /var/run/dhcpcd/*.pid 2>/dev/null || true

        # Bring parent link UP
        ip link set dev end0 up

        # Create vlan0 with tag 100 and spoofed MAC if absent
        if ! ip link show dev vlan0 >/dev/null 2>&1; then
            ip link add link end0 name vlan0 address 00:01:2e:78:05:ac type vlan id 100
        fi
        ip link set dev vlan0 up

        # Sync OpenRC state
        rc-service net.vlan0 zap 2>/dev/null || true
        rc-service net.vlan0 start 2>/dev/null || true

        # Remove fallback routes
        ip -4 route del default via 192.168.51.254 dev end1 2>/dev/null || true
        ip -6 route del default via fe80::1:254 dev end1 2>/dev/null || true

        sleep 2

        # Start dedicated dhcpcd
        /sbin/dhcpcd -b -q --noarp vlan0

        # Start services
        rc-service dnsmasq restart 2>/dev/null || rc-service dnsmasq start 2>/dev/null || true
        rc-service radvd restart 2>/dev/null || rc-service radvd start 2>/dev/null || true
        ;;

    "BACKUP"|"FAULT"|"STOP")
        logger -t keepalived-script "Handling $STATE state - Destroying vlan0"

        # 1. Immediately flush addresses and delete kernel VLAN interface FIRST
        ip -4 addr flush dev vlan0 2>/dev/null || true
        ip -6 addr flush dev vlan0 scope global 2>/dev/null || true
        ip link set dev vlan0 down 2>/dev/null || true
        ip link delete dev vlan0 2>/dev/null || true

        # 2. Shut down physical parent device
        ip link set dev end0 down 2>/dev/null || true

        # 3. Force-kill all running network daemons
        killall -9 dhcpcd dnsmasq radvd 2>/dev/null || true
        rm -rf /run/dhcpcd /var/run/dhcpcd

        # 4. Clean and zap OpenRC states without waiting on processes
        rc-service net.vlan0 zap 2>/dev/null || true
        rc-service dnsmasq zap 2>/dev/null || true
        rc-service radvd zap 2>/dev/null || true

        # 5. Set fallback default routes
        ip -4 route replace default via 192.168.51.254 dev end1 metric 1024 2>/dev/null || true
        ip -6 route replace default via fe80::1:254 dev end1 metric 1024 2>/dev/null || true
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
