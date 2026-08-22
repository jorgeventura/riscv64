# Gentoo Cross-Compilation & Binhost Guide for VisionFive 2 (StarFive JH7110)

This guide documents the complete procedure to cross-compile binary packages on a fast `amd64` host using `crossdev` and deploy them to a physical **StarFive VisionFive 2** (`riscv64` / `lp64d`) over a custom HTTP binhost overlay.

---

## 1. Toolchain Setup on the `amd64` Host

### 1.1. Prepare Portage Repositories

`crossdev` requires an overlay repository to store target-specific toolchain ebuilds.

1. Create `/etc/portage/repos.conf/crossdev.conf`:



```ini
[crossdev]
location = /var/db/repos/crossdev
priority = 1000
masters = gentoo
auto-sync = no

```

2. Initialize the repository structure:



```bash
mkdir -p /var/db/repos/crossdev/{profiles,metadata}
echo 'crossdev' > /var/db/repos/crossdev/profiles/repo_name
echo 'masters = gentoo' > /var/db/repos/crossdev/metadata/layout.conf
chown -R portage:portage /var/db/repos/crossdev

```

3. Install `crossdev`:



```bash
emerge -av sys-devel/crossdev

```

---

### 1.2. Generate the RISC-V Toolchain

Build the toolchain targeting the standard 64-bit RISC-V Linux ABI (`rv64gc` / `lp64d`):

```bash
crossdev -t riscv64-unknown-linux-gnu

```

This creates the toolchain binaries (`riscv64-unknown-linux-gnu-gcc`, `riscv64-unknown-linux-gnu-emerge`, etc.) and initializes the target sysroot at `/usr/riscv64-unknown-linux-gnu`.

---

## 2. Target Sysroot Configuration (`amd64` Host)

### 2.1. Configure Repository Pointer

Ensure the target sysroot references the host Gentoo repository so profile management and package metadata resolve accurately:

```bash
mkdir -p /usr/riscv64-unknown-linux-gnu/etc/portage/repos.conf
cat << 'EOF' > /usr/riscv64-unknown-linux-gnu/etc/portage/repos.conf/gentoo.conf
[DEFAULT]
main-repo = gentoo

[gentoo]
location = /var/db/repos/gentoo
sync-type = rsync
sync-uri = rsync://rsync.gentoo.org/gentoo-portage
auto-sync = no
EOF

```

---

### 2.2. Convert Sysroot to Merged-Usr Layout

By default, crossdev populates standard directories inside the sysroot (`/bin`, `/sbin`, `/lib`, `/lib64`). To align with Gentoo 23.0 merged-usr targets, migrate these paths into symlinks pointing to `/usr/*`:

```bash
SYSROOT="/usr/riscv64-unknown-linux-gnu"

# 1. Create usr destinations
mkdir -p "${SYSROOT}/usr/bin" "${SYSROOT}/usr/sbin" "${SYSROOT}/usr/lib" "${SYSROOT}/usr/lib64"

# 2. Migrate existing files and replace base directories with symlinks
for d in bin sbin lib lib64; do
    if [ -d "${SYSROOT}/${d}" ] && [ ! -L "${SYSROOT}/${d}" ]; then
        cp -a "${SYSROOT}/${d}/." "${SYSROOT}/usr/${d}/" 2>/dev/null || true
        rm -rf "${SYSROOT}/${d}"
        ln -s "usr/${d}" "${SYSROOT}/${d}"
    fi
done

```

Verify symlink convergence:

```bash
ls -ld /usr/riscv64-unknown-linux-gnu/{bin,sbin,lib,lib64}
```

---

### 2.3. Query, Select, and Update the Profile

Manage the target profile using `eselect` with `PORTAGE_CONFIGROOT` pointing to the target sysroot:

1. **List Available Target Profiles:**

```bash
PORTAGE_CONFIGROOT=/usr/riscv64-unknown-linux-gnu ROOT=/ eselect profile list
```

2. **Select the Systemd Target Profile:**
Set profile `8` (`default/linux/riscv/23.0/rv64/lp64d/systemd`):

```bash
PORTAGE_CONFIGROOT=/usr/riscv64-unknown-linux-gnu ROOT=/ eselect profile set default/linux/riscv/23.0/rv64/lp64d/systemd
```

3. **Verify the Selected Profile:**

```bash
PORTAGE_CONFIGROOT=/usr/riscv64-unknown-linux-gnu ROOT=/ eselect profile show
```

---

### 2.4. Configure Target `make.conf`

Edit `/usr/riscv64-unknown-linux-gnu/etc/portage/make.conf`:

