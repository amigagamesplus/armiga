#!/bin/bash
# =============================================================================
# Armiga — post-build.sh
# Se ejecuta DESPUÉS de construir el rootfs, ANTES de empaquetarlo
# Argumento $1 = TARGET_DIR (raíz del rootfs)
# Argumento $2 = BR2_EXTERNAL path (pasado vía POST_SCRIPT_ARGS)
# =============================================================================

set -euo pipefail

TARGET_DIR="$1"
BR2_EXTERNAL="${2:-}"

echo ">>> Armiga post-build.sh: TARGET_DIR=$TARGET_DIR"

# --- Hostname ----------------------------------------------------------------
echo "armiga" > "$TARGET_DIR/etc/hostname"

# --- Console serial ----------------------------------------------------------
# Asegurar que getty en ttyS0 está habilitado
if ! grep -q "^ttyS0::" "$TARGET_DIR/etc/inittab" 2>/dev/null; then
    echo "ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100" >> "$TARGET_DIR/etc/inittab"
fi

# --- Teclado español ---------------------------------------------------------
# Crear directorio para keymaps si no existe
mkdir -p "$TARGET_DIR/usr/share/keymaps"

# --- Dropbear SSH dirs -------------------------------------------------------

# --- WiFi firmware dirs (RTL8821CS) ------------------------------------------
mkdir -p "$TARGET_DIR/lib/firmware/rtl_bt"
mkdir -p "$TARGET_DIR/lib/firmware/rtlwifi"

# --- Panel firmware dir ------------------------------------------------------
mkdir -p "$TARGET_DIR/lib/firmware/panels"

# --- udhcpc default script ---------------------------------------------------
mkdir -p "$TARGET_DIR/usr/share/udhcpc"
if [ ! -f "$TARGET_DIR/usr/share/udhcpc/default.script" ]; then
    cat > "$TARGET_DIR/usr/share/udhcpc/default.script" << 'UDHCPC_EOF'
#!/bin/sh
# udhcpc script — configuración DHCP

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
        # DNS
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

# --- Timezone ----------------------------------------------------------------
ln -sf /usr/share/zoneinfo/Europe/Madrid "$TARGET_DIR/etc/localtime"

echo ">>> Armiga post-build.sh: done"

# --- Módulos WiFi RTL8821CS --------------------------------------------------
LINUX_DIR="$(dirname "$0")/linux/linux-7.0.2"
KVER="7.0.2-armiga"
RTW88_SRC="$LINUX_DIR/drivers/net/wireless/realtek/rtw88"
RTW88_DST="$TARGET_DIR/lib/modules/$KVER/kernel/drivers/net/wireless/realtek/rtw88"

if [ -d "$RTW88_SRC" ]; then
    mkdir -p "$RTW88_DST"
    for mod in rtw88_core rtw88_sdio rtw88_8821c rtw88_8821cs; do
        if [ -f "$RTW88_SRC/${mod}.ko" ]; then
            cp "$RTW88_SRC/${mod}.ko" "$RTW88_DST/"
            echo ">>> Copied ${mod}.ko"
        fi
    done
    # Generar modules.dep
    depmod -a -b "$TARGET_DIR" "$KVER" || true
else
    echo ">>> WARNING: rtw88 source dir not found, skipping modules"
fi
