#!/bin/bash
set -euo pipefail
TARGET_DIR="$1"
BR2_EXTERNAL="${2:-}"
echo ">>> Armiga post-build.sh: TARGET_DIR=$TARGET_DIR"
echo "armiga" > "$TARGET_DIR/etc/hostname"
if ! grep -q "^ttyS0::" "$TARGET_DIR/etc/inittab" 2>/dev/null; then
    echo "ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100" >> "$TARGET_DIR/etc/inittab"
fi
if ! grep -q "^tty0::" "$TARGET_DIR/etc/inittab" 2>/dev/null; then
    echo "tty0::respawn:/sbin/getty -L tty0 115200 vt100" >> "$TARGET_DIR/etc/inittab"
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
KVER="7.0.2-armiga"
depmod -a -b "$TARGET_DIR" "$KVER" || true
echo ">>> Armiga post-build.sh: done"

# --- OS name -----------------------------------------------------------------
cat > "$TARGET_DIR/etc/os-release" << 'OS_EOF'
NAME="armiga"
VERSION="1.0"
ID=armiga
VERSION_ID=1.0
PRETTY_NAME="armiga 1.0"
OS_EOF
ln -sf ../etc/os-release "$TARGET_DIR/usr/lib/os-release" 2>/dev/null || true
