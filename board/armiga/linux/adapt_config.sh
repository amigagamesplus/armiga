#!/bin/bash
# =============================================================================
# Script para adaptar el .conf de Rocknix a las necesidades de armiga
# =============================================================================

CONF="linux.aarch64.conf"

if [ ! -f "$CONF" ]; then
    echo "Error: El archivo $CONF no existe en esta carpeta."
    echo "Por favor, pega el contenido del .conf de Rocknix en ese archivo primero."
    exit 1
fi

echo "Adaptando $CONF para armiga..."

# 1. Cambiar el hostname por defecto
sed -i 's/CONFIG_DEFAULT_HOSTNAME="@DEVICENAME@"/CONFIG_DEFAULT_HOSTNAME="armiga"/' "$CONF"

# 2. Deshabilitar el Initramfs de Rocknix
# Rocknix usa variables con @. Al dejarlo en blanco, el kernel intentará
# montar el rootfs (mmcblk0p2) directamente, lo cual funciona porque ext4 y MMC
# están integrados (built-in) en el kernel de Rocknix.
sed -i 's/CONFIG_INITRAMFS_SOURCE="@INITRAMFS_SOURCE@"/CONFIG_INITRAMFS_SOURCE=""/' "$CONF"

# 3. Firmware del panel embebido en el kernel
# El driver panel-mipi-dpi-spi lo necesita antes de que el rootfs esté montado
FIRMWARE_DIR="$(cd "$(dirname "$0")" && pwd)/../rootfs_overlay/lib/firmware"
sed -i "s|CONFIG_EXTRA_FIRMWARE=.*|CONFIG_EXTRA_FIRMWARE=\"panels/anbernic,rg40xx-panel.panel\"|" "$CONF"
sed -i "s|CONFIG_EXTRA_FIRMWARE_DIR=.*|CONFIG_EXTRA_FIRMWARE_DIR=\"${FIRMWARE_DIR}\"|" "$CONF"

# 4. Nombre personalizado del kernel
if grep -q "^CONFIG_LOCALVERSION=" "$CONF"; then
    sed -i 's/^CONFIG_LOCALVERSION=.*/CONFIG_LOCALVERSION="-armiga"/' "$CONF"
else
    echo 'CONFIG_LOCALVERSION="-armiga"' >> "$CONF"
fi

echo "¡Adaptación completada! El archivo $CONF ya está listo para compilar localmente sin dependencias de Rocknix."
