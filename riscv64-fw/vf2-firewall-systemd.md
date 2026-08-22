# VisionFive 2 (`riscv64`) Standby Firewall & HA Configuration (`vf2-firewall.md`)

#### The VisionFive2 HAS NO RTC

This document details the configuration required to deploy the VisionFive 2 (`riscv64`) board as the High Availability backup node (`rem64`) for `fw1` using `firewalld` (or native `nftables`), `NetworkManager`, `keepalived`, `radvd`, and `dnsmasq`.

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

## 2. Package Compilation & Installation

### 2.1. On the `amd64` Build Host (`pptgentoo`)

Cross-compile the required service daemons and firewall stack:

```bash
riscv64-unknown-linux-gnu-emerge -av sys-cluster/keepalived net-misc/radvd net-dns/dnsmasq
emaint binhost -f
```

### 2.2. On the VisionFive 2 (`riscv64`)

Sync metadata and install the pre-built binaries:

```bash
emaint binhost -s
emerge -avK sys-cluster/keepalived net-misc/radvd net-dns/dnsmasq
```
---

## 3. Kernel Sysctl & NetworkManager Setup

### 3.1. Sysctl Overrides (`/etc/sysctl.d/90-vrrp.conf`)

```ini
# Global Packet Forwarding
net.ipv4.ip_forward = 1
net.ipv6.conf.all.forwarding = 1
net.ipv6.conf.default.forwarding = 1

# Disable SLAAC/RA processing globally for managed firewall interfaces
net.ipv6.conf.all.accept_ra = 0
net.ipv6.conf.default.accept_ra = 0
net.ipv6.conf.all.autoconf = 0
net.ipv6.conf.default.autoconf = 0

# VRRP & Neighbor Discovery Optimizations
net.ipv6.conf.all.ndisc_notify = 1
net.ipv6.conf.default.ndisc_notify = 1

# Loose reverse path filtering across all dynamic interfaces
net.ipv4.conf.all.rp_filter = 2
net.ipv4.conf.default.rp_filter = 2

# Prevent base interface ARP responses for VMACs
net.ipv4.conf.all.arp_ignore = 1
net.ipv4.conf.default.arp_ignore = 1

```

Apply immediately:

```bash
sysctl --system
```

---

### 3.2. NetworkManager WAN Configuration

Configure `vlan0` over `end0`, disable autoconnect, and clone `fw1` credentials:

```bash
# 1. Create VLAN interface on parent end0
nmcli connection add type vlan con-name vlan0 ifname vlan0 dev end0 id 100

# 2. Prevent automatic startup on boot
nmcli connection modify vlan0 connection.autoconnect no

# 3. Clone fw1 MAC and Unified DUID for Spectrum BNG
nmcli connection modify vlan0 802-3-ethernet.cloned-mac-address "00:01:2e:78:05:ac"
nmcli connection modify vlan0 ipv6.dhcp-duid "00:03:00:01:00:01:2e:78:05:ac"

# 4. Request Prefix Delegation (/56 hint) and DHCP options
nmcli connection modify vlan0 ipv6.method auto
nmcli connection modify vlan0 ipv6.dhcp-pd-hint ::/56
nmcli connection modify vlan0 ipv6.dhcp-send-hostname yes
nmcli connection modify vlan0 ipv6.dhcp-timeout 2147483647

```
---

## 4. Firewall Setup (`firewalld`)

### 4.1. Disable IPv6 Reverse Path Filter

Edit `/etc/firewalld/firewalld.conf` on the VisionFive 2:

```ini
IPv6_rpfilter=no

```

### 4.2. Apply Zones and Forwarding Policies

