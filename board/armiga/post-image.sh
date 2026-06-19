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
  APPEND root=/dev/mmcblk0p2 rootfstype=ext4 rootwait ro console=ttyS0,115200 console=tty0 net.ifnames=0 loglevel=3
EOF

# --- Generar amiga_data.img (exFAT) ------------------------------------------
# genimage NO soporta exFAT nativamente (a diferencia de vfat/ext2/ext4),
# así que la generamos aquí a mano y genimage la trata como imagen
# pre-existente en genimage.cfg (partition amiga_data, image = "amiga_data.img").
echo ">>> Generando amiga_data.img (exFAT)..."

if ! command -v mkfs.exfat >/dev/null 2>&1; then
    echo "ERROR: mkfs.exfat no encontrado. Instalar 'exfatprogs' en el runner CI."
    exit 1
fi

AMIGA_DATA_IMG="$IMAGES_DIR/amiga_data.img"
AMIGA_DATA_SIZE_MB=256   # debe coincidir con el size en genimage.cfg

rm -f "$AMIGA_DATA_IMG"
# Crear fichero en blanco del tamaño deseado
dd if=/dev/zero of="$AMIGA_DATA_IMG" bs=1M count="$AMIGA_DATA_SIZE_MB" status=none

# Formatear como exFAT con etiqueta amiga_data
mkfs.exfat -L "amiga_data" "$AMIGA_DATA_IMG"

echo ">>> amiga_data.img generada (${AMIGA_DATA_SIZE_MB}M, exFAT)"

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
echo " Para flashear (preservando datos en amiga_data):"
echo "   sudo bash flash.sh /dev/sdX"
echo ""
echo ">>> Armiga post-image.sh: done"