```bash
CHOST=riscv64-unknown-linux-gnu
CBUILD=x86_64-pc-linux-gnu

ROOT=/usr/${CHOST}/

ACCEPT_KEYWORDS="${ARCH} ~${ARCH}"

USE="${ARCH} ssl openssl zlib systemd -test -introspection"

CFLAGS="-O2 -pipe -march=rv64gc -mabi=lp64d"
CXXFLAGS="${CFLAGS}"

FEATURES="-collision-protect sandbox buildpkg noman noinfo nodoc"
PKGDIR=${ROOT}var/cache/binpkgs/
PORTAGE_TMPDIR=${ROOT}tmp/

# Python targets for sysroot
PYTHON_TARGETS="python3_14"
PYTHON_SINGLE_TARGET="python3_14"

```

---

### 2.5. Common Package USE Flags & Config Updates

1. **Package USE Customizations:**

```bash
mkdir -p /usr/riscv64-unknown-linux-gnu/etc/portage/package.use
echo "sys-apps/util-linux -su" >> /usr/riscv64-unknown-linux-gnu/etc/portage/package.use/util-linux
echo "net-misc/curl ssl openssl" >> /usr/riscv64-unknown-linux-gnu/etc/portage/package.use/curl
echo "sys-apps/dbus systemd" >> /usr/riscv64-unknown-linux-gnu/etc/portage/package.use/dbus
echo "sys-cluster/keepalived systemd" >> /usr/riscv64-unknown-linux-gnu/etc/portage/package.use/keepalived

```

2. **Merge Sysroot Profile Configuration Updates:**

```bash
ROOT="/usr/riscv64-unknown-linux-gnu" etc-update --automode -3

```

3. **Update Base Target Sysroot:**

```bash
riscv64-unknown-linux-gnu-emerge -avuDN @world

```

---

## 3. Binhost Web Server Setup (`amd64` Host)

### 3.1. Configure Nginx

Install and serve the package directory:

```bash
emerge -av www-servers/nginx

```

Configure a server block in `/etc/nginx/nginx.conf` (or `/etc/nginx/conf.d/binhost.conf`):

```nginx
events {
    worker_connections 1024;
}

http {
    include       mime.types;
    default_type  application/octet-stream;
    sendfile      on;

    server {
        listen 8080;
        server_name 192.168.51.17;

        root /usr/riscv64-unknown-linux-gnu/var/cache/binpkgs;

        location / {
            autoindex on;
            autoindex_exact_size off;
            autoindex_localtime on;
        }
    }
}

```

Ensure read access and start the service:

```bash
chmod -R o+rX /usr/riscv64-unknown-linux-gnu/var/cache/binpkgs
rc-service nginx start
rc-update add nginx default

```

---

### 3.2. Generate/Update Package Index Metadata

Whenever binary packages are compiled on the host, update the index file so clients can fetch metadata:

```bash
emaint binhost -f

```

---

### 3.3. Export and Sync the Eix Cache

#### 1. On the `amd64` Build Host

```bash
mkdir -p /tmp/eix-export
cp /var/cache/eix/portage.eix /tmp/eix-export/
tar -cjf /usr/riscv64-unknown-linux-gnu/var/cache/binpkgs/remote.tar.bz2 -C /tmp/eix-export portage.eix
rm -rf /tmp/eix-export

```

#### 2. On the VisionFive 2 (`riscv64`)

```bash
eix-remote -a http://192.168.51.17:8080/remote.tar.bz2 fetch
eix-remote add

```

---

## 4. VisionFive 2 Client Configuration

### 4.1. Configure Binary Repositories (`binrepos.conf`)

Create `/etc/portage/binrepos.conf/gentoobinhost.conf`:

```ini
# Primary: Official Gentoo binary packages
[gentoobinhost]
priority = 100
sync-uri = https://distfiles.gentoo.org/releases/riscv/binpackages/23.0/rv64_lp64d

# Fallback Overlay: Custom AMD64 Crossdev Binhost
[amd64-binhost]
priority = 50
sync-uri = http://192.168.51.17:8080/

```

---

### 4.2. Configure `make.conf` on the VisionFive 2

Edit `/etc/portage/make.conf`:

```bash
COMMON_FLAGS="-O2 -pipe -march=rv64gc -mabi=lp64d"
CFLAGS="${COMMON_FLAGS}"
CXXFLAGS="${COMMON_FLAGS}"
FCFLAGS="${COMMON_FLAGS}"
FFLAGS="${COMMON_FLAGS}"

FEATURES="${FEATURES} getbinpkg"

USE="-introspection"
LC_MESSAGES=C.utf8
MAKEOPTS="-j4 -l4"

EMERGE_DEFAULT_OPTS="${EMERGE_DEFAULT_OPTS} --usepkgonly"

```

---

## 5. Cross-Compiling the VisionFive 2 Linux Kernel

