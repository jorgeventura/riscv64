# Dual-Stack HA Firewall System Documentation (Keepalived, Spectrum Fiber, Gentoo Linux)

## Executive Summary

This document details the validated production configuration for a 4-node High Availability (HA) dual-stack (IPv4/IPv6) firewall array behind a Spectrum Fiber ONU (SONUV1H).
**Key Architectural Principle:** To avoid Layer-2 MAC flapping on the ISP switch, Keepalived *only* monitors the internal LAN (`eth1`). The WAN interface (`vlan0`) is actively toggled UP/DOWN by the transition script.

The architecture uses:
* **Keepalived VRRPv3** on the LAN with Virtual MACs (`use_vmac` + `vmac_xmit_base`) for flawless physical switch compatibility.
* **NetworkManager** with MAC cloning and matching DUIDs to ensure Spectrum grants the same IPv4/IPv6 address and Prefix Delegation (`IA_PD`) regardless of which node is active.
* **radvd (Router Advertisement Daemon)** running exclusively on `vrrp.53` during `MASTER` state to reliably serve the `fe80::1:254` floating gateway and IPv6 prefix advertisements to clients.
* **dnsmasq** bound dynamically for IPv4 DHCP and local DNS resolution.
* **firewalld** active continuously with `IPv6_rpfilter=no` to permit asymmetric routing across VMAC interfaces and ingress exceptions for VRRP.

---

## Technical Architecture Overview

```text
                          +-----------------------+
                          |   Spectrum SONUV1H    |
                          |      Fiber ONU        |
                          +-----------+-----------+
                                      | (VLAN 100 Trunk)
                                      |
                     +----------------+----------------+
                     |  Switch / VLAN Trunk (vlan0)    |
                     +---+---------+---------+---------+
                         |         |         |         |
                     +---+---+ +---+---+ +---+---+ +---+---+
                     |  fw1  | |  fw2  | |  fw3  | |  fw4  |
                     +---+---+ +---+---+ +---+---+ +---+---+
                         |         |         |         |
                     +---+---------+---------+---------+
                     |       Internal LAN (eth1)       |
                     |  IPv4 VIP: 192.168.51.254/24    |
                     |  IPv6 VIP: fe80::1:254/64       |
                     +---------------------------------+
```[cite: 2]

### Network Interfaces & Virtual MAC Matrix

| Interface Role | Physical Interface | VRID | Virtual MAC | Virtual IPv4 VIP | Virtual IPv6 Link-Local VIP |
| --- | --- | --- | --- | --- | --- |
| **WAN (ISP)** | `vlan0` | N/A | `00:01:2e:78:05:ac` *(Cloned from fw1)* | *Dynamic Spectrum IP* | *Dynamic Spectrum IP* |
| **LAN IPv4 VIP** | `eth1` | `52` | `00:00:5e:00:01:34` | `192.168.51.254/24` | N/A |
| **LAN IPv6 VIP** | `eth1` | `53` | `00:00:5e:00:02:35` | N/A | `fe80::1:254/64` |

---

## Keepalived Configuration (`/etc/keepalived/keepalived.conf`)

### 1. Primary Node (`fw1`)
```vrrp
global_defs {
    router_id FW_HA
    vrrp_version 3
    enable_script_security
    script_user root
}

# 1. LAN IPv4 Instance
vrrp_instance VI_LAN_IPV4 {
    state MASTER
    interface eth1              
    virtual_router_id 52
    priority 110
    advert_int 1
    use_vmac vrrp.52
    vmac_xmit_base

    virtual_ipaddress {
        192.168.51.254/24 dev eth1
    }
}

# 2. LAN IPv6 Instance
vrrp_instance VI_LAN_IPV6 {
    state MASTER
    interface eth1              
    virtual_router_id 53
    priority 110
    advert_int 1
    use_vmac vrrp.53
    native_ipv6

    virtual_ipaddress {
        # Link-local VIP MUST use /64 subnet mask for IPv6 routing
        fe80::1:254/64 dev eth1
    }

    notify "/root/notify-keepalived.sh"
    notify_stop "/root/notify-keepalived.sh INSTANCE VI_LAN STOP"
}

```

### 2. Standby Nodes (`fw2`, `fw3`, `fw4`)

