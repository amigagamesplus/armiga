#!/bin/bash
set -euo pipefail
TARGET_DIR="$1"
BR2_EXTERNAL="${2:-}"
echo ">>> Armiga post-build.sh: TARGET_DIR=$TARGET_DIR"
echo "armiga" > "$TARGET_DIR/etc/hostname"
if ! grep -q "^ttyS0::" "$TARGET_DIR/etc/inittab" 2>/dev/null; then
    echo "ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100" >> "$TARGET_DIR/etc/inittab"
fi
mkdir -p "$TARGET_DIR/usr/share/keymaps"
mkdir -p "$TARGET_DIR/lib/firmware/rtl_bt"
mkdir -p "$TARGET_DIR/lib/firmware/rtlwifi"
mkdir -p "$TARGET_DIR/lib/firmware/panels"
mkdir -p "$TARGET_DIR/usr/share/udhcpc"
if [ ! -f "$TARGET_DIR/usr/share/udhcpc/default.script" ]; then
    cat > "$TARGET_DIR/usr/share/udhcpc/default.script" << 'UDHCPC_EOF'
#!/bin/sh
case "$1" in
    deconfig)
        ip addr flush dev "$interface"
        ip link set "$interface" up
        ;;
    renew|bound)
        ip addr add "$ip/$mask" dev "$interface"
        if [ -n "${router:-}" ]; then
            while ip route del default 2>/dev/null; do :; done
            for gw in $router; do
                ip route add default via "$gw" dev "$interface"
            done
        fi
        if [ -n "${dns:-}" ]; then
            echo -n > /etc/resolv.conf
            for ns in $dns; do
                echo "nameserver $ns" >> /etc/resolv.conf
            done
        fi
        if [ -n "${domain:-}" ]; then
            echo "search $domain" >> /etc/resolv.conf
        fi
        ;;
esac
UDHCPC_EOF
    chmod +x "$TARGET_DIR/usr/share/udhcpc/default.script"
fi
KVER="7.0.11-armiga"
depmod -a -b "$TARGET_DIR" "$KVER" || true
# --- armiga-release ----------------------------------------------------------
BUILD_DATE=$(date -u +"%Y-%m-%d %H:%M")
ARMIGA_VERSION=$(git -C "${1:-$(pwd)}" describe --tags --abbrev=0 2>/dev/null || echo "1.0")
BUILD_NUMBER="${GITHUB_RUN_NUMBER:-local}"
cat > "$TARGET_DIR/etc/armiga-release" << RELEASE_EOF
ARMIGA_VERSION=$ARMIGA_VERSION
BUILD_NUMBER=$BUILD_NUMBER
KERNEL_VERSION=7.0.11-armiga
MESA_VERSION=26.1.4
RETROARCH_VERSION=1.22.2
SDL3_VERSION=3.4.12
SDL3_TTF_VERSION=3.2.2
BUILD_DATE=$BUILD_DATE
RELEASE_EOF

# --- S43update --------------------------------------------------------------
mkdir -p "$TARGET_DIR/etc/init.d"
cp "$(dirname "$0")/rootfs_overlay/etc/init.d/S43update" "$TARGET_DIR/etc/init.d/S43update"
chmod +x "$TARGET_DIR/etc/init.d/S43update"

echo ">>> Armiga post-build.sh: done"

# --- OS name -----------------------------------------------------------------
mkdir -p "$TARGET_DIR/usr/lib"
cat > "$TARGET_DIR/usr/lib/os-release" << 'OS_EOF'
NAME="armiga"
VERSION="1.0"
ID=armiga
VERSION_ID=1.0
PRETTY_NAME="armiga 1.0"
OS_EOF

# --- SSH authorized keys -----------------------------------------------------
mkdir -p "$TARGET_DIR/root/.ssh"
chmod 700 "$TARGET_DIR/root/.ssh"
if [ -f "$(dirname "$0")/../rootfs_overlay/root/.ssh/authorized_keys" ]; then
    cp "$(dirname "$0")/../rootfs_overlay/root/.ssh/authorized_keys" "$TARGET_DIR/root/.ssh/authorized_keys"
    chmod 600 "$TARGET_DIR/root/.ssh/authorized_keys"
fi
