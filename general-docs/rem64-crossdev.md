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

Target compilation settings are managed inside `/usr/riscv64-unknown-linux-gnu/etc/portage/`.

### 2.1. Configure `make.conf`

Edit `/usr/riscv64-unknown-linux-gnu/etc/portage/make.conf`:

```bash
# Note: profile variables are set/overridden in profile/ files:
# etc/portage/profile/use.force (overrides kernel_* USE variables)
# etc/portage/profile/make.defaults (overrides ARCH, KERNEL, ELIBC variables)

CHOST=riscv64-unknown-linux-gnu
CBUILD=x86_64-pc-linux-gnu

ROOT=/usr/${CHOST}/

ACCEPT_KEYWORDS="${ARCH} ~${ARCH}"

USE="${ARCH} ssl openssl zlib -test -introspection"

CFLAGS="-O2 -pipe -fomit-frame-pointer"
CXXFLAGS="${CFLAGS}"

FEATURES="-collision-protect sandbox buildpkg noman noinfo nodoc"
# Be sure we dont overwrite pkgs from another repo..
PKGDIR=${ROOT}var/cache/binpkgs/
PORTAGE_TMPDIR=${ROOT}tmp/

# Python targets for sysroot
PYTHON_TARGETS="python3_14"
PYTHON_SINGLE_TARGET="python3_14"
```

---

### 2.2. Common Cross-Compilation Sysroot Workarounds

1. **`sys-apps/util-linux` (`su` / `pam` constraint):**
```bash
mkdir -p /usr/riscv64-unknown-linux-gnu/etc/portage/package.use
echo "sys-apps/util-linux -su" >> /usr/riscv64-unknown-linux-gnu/etc/portage/package.use/util-linux

```


2. **`net-misc/curl` (`quic` / `ssl` constraint):**
```bash
echo "net-misc/curl ssl openssl" >> /usr/riscv64-unknown-linux-gnu/etc/portage/package.use/curl

```


3. **Managing sysroot `etc-update` changes:**
When Portage writes autounmask config updates into the sysroot, merge them using:
```bash
ROOT="/usr/riscv64-unknown-linux-gnu" etc-update --automode -3

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

### 3.3. Export and sync the eix cache

#### 1. On the `amd64` Build Host

Create the compressed `.tar.bz2` archive containing the host's `portage.eix` and place it in the HTTP web root:

```bash
mkdir -p /tmp/eix-export
cp /var/cache/eix/portage.eix /tmp/eix-export/
tar -cjf /usr/riscv64-unknown-linux-gnu/var/cache/binpkgs/remote.tar.bz2 -C /tmp/eix-export portage.eix
rm -rf /tmp/eix-export
```

#### 2. On the VisionFive 2 (`pptgentoo`)

Use `eix-remote fetch` with the `-a` (address) flag to download the archive to `/var/cache/eix/remote.tar.bz2`, then add it:

```bash
# Fetch from your binhost HTTP server:
eix-remote -a http://192.168.51.17:8080/remote.tar.bz2 fetch

