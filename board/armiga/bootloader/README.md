# Placeholder — Kernel compilado localmente
#
# Este directorio contiene los binarios precompilados del bootloader:
#
#   Image      — Kernel Linux (binario ARM64, de Rocknix 7.0.10)
#   dtb.img    — Device Tree Blob para Allwinner H700 / RG40XX H
#   u-boot.bin — U-Boot SPL + proper (de Rocknix, se escribe en raw offset 8K)
#
# Estos ficheros se obtienen extrayéndolos de una imagen Rocknix nightly:
#
#   wget https://github.com/ROCKNIX/distribution-nightly/releases/download/nightly-YYYYMMDD/ROCKNIX-H700.aarch64-YYYYMMDD.img.gz
#   gunzip ROCKNIX-H700.aarch64-YYYYMMDD.img.gz
#   sudo losetup -fP ROCKNIX-H700.aarch64-YYYYMMDD.img
#   sudo mount /dev/loop0p1 /mnt/tmp
#   cp /mnt/tmp/KERNEL ./Image
#   cp /mnt/tmp/dtb.img ./dtb.img
#
# Para U-Boot, extraer los primeros ~600KB de la imagen raw:
#   dd if=ROCKNIX-H700.aarch64-YYYYMMDD.img of=./u-boot.bin bs=512 skip=16 count=1200