```bash
# External Zone (vlan0)
firewall-cmd --zone=external --add-interface=vlan0 --permanent
firewall-cmd --zone=external --add-service=dhcpv6-client --permanent
firewall-cmd --zone=external --add-protocol=ipv6-icmp --permanent

# Internal Zone (end1 & Keepalived VMACs)
firewall-cmd --zone=nm-shared --add-interface=end1 --permanent
firewall-cmd --zone=nm-shared --add-interface=vrrp.52 --permanent
firewall-cmd --zone=nm-shared --add-interface=vrrp.53 --permanent
firewall-cmd --zone=nm-shared --add-service=dns --permanent
firewall-cmd --zone=nm-shared --add-service=dhcp --permanent
firewall-cmd --zone=nm-shared --add-service=ssh --permanent
firewall-cmd --zone=nm-shared --add-protocol=vrrp --permanent

# LAN to WAN Forwarding Policy
firewall-cmd --permanent --new-policy=lan-to-wan
firewall-cmd --permanent --policy=lan-to-wan --set-priority=-1
firewall-cmd --permanent --policy=lan-to-wan --set-target=ACCEPT
firewall-cmd --permanent --policy=lan-to-wan --add-ingress-zone=nm-shared
firewall-cmd --permanent --policy=lan-to-wan --add-egress-zone=external

# Start and reload
systemctl enable --now firewalld
firewall-cmd --reload

```
---

## 5. Keepalived Standby Configuration (`/etc/keepalived/keepalived.conf`)

Configure the node as a `BACKUP` instance with priority `100` targeting `end1`:

```vrrp
global_defs {
    router_id FW_RISCV64
    vrrp_version 3
    enable_script_security
    script_user root
}

# 1. LAN IPv4 Instance
vrrp_instance VI_LAN_IPV4 {
    state BACKUP
    interface end1              
    virtual_router_id 52
    priority 100
    advert_int 1
    use_vmac vrrp.52
    vmac_xmit_base

    virtual_ipaddress {
        192.168.51.254/24 dev end1
    }
}

# 2. LAN IPv6 Instance
vrrp_instance VI_LAN_IPV6 {
    state BACKUP
    interface end1              
    virtual_router_id 53
    priority 100
    advert_int 1
    use_vmac vrrp.53
    native_ipv6

    virtual_ipaddress {
        fe80::1:254/64 dev end1
    }

    notify "/root/notify-keepalived.sh"
    notify_stop "/root/notify-keepalived.sh INSTANCE VI_LAN STOP"
}

```
---

## 6. Router Advertisement Daemon (`/etc/radvd.conf`)

```conf
interface vrrp.53
{
    AdvSendAdvert on;
    MinRtrAdvInterval 3;
    MaxRtrAdvInterval 10;
    AdvDefaultPreference high;
    AdvSourceLLAddress on;

    # Global IPv6 Prefix
    prefix 2600:6c60:6600:500::/64
    {
        AdvOnLink on;
        AdvAutonomous on;
        AdvRouterAddr on;
    };
};

```
---

## 7. Transition Script (`/root/notify-keepalived.sh`)

```bash
#!/bin/bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

TYPE=$1
NAME=$2
STATE=$3

logger -t keepalived-script "Script triggered: TYPE=$TYPE NAME=$NAME STATE=$STATE"

case "$STATE" in
    "MASTER")
        logger -t keepalived-script "Transitioning to MASTER - Bringing up vlan0, dnsmasq, and radvd"
        nmcli connection up vlan0 2>/dev/null
        systemctl stop systemd-resolved.service 2>/dev/null || true
        systemctl start dnsmasq.service 2>/dev/null || true
        systemctl start radvd.service 2>/dev/null || true
        pkill -HUP dnsmasq 2>/dev/null || true
        ;;

    "BACKUP"|"FAULT"|"STOP")
        logger -t keepalived-script "Transitioning to $STATE - Tearing down radvd, dnsmasq, and vlan0"
        systemctl stop radvd.service 2>/dev/null || true
        nmcli connection down vlan0 2>/dev/null
        ip -4 addr flush dev vlan0
        ip -6 addr flush dev vlan0 scope global
        systemctl stop dnsmasq.service 2>/dev/null || true
        systemctl start systemd-resolved.service 2>/dev/null || true
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

## 8. Failover Verification

1. **Start Keepalived:**
```bash
systemctl enable --now keepalived
```

2. **Verify Backup State:**
```bash
journalctl -u keepalived -f
```
*Should confirm receipt of advertisements from `fw1` and enter `BACKUP`.*

3. **Simulate Failover:** Stop `keepalived` on `fw1`:
```bash
systemctl stop keepalived

```
4. **Confirm Active Takeover:** Ensure `rem64` transitions to `MASTER`, brings up `vlan0`, and starts routing traffic.
