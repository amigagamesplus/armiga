#!/bin/bash
# =============================================================================
# Armiga — post-image.sh
# Se ejecuta DESPUÉS de crear las imágenes del filesystem
# Argumento $1 = IMAGES_DIR (directorio con rootfs.ext4, etc.)
# Argumento $2 = BR2_EXTERNAL path (pasado vía POST_SCRIPT_ARGS)
# =============================================================================

set -euo pipefail

IMAGES_DIR="${BINARIES_DIR}"
BR2_EXTERNAL="${BR2_EXTERNAL_armiga_PATH:-${GITHUB_WORKSPACE}}"
BOARD_DIR="$BR2_EXTERNAL/board/armiga"

echo ">>> Armiga post-image.sh: IMAGES_DIR=$IMAGES_DIR"
echo ">>> Armiga post-image.sh: BOARD_DIR=$BOARD_DIR"

# --- Copiar bootloader binarios a images/ ------------------------------------
echo ">>> Copiando kernel Image..."
cp "$BOARD_DIR/bootloader/Image" "$IMAGES_DIR/Image"

echo ">>> Copiando DTB..."
cp "$BOARD_DIR/bootloader/dtb.img" "$IMAGES_DIR/dtb.img"

# --- Crear extlinux.conf -----------------------------------------------------
echo ">>> Generando extlinux.conf..."
cat > "$IMAGES_DIR/extlinux.conf" << 'EOF'
LABEL Armiga
  LINUX /Image
  FDT /dtb.img
  APPEND root=/dev/mmcblk0p2 rootfstype=ext4 rootwait rw console=ttyS0,115200 console=tty0 net.ifnames=0 loglevel=3
EOF

# --- Generar imagen SD con genimage ------------------------------------------
echo ">>> Ejecutando genimage..."

GENIMAGE_TMP="$IMAGES_DIR/genimage.tmp"
rm -rf "$GENIMAGE_TMP"
mkdir -p "$GENIMAGE_TMP"

genimage \
    --rootpath "$TARGET_DIR" \
    --tmppath "$GENIMAGE_TMP" \
    --inputpath "$IMAGES_DIR" \
    --outputpath "$IMAGES_DIR" \
    --config "$BOARD_DIR/genimage.cfg"

rm -rf "$GENIMAGE_TMP"

# --- Escribir U-Boot en raw (offset 8K = 16 sectores de 512B) ---------------
UBOOT_BIN="$BOARD_DIR/bootloader/u-boot.bin"
SDCARD_IMG="$IMAGES_DIR/sdcard.img"

if [ -f "$UBOOT_BIN" ]; then
    echo ">>> Escribiendo U-Boot en offset 8K..."
    dd if="$UBOOT_BIN" of="$SDCARD_IMG" bs=512 seek=16 conv=notrunc status=progress
else
    echo ">>> AVISO: No se encontró u-boot.bin, saltando escritura de U-Boot"
fi

echo ""
echo "========================================="
echo " Imagen generada: $SDCARD_IMG"
echo "========================================="
echo ""
echo " Para flashear:"
echo "   sudo dd if=$SDCARD_IMG of=/dev/sdX bs=4M status=progress conv=fsync"
echo "   sudo sgdisk -e /dev/sdX"
echo "   sync"
echo ""
echo ">>> Armiga post-image.sh: done"