You can cross-compile mainline/vendor kernels on the amd64 host in minutes using your crossdev toolchain.

### 5.1. Fetch Source & Set Kernel Configuration

#### 5.1.1. Option 1: From the mainline

```bash
git clone https://github.com/torvalds/linux.git /usr/riscv64-unknown-linux-gnu/usr/src/linux-mainline
```
Create the linux symlink.
```bash
ln -s /usr/riscv64-unknown-linux-gnu/usr/src/linux-mainline /usr/riscv64-unknown-linux-gnu/usr/src/linux
```
Configure the kernel.
```bash
cd /usr/riscv64-unknown-linux-gnu/usr/src/linux
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- defconfig
```

#### 5.1.2. Option 2: From the StarFive vendor

```bash
git clone https://github.com/starfive-tech/linux.git /usr/riscv64-unknown-linux-gnu/usr/src/linux-starfive
```

Create the linux symlink.
```bash
ln -s /usr/riscv64-unknown-linux-gnu/usr/src/linux-starfive /usr/riscv64-unknown-linux-gnu/usr/src/linux
```

```bash
cd /usr/riscv64-unknown-linux-gnu/usr/src/linux-starfive
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- visionfive_defconfig
```

### 5.1.3 Enable required subsystem, firewall, scheduling, and cgroup options

```bash
# VirtIO & Storage
./scripts/config --enable CONFIG_VIRTIO
./scripts/config --enable CONFIG_VIRTIO_PCI
./scripts/config --enable CONFIG_VIRTIO_BLK
./scripts/config --enable CONFIG_VIRTIO_NET
./scripts/config --enable CONFIG_EXT4_FS
./scripts/config --enable CONFIG_SERIAL_8250
./scripts/config --enable CONFIG_SERIAL_8250_CONSOLE

# Core WireGuard & Networking
./scripts/config --enable CONFIG_NET_CORE
./scripts/config --enable CONFIG_INET
./scripts/config --enable CONFIG_NET
./scripts/config --enable CONFIG_WIREGUARD
./scripts/config --enable CONFIG_NET_UDP_TUNNEL
./scripts/config --enable CONFIG_IP_ADVANCED_ROUTER
./scripts/config --enable CONFIG_IP_MULTIPLE_TABLES
./scripts/config --enable CONFIG_NETFILTER
./scripts/config --enable CONFIG_NETFILTER_ADVANCED
./scripts/config --enable CONFIG_NETFILTER_XT_MARK
./scripts/config --enable CONFIG_NETFILTER_XT_MATCH_MARK
./scripts/config --enable CONFIG_NF_TABLES
./scripts/config --enable CONFIG_NFT_COMPAT
./scripts/config --enable CONFIG_NF_CT_NETLINK

# WireGuard Crypto Primitives
./scripts/config --enable CONFIG_CRYPTO
./scripts/config --enable CONFIG_CRYPTO_LIB_CHACHA20POLY1305
./scripts/config --enable CONFIG_CRYPTO_LIB_POLY1305
./scripts/config --enable CONFIG_CRYPTO_LIB_CURVE25519
./scripts/config --enable CONFIG_CRYPTO_LIB_BLAKE2S

# Full IPv6 Subsystem & Netfilter
./scripts/config --enable CONFIG_IPV6
./scripts/config --enable CONFIG_IPV6_MULTIPLE_TABLES
./scripts/config --enable CONFIG_IPV6_SUBTREES
./scripts/config --enable CONFIG_IPV6_ROUTER_PREF
./scripts/config --enable CONFIG_IPV6_ROUTE_INFO
./scripts/config --enable CONFIG_IPV6_OPTIMISTIC_DAD
./scripts/config --enable CONFIG_NF_TABLES_IPV6
./scripts/config --enable CONFIG_IP6_NF_IPTABLES
./scripts/config --enable CONFIG_IP6_NF_FILTER
./scripts/config --enable CONFIG_IP6_NF_MANGLE

# Packet Scheduler & Debug
./scripts/config --enable CONFIG_MAGIC_SYSRQ
./scripts/config --enable CONFIG_NET_SCH_FQ_CODEL
./scripts/config --enable CONFIG_NET_SCHED
./scripts/config --set-str CONFIG_DEFAULT_NET_SCH "fq_codel"

# Unified Cgroups v2 & Systemd Monitoring
./scripts/config --enable CONFIG_CGROUPS
./scripts/config --enable CONFIG_INOTIFY_USER
./scripts/config --enable CONFIG_FANOTIFY
./scripts/config --enable CONFIG_MEMCG
./scripts/config --enable CONFIG_BLK_CGROUP
./scripts/config --enable CONFIG_CGROUP_SCHED
./scripts/config --enable CONFIG_FAIR_GROUP_SCHED
./scripts/config --enable CONFIG_CFS_BANDWIDTH
./scripts/config --enable CONFIG_CGROUP_PIDS
./scripts/config --enable CONFIG_CGROUP_FREEZER
./scripts/config --enable CONFIG_CGROUP_DEVICE
./scripts/config --enable CONFIG_CGROUP_CPUACCT
./scripts/config --enable CONFIG_CGROUP_PERF
./scripts/config --enable CONFIG_CGROUP_BPF
./scripts/config --enable CONFIG_SOCK_CGROUP_DATA
./scripts/config --enable CONFIG_PSI

# Enable I2C core & controller drivers built-in
./scripts/config --set-val CONFIG_I2C y
./scripts/config --set-val CONFIG_I2C_CHARDEV y
./scripts/config --set-val CONFIG_I2C_DESIGNWARE_CORE y
./scripts/config --set-val CONFIG_I2C_DESIGNWARE_PLATFORM y

# Interface required params
./scripts/config --set-val CONFIG_MOTORCOMM_PHY y
./scripts/config --set-val CONFIG_STMMAC_ETH y
./scripts/config --set-val CONFIG_STMMAC_PLATFORM y
./scripts/config --set-val CONFIG_DWMAC_GENERIC y
./scripts/config --set-val CONFIG_DWMAC_STARFIVE y
./scripts/config --set-val CONFIG_MACVLAN m
./scripts/config --set-val CONFIG_MACVTAP m

# Core Connection Tracking & Families
./scripts/config --enable CONFIG_NF_CONNTRACK
./scripts/config --enable CONFIG_NF_TABLES_INET
./scripts/config --enable CONFIG_NF_TABLES_IPV4
./scripts/config --enable CONFIG_NFT_CT

# NAT & Masquerade Engines (table ip nat & masquerade)
./scripts/config --enable CONFIG_NF_NAT
./scripts/config --enable CONFIG_NFT_NAT
./scripts/config --enable CONFIG_NFT_MASQ

# TCP Option Clamping (tcp option maxseg size set rt mtu)
./scripts/config --enable CONFIG_NFT_SYNPROXY
```

