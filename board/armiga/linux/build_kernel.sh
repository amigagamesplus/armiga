#!/bin/bash
# =============================================================================
# Script de ayuda para compilar el kernel localmente
#
# Este script descarga el código fuente de Linux 7.0.2, aplica los parches
# de board/armiga/linux/patches/, usa la configuración linux.aarch64.conf
# y compila el binario Image y el dtb.
# =============================================================================

set -e

KERNEL_VERSION="7.0.14"
LINUX_DIR="linux-${KERNEL_VERSION}"
BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"
BOOTLOADER_DIR="$(dirname "$BOARD_DIR")/bootloader"

echo "=== Preparando compilación del kernel ==="

if [ ! -d "$LINUX_DIR" ]; then
    echo "Descargando Linux ${KERNEL_VERSION}..."
    wget -q https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_VERSION}.tar.xz
    tar xf linux-${KERNEL_VERSION}.tar.xz
else
    echo "Directorio $LINUX_DIR ya existe. Usando código existente..."
fi

cd "$LINUX_DIR"

echo "=== Limpiando árbol de código ==="
make mrproper

echo "=== Aplicando parches ==="
if [ -d "$BOARD_DIR/patches" ]; then
    for patch in "$BOARD_DIR"/patches/*.patch; do
        if [ -f "$patch" ]; then
            echo "Aplicando $patch..."
            patch -p1 < "$patch"
        fi
    done
else
    echo "No hay parches para aplicar."
fi

echo "=== Copiando DTS personalizados ==="
if [ -d "$BOARD_DIR/dts" ]; then
    for dts in "$BOARD_DIR"/dts/*.dts; do
        if [ -f "$dts" ]; then
            echo "Copiando $(basename "$dts")..."
            cp "$dts" arch/arm64/boot/dts/allwinner/
            
            # Extraemos el nombre del dtb y lo añadimos al Makefile para que compile
            filename=$(basename -- "$dts")
            dtb_name="${filename%.dts}.dtb"
            if ! grep -q "$dtb_name" arch/arm64/boot/dts/allwinner/Makefile; then
                echo "dtb-\$(CONFIG_ARCH_SUNXI) += $dtb_name" >> arch/arm64/boot/dts/allwinner/Makefile
            fi
        fi
    done
fi

echo "=== Adaptando configuración ==="
if [ -f "$BOARD_DIR/adapt_config.sh" ]; then
    # Ejecutamos el script desde su directorio para evitar problemas de rutas relativas
    (cd "$BOARD_DIR" && bash adapt_config.sh)
else
    echo "Advertencia: no se encontró adapt_config.sh en $BOARD_DIR"
fi

echo "=== Configurando kernel ==="
cp "$BOARD_DIR/linux.aarch64.conf" .config
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig

echo "=== Compilando kernel (Image), DTBs y Módulos ==="
# Asegúrate de tener instalado gcc-aarch64-linux-gnu
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image dtbs modules

echo "=== Instalando módulos temporalmente ==="
MODULES_DIR="$BOARD_DIR/modules_out"
mkdir -p "$MODULES_DIR"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$MODULES_DIR" modules_install
echo "Módulos listos en $MODULES_DIR (Cópialos después a la partición rootfs en /lib/modules/)"

echo "=== Copiando binarios al directorio bootloader ==="
mkdir -p "$BOOTLOADER_DIR"
cp arch/arm64/boot/Image "$BOOTLOADER_DIR/KERNEL"

# El dtb específico del H700 (RG40XX H)
DTB_FILE="arch/arm64/boot/dts/allwinner/sun50i-h700-anbernic-rg40xx-h.dtb"
if [ -f "$DTB_FILE" ]; then
    cp "$DTB_FILE" "$BOOTLOADER_DIR/dtb.img"
    echo "Kernel e Image DTB compilados y copiados correctamente a $BOOTLOADER_DIR."
else
    echo "ATENCIÓN: No se encontró el dtb esperado ($DTB_FILE). Asegúrate de tener los parches correctos."
fi

echo "¡Hecho!"

echo "=== Verificando configuración crítica ==="
for opt in CONFIG_RTW88_CORE CONFIG_RTW88_SDIO CONFIG_RTW88_8821C CONFIG_RTW88_8821CS; do
    val=$(grep "^${opt}=" .config 2>/dev/null | cut -d= -f2)
    if [ "$val" != "m" ]; then
        echo "ERROR: $opt debe ser =m pero es '=$val'"
        exit 1
    fi
done
echo "Configuración crítica OK"