```vrrp
global_defs {
    router_id FW_HA
    vrrp_version 3
    enable_script_security
    script_user root
}

# 1. LAN IPv4 Instance
vrrp_instance VI_LAN_IPV4 {
    state BACKUP
    interface eth1              
    virtual_router_id 52
    priority 100
    advert_int 1
    use_vmac vrrp.52
    vmac_xmit_base

    virtual_ipaddress {
        192.168.51.254/24 dev eth1
    }
}

# 2. LAN IPv6 Instance
vrrp_instance VI_LAN_IPV6 {
    state BACKUP
    interface eth1              
    virtual_router_id 53
    priority 100
    advert_int 1
    use_vmac vrrp.53
    native_ipv6

    virtual_ipaddress {
        fe80::1:254/64 dev eth1
    }

    notify "/root/notify-keepalived.sh"
    notify_stop "/root/notify-keepalived.sh INSTANCE VI_LAN STOP"
}

```

---

## Transition Controller Script (`/root/notify-keepalived.sh`)

*This script manages WAN activation and toggles `radvd` so that Router Advertisements are broadcast strictly by the active `MASTER` node on the `vrrp.53` VMAC interface.*

```bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

TYPE=$1
NAME=$2
STATE=$3

logger -t keepalived-script "Script triggered: TYPE=$TYPE NAME=$NAME STATE=$STATE"

case "$STATE" in
    "MASTER")
        logger -t keepalived-script "Transitioning to MASTER - Bringing up vlan0, dhcpcd, dnsmasq, and radvd"

        # 1. Bring up WAN interface via NetworkManager (for IPv4)
        nmcli connection up vlan0 2>/dev/null

        # 2. Manage local DNS/DHCP daemons
        systemctl stop systemd-resolved.service 2>/dev/null || true
        systemctl start dnsmasq.service 2>/dev/null || true
        systemctl start ddns-update.service 2>/dev/null || true
        systemctl start dnsmasq-ddns.service 2>/dev/null || true

        # 4. Start radvd to broadcast IPv6 RAs on vrrp.53
        systemctl start radvd.service 2>/dev/null || true
        pkill -HUP dnsmasq 2>/dev/null || true
        ;;

    "BACKUP"|"FAULT"|"STOP")
        logger -t keepalived-script "Transitioning to $STATE - Tearing down radvd, dhcpcd, dnsmasq, and vlan0"

        # 1. Stop radvd immediately so standby node sends zero RAs
        systemctl stop radvd.service 2>/dev/null || true

        # 2. Teardown WAN interface and flush residual IPs/routes
        nmcli connection down vlan0 2>/dev/null
        ip -4 addr flush dev vlan0
        ip -6 addr flush dev vlan0 scope global

        # 3. Stop local network services
        systemctl stop dnsmasq.service 2>/dev/null || true
        systemctl stop dnsmasq-ddns.service 2>/dev/null || true
        systemctl stop ddns-update.service 2>/dev/null || true
        systemctl start systemd-resolved.service 2>/dev/null || true
        ;;

    *)
        logger -t keepalived-script "Unknown state '$STATE' received."
        exit 1
        ;;
esac
exit 0
```

*Ensure executable permissions:* `chmod +x /root/notify-keepalived.sh`

---

## Router Advertisement Daemon Configuration (`/etc/radvd.conf`)

*`radvd` runs on both nodes, but is controlled via `notify-keepalived.sh`. It binds strictly to `vrrp.53`, advertising `fe80::1:254` as the default router.*

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

## NetworkManager WAN & DUID Configuration

To guarantee Spectrum assigns the exact same IPs and IPv6 Prefix Delegation during failover, standby nodes **must** clone `fw1`'s MAC address and unified DUID. Additionally, `vlan0` must **never** auto-connect on boot (the script manages it).

**Run on ALL nodes:**

```bash
# 1. Prevent vlan0 from connecting automatically before Keepalived is ready
nmcli connection modify vlan0 connection.autoconnect no

# 2. Hardcode unified DUID across fw1, fw2, fw3, and fw4
nmcli connection modify vlan0 ipv6.dhcp-duid "00:03:00:01:00:01:2e:78:05:ac"

# 3. Configure WAN to request Prefix Delegation (/56 hint)
nmcli connection modify vlan0 ipv6.method auto
nmcli connection modify vlan0 ipv6.dhcp-pd-hint ::/56
nmcli connection modify vlan0 ipv6.dhcp-send-hostname yes

# 4. Set LAN IPv6 to Manual (radvd exclusively handles IPv6 RAs)
# nmcli connection modify eth1 ipv6.method manual ipv6.addresses "2600:6c60:6600:500::1/64"

```

**Run on STANDBY nodes ONLY (`fw2`, `fw3`, `fw4`):**

