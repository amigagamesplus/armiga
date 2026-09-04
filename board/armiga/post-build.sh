#!/bin/bash
set -euo pipefail
TARGET_DIR="$1"
BR2_EXTERNAL="${2:-}"
echo ">>> armiga post-build.sh: TARGET_DIR=$TARGET_DIR"
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
KVER=$(basename "$(find "$TARGET_DIR/lib/modules" -maxdepth 1 -mindepth 1 -type d | head -1)")
if [ -z "$KVER" ]; then
    echo "ERROR: no kernel module directory found under $TARGET_DIR/lib/modules"
    exit 1
fi
depmod -a -b "$TARGET_DIR" "$KVER" || true
# --- armiga-release ----------------------------------------------------------
BUILD_DATE=$(TZ=Europe/Madrid date +"%d/%m/%Y %H:%M")
ARMIGA_VERSION=$(git -C "${1:-$(pwd)}" describe --tags --abbrev=0 2>/dev/null || echo "1.0")
BUILD_NUMBER="${GITHUB_RUN_NUMBER:-local}"
ARMIGA_EXTERNAL_DIR="${BR2_EXTERNAL_ARMIGA_PATH:-${1:-$(pwd)}}"
SDL3_VER=$(grep "^SDL3_VERSION" "$ARMIGA_EXTERNAL_DIR/package/sdl3/sdl3.mk" 2>/dev/null | cut -d= -f2 | tr -d ' ')
SDL3_TTF_VER=$(grep "^SDL3_TTF_VERSION" "$ARMIGA_EXTERNAL_DIR/package/sdl3_ttf/sdl3_ttf.mk" 2>/dev/null | cut -d= -f2 | tr -d ' ')
PUAE_SO="$TARGET_DIR/usr/lib/libretro/puae2021_libretro.so"
PUAE_VER=$(strings "$PUAE_SO" 2>/dev/null | grep -E "^2\.[0-9]+\.[0-9]+ [0-9a-f]{7}$" | head -1 | awk '{print $2}')
if [ -z "$PUAE_VER" ]; then
    echo "WARNING: could not extract PUAE2021 core version from $PUAE_SO, falling back to 6636d5f"
    PUAE_VER="6636d5f"
fi
RETROARCH_COMMIT_FILE="$TARGET_DIR/usr/bin/retroarch.commit"
if [ -f "$RETROARCH_COMMIT_FILE" ]; then
    RETROARCH_COMMIT=$(tr -d '[:space:]' < "$RETROARCH_COMMIT_FILE")
    RETROARCH_VER="1.22.2-nightly ($RETROARCH_COMMIT)"
else
    echo "WARNING: $RETROARCH_COMMIT_FILE not found, falling back to 1.22.2"
    RETROARCH_VER="1.22.2"
fi

cat > "$TARGET_DIR/etc/armiga-release" << RELEASE_EOF
ARMIGA_VERSION=$ARMIGA_VERSION
BUILD_NUMBER=$BUILD_NUMBER
KERNEL_VERSION=$KVER
MESA_VERSION=26.2.2
RETROARCH_VERSION=$RETROARCH_VER
SDL3_VERSION=$SDL3_VER
PUAE2021_CORE_VERSION=$PUAE_VER
SDL3_TTF_VERSION=$SDL3_TTF_VER
BUILD_DATE=$BUILD_DATE
RELEASE_EOF

# --- S43update --------------------------------------------------------------
mkdir -p "$TARGET_DIR/etc/init.d"
cp "$(dirname "$0")/rootfs_overlay/etc/init.d/S43update" "$TARGET_DIR/etc/init.d/S43update"
chmod +x "$TARGET_DIR/etc/init.d/S43update"

# --- S52samba ------------------------------------------------------------
cp "$(dirname "$0")/rootfs_overlay/etc/init.d/S52samba" "$TARGET_DIR/etc/init.d/S52samba"
chmod +x "$TARGET_DIR/etc/init.d/S52samba"

# --- S53samba-toggle -------------------------------------------------------
cp "$(dirname "$0")/rootfs_overlay/etc/init.d/S53samba-toggle" "$TARGET_DIR/etc/init.d/S53samba-toggle"
chmod +x "$TARGET_DIR/etc/init.d/S53samba-toggle"

# --- S05font -----------------------------------------------------------
cp "$(dirname "$0")/rootfs_overlay/etc/init.d/S05font" "$TARGET_DIR/etc/init.d/S05font"
chmod +x "$TARGET_DIR/etc/init.d/S05font"

echo ">>> armiga post-build.sh: done"

# --- OS name -----------------------------------------------------------------
mkdir -p "$TARGET_DIR/usr/lib"
cat > "$TARGET_DIR/usr/lib/os-release" << OS_EOF
NAME="armiga"
VERSION="$ARMIGA_VERSION"
ID=armiga
VERSION_ID=$ARMIGA_VERSION
PRETTY_NAME="armiga $ARMIGA_VERSION"
OS_EOF

# --- SSH authorized keys -----------------------------------------------------
mkdir -p "$TARGET_DIR/root/.ssh"
chmod 700 "$TARGET_DIR/root/.ssh"
if [ -f "$(dirname "$0")/../rootfs_overlay/root/.ssh/authorized_keys" ]; then
    cp "$(dirname "$0")/../rootfs_overlay/root/.ssh/authorized_keys" "$TARGET_DIR/root/.ssh/authorized_keys"
    chmod 600 "$TARGET_DIR/root/.ssh/authorized_keys"
fi