Resolve dependencies
```bash
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- olddefconfig
```

### 5.1.4 Apply Device Tree Fixes for VisionFive 2 GMAC0

Apply GMAC0 RGMII PHY clock timing fix
```bash
patch -p1 < /path/to/VisionFive2/0002-starfive-jh7110-gmac0-rgmii-phy-clock-fix.patch
```

---

### 5.2. Compile and Deploy Kernel


Build Kernel & Modules & Device tree blobs
```bash
make -j$(nproc) ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- Image modules dtbs
```
```bash
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- INSTALL_MOD_PATH=/tmp/riscv-modules modules_install
```
```bash
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- INSTALL_DTBS_PATH=/tmp/riscv-dtbs dtbs_install
```

```bash
KREL=$(make -s kernelrelease)
```
Copy Kernel Image (the name maybe different)
```bash
scp arch/riscv/boot/Image root@192.168.51.45:/boot/Image-${KREL}
scp System.map root@192.168.51.45:/boot/System.map-${KREL}
scp .config root@192.168.51.45:/boot/config-${KREL}
```

Update standard U-Boot symlink on target
```bash
ssh root@192.168.51.45 "ln -sf Image-${KREL} /boot/Image"
```

Copy Device Tree blobs
```bash
rsync -avz -e "ssh -p 22" /tmp/riscv-dtbs/starfive root@192.168.51.45:/boot/dtbs/starfive-${KREL}
```

Update standard U-Boot symlink on target for device tree
```bash
ssh root@192.168.51.45 "ln -sf starfive-${KREL} /boot/dtbs/starfive"
```

Copy Modules & Run depmod on Target
```bash
rsync -avz -e "ssh -p 22" /tmp/riscv-modules/lib/modules/ root@192.168.51.45:/lib/modules/
ssh root@192.168.51.45 "depmod -a"
```
---

## 6. Day-to-Day Build & Deployment Workflow

```text
[ amd64 Build Host ]                                [ VisionFive 2 ]
  │                                                   │
  ├─> riscv64-unknown-linux-gnu-emerge -av <pkg>      │
  │   (Compiles binary to PKGDIR)                     │
  │                                                   │
  ├─> emaint binhost -f                               │
  │   (Updates Packages index file)                   │
  │                                                   │
  │                                                   ├─> emaint binhost -s
  │                                                   │   (Syncs remote binhost metadata)
  │                                                   │
  │                                                   ├─> emerge -avK <pkg>
  │<───────────────── HTTP GET /pkg.gpkg.tar ─────────┤   (Installs pre-built binary)

```