```bash
# Clone fw1's physical MAC address to satisfy Spectrum's BNG security
nmcli connection modify vlan0 802-3-ethernet.cloned-mac-address "00:01:2e:78:05:ac"

```

---

## Kernel Sysctl Overrides (`/etc/sysctl.d/90-vrrp.conf`)

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
```

---

## Firewall Rules & Configuration (`firewalld`)

### 1. Reverse Path Filter Fix in `/etc/firewalld/firewalld.conf`

Because Linux lacks an `rp_filter` sysctl for IPv6, `firewalld` automatically inserts a `fib saddr . mark . iif check missing drop` rule into `nftables`. To prevent firewalld from dropping IPv6 transit traffic coming through `vrrp.53`, you **must** update `/etc/firewalld/firewalld.conf` on **all nodes**:

```ini
# /etc/firewalld/firewalld.conf
IPv6_rpfilter=no

```

### 2. Firewalld Zone Rules

```bash
# External Zone (vlan0)
firewall-cmd --zone=external --add-interface=vlan0 --permanent
firewall-cmd --zone=external --add-service=dhcpv6-client --permanent
firewall-cmd --zone=external --add-protocol=ipv6-icmp --permanent

# Internal Zone (eth1 & Keepalived VMACs)
firewall-cmd --zone=nm-shared --add-interface=eth1 --permanent
firewall-cmd --zone=nm-shared --add-interface=vrrp.52 --permanent
firewall-cmd --zone=nm-shared --add-interface=vrrp.53 --permanent
firewall-cmd --zone=nm-shared --add-service=dns --permanent
firewall-cmd --zone=nm-shared --add-service=dhcp --permanent
firewall-cmd --zone=nm-shared --add-protocol=vrrp --permanent

# Lan-to-wan Policy
firewall-cmd --permanent --new-policy=lan-to-wan
firewall-cmd --permanent --policy=lan-to-wan --set-priority=-1
firewall-cmd --permanent --policy=lan-to-wan --set-target=ACCEPT
# nm-shared
firewall-cmd --permanent --policy=lan-to-wan --add-ingress-zone=nm-shared
firewall-cmd --permanent --policy=lan-to-wan --add-egress-zone=external

firewall-cmd --reload

```

### 3. Enforce Static IPv6 on eth1

```bash
nmcli connection modify eth1 ipv6.method manual ipv6.addresses "2600:6c60:6640:117::1/64"
nmcli connection modify vlan0 ipv6.dhcp-timeout 2147483647
```
---

## `dnsmasq` Modular Configuration

### `/etc/dnsmasq.d/dns.conf`

```ini
interface=eth1
interface=vrrp.52
interface=vrrp.53
bind-dynamic
port=53
listen-address=127.0.0.1,::1,10.8.0.21,192.168.51.41,192.168.51.254,fe80::1:254

domain-needed
domain=jventura.us
bogus-priv
resolv-file=/etc/resolv.dnsmasq
cache-size=1024
no-negcache
neg-ttl=3600

addn-hosts=/etc/dnsmasq.d/custom_hosts
addn-hosts=/etc/dnsmasq.d/ddns_hosts

```

### `/etc/dnsmasq.d/dhcp.conf`

```ini
interface=eth1

# IPv4 DHCP
dhcp-option=tag:no-gateway,3
dhcp-option=tag:no-routes,121
dhcp-option=tag:no-routes,249
dhcp-range=192.168.51.64,192.168.51.240,12h
dhcp-option=option:router,192.168.51.254
dhcp-option=option:dns-server,192.168.51.254

```

---

## Operational Verification Checklist

1. **Verify Keepalived & Service Logs:**
```bash
journalctl -u keepalived -f
journalctl -u radvd -f
journalctl -t keepalived-script -f

```


*Watch for atomic state transitions and ensure the script logs "Bringing up vlan0, dnsmasq, and radvd".*
2. **Verify IPv6 Router Advertisements on Wire:**
```bash
tcpdump -i eth1 -n -vvv "icmp6 && ip6[40] == 134"

```


*Confirms RAs originate strictly from `fe80::1:254` with MAC `00:00:5e:00:02:35`.*
3. **Verify Client IPv6 Default Route:**
```bash
ip -6 route sh default

```


*On client nodes, confirms default route is set via `fe80::1:254` with high priority.*
4. **Verify Firewalld FIB Drop Removal:**
```bash
nft list ruleset | grep "fib saddr"

```


*Should return empty, confirming IPv6 transit packets will pass through `vrrp.53` without rejection.*