# Merge it into the local database:
eix-remote add
```

*(Alternatively, `eix-remote -a [http://192.168.51.17:8080/remote.tar.bz2](http://192.168.51.17:8080/remote.tar.bz2) update` runs both `fetch` and `add` in a single pass).*

#### 3. Searching the remote database

Once imported, use `-R` to search the remote repository entries:

```bash
eix -R <package-name>
```

---

## 4. VisionFive 2 Client Configuration

### 4.1. Configure Binary Repositories (`binrepos.conf`)

Configure Portage to query the official Gentoo binary repository first, falling back to your local `amd64` cross-build host overlay.

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

# Automatically pull binary packages
FEATURES="${FEATURES} getbinpkg"

USE="-introspection"
LC_MESSAGES=C.utf8
MAKEOPTS="-j4 -l4"

# Enforce binary-only installations (fails if no binary exists instead of compiling locally)
EMERGE_DEFAULT_OPTS="${EMERGE_DEFAULT_OPTS} --usepkgonly"

```

---

## 5. Cross-Compiling the VisionFive 2 Linux Kernel

You can cross-compile mainline/vendor kernels on the `amd64` host in minutes using your crossdev toolchain.

Mainline:
```bash
git clone https://github.com/torvalds/linux.git /usr/riscv64-unknown-linux-gnu/usr/src/linux-mainline
```

Vendor:
```bash
git clone https://github.com/starfive-tech/linux.git /usr/riscv64-unknown-linux-gnu/usr/src/linux-starfive
```

### 5.1. Configure Drivers (Built-in WireGuard & IPv6)

Inside the kernel source tree on the `amd64` host:

Mainline:
```bash
# Generate base config
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- defconfig
```

Vendor:
```bash
# Generate base config from vendor
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- visionfive_defconfig
```


```bash

# For libvirtd (Optional)
./scripts/config --enable CONFIG_VIRTIO
./scripts/config --enable CONFIG_VIRTIO_PCI
./scripts/config --enable CONFIG_VIRTIO_BLK
./scripts/config --enable CONFIG_VIRTIO_NET
./scripts/config --enable CONFIG_EXT4_FS
./scripts/config --enable CONFIG_SERIAL_8250
./scripts/config --enable CONFIG_SERIAL_8250_CONSOLE

# Enable IPv6
./scripts/config --enable CONFIG_IPV6
./scripts/config --enable CONFIG_IPV6_MULTIPLE_TABLES
./scripts/config --enable CONFIG_IPV6_SUBTREES
./scripts/config --enable CONFIG_IP6_NF_IPTABLES
./scripts/config --enable CONFIG_NF_TABLES_IPV6

# Core WireGuard driver
./scripts/config --enable CONFIG_NET_CORE
./scripts/config --enable CONFIG_INET
./scripts/config --enable CONFIG_NET
./scripts/config --enable CONFIG_WIREGUARD

# Cryptographic and hashing primitives required by WireGuard
./scripts/config --enable CONFIG_CRYPTO
./scripts/config --enable CONFIG_CRYPTO_LIB_CHACHA20POLY1305
./scripts/config --enable CONFIG_CRYPTO_LIB_POLY1305
./scripts/config --enable CONFIG_CRYPTO_LIB_CURVE25519
./scripts/config --enable CONFIG_CRYPTO_LIB_BLAKE2S

# Networking routing and tunneling dependencies
./scripts/config --enable CONFIG_NET_UDP_TUNNEL
./scripts/config --enable CONFIG_IP_ADVANCED_ROUTER
./scripts/config --enable CONFIG_IP_MULTIPLE_TABLES
./scripts/config --enable CONFIG_IPV6_MULTIPLE_TABLES

# Packet filtering and firewall markers (needed for fwmark / wg-quick routing)
./scripts/config --enable CONFIG_NETFILTER
./scripts/config --enable CONFIG_NETFILTER_ADVANCED
./scripts/config --enable CONFIG_NETFILTER_XT_MARK
./scripts/config --enable CONFIG_NETFILTER_XT_MATCH_MARK
./scripts/config --enable CONFIG_NF_TABLES
./scripts/config --enable CONFIG_NFT_COMPAT

# Core IPv6 networking
./scripts/config --enable CONFIG_IPV6
./scripts/config --enable CONFIG_IPV6_ROUTER_PREF
./scripts/config --enable CONFIG_IPV6_ROUTE_INFO
./scripts/config --enable CONFIG_IPV6_OPTIMISTIC_DAD

# IPv6 routing tables & policy routing (needed for wg-quick AllowedIPs / default routes)
./scripts/config --enable CONFIG_IPV6_MULTIPLE_TABLES
./scripts/config --enable CONFIG_IPV6_SUBTREES

# Netfilter / nftables IPv6 support (needed for fwmark & iptables/nftables rules)
./scripts/config --enable CONFIG_NF_TABLES_IPV6
./scripts/config --enable CONFIG_IP6_NF_IPTABLES
./scripts/config --enable CONFIG_IP6_NF_MATCH_AH
./scripts/config --enable CONFIG_IP6_NF_MATCH_EUI64
./scripts/config --enable CONFIG_IP6_NF_MATCH_FRAG
./scripts/config --enable CONFIG_IP6_NF_MATCH_OPTS
./scripts/config --enable CONFIG_IP6_NF_MATCH_HL
./scripts/config --enable CONFIG_IP6_NF_MATCH_IPV6HEADER
./scripts/config --enable CONFIG_IP6_NF_MATCH_MH
./scripts/config --enable CONFIG_IP6_NF_MATCH_RPFILTER
./scripts/config --enable CONFIG_IP6_NF_MATCH_RT
./scripts/config --enable CONFIG_IP6_NF_FILTER
./scripts/config --enable CONFIG_IP6_NF_MANGLE
```

```bash

# Resolve config dependencies
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- olddefconfig

```

---

### 5.2. Compile Kernel and Modules

```bash
# Build Kernel Image
make -j$(nproc) ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- Image

# Build Modules
make -j$(nproc) ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- modules

# Stage modules into a temporary directory for transfer
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     INSTALL_MOD_PATH=/tmp/vf2-modules \
     modules_install

```

---

### 5.3. Deploy to the Board

1. **Kernel Image:** Copy `arch/riscv/boot/Image` to `/boot/` on the VisionFive 2.

```bash
scp arch/riscv/boot/Image root@192.168.51.61:/boot/Image-7.2.0-rc7
```



2. **Kernel Modules:** Sync modules to `/lib/modules/`:
```bash
rsync -avz -e "ssh -p 22" /tmp/riscv-modules/lib/modules/ root@<VF2_IP:/lib/modules/
```

Ex:
```bash
rsync -avz -e "ssh -p 22" /tmp/riscv-modules/lib/modules/ root@192.168.51.61:/lib/modules/
```


3. **Update dependencies on the VF2:**
```bash
depmod -a
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
