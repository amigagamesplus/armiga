#!/bin/bash
# =============================================================================
# mkbootpkg.sh — Genera system.img: rootfs.squashfs + paquete de arranque
#
# La OTA copia la particion system COMPLETA (300 MiB) al slot inactivo, pero el
# rootfs squashfs solo ocupa ~58 MB. Detras del squashfs, a un offset fijo, se
# guarda un paquete (tar) con KERNEL, DTB y U-Boot. S01bootsync lo lee en el
# arranque y alinea p1 y el U-Boot con el, de modo que una OTA deje la consola
# identica a la release.
#
# Formato dentro de system.img:
#   0 ............ rootfs.squashfs
#   PKG_OFFSET ... cabecera de 512 bytes: "ARMIGAPKG1\nSIZE=<n>\nKERNEL_SIZE=<n>\nSHA256=<hex>\n" + ceros
#   +512 ......... tar (ustar) de SIZE bytes: KERNEL, dtb*.img (4), u-boot.bin
#
# Uso: mkbootpkg.sh IMAGES_DIR BOARD_DIR
# =============================================================================
set -euo pipefail

IMAGES_DIR="${1:?uso: mkbootpkg.sh IMAGES_DIR BOARD_DIR}"
BOARD_DIR="${2:?uso: mkbootpkg.sh IMAGES_DIR BOARD_DIR}"

PKG_OFFSET=$(( 128 * 1024 * 1024 ))
SLOT_SIZE=$(( 300 * 1024 * 1024 ))
SQUASHFS="${IMAGES_DIR}/rootfs.squashfs"
PKG_DIR="${IMAGES_DIR}/bootpkg"
PKG_TAR="${IMAGES_DIR}/bootpkg.tar"
PKG_HDR="${IMAGES_DIR}/bootpkg.hdr"
SYSTEM_IMG="${IMAGES_DIR}/system.img"

die () { echo "ERROR mkbootpkg: $*" >&2; exit 1; }

[ -f "${SQUASHFS}" ] || die "no existe ${SQUASHFS}"

rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}"
# Todos los miembros son obligatorios: S01bootsync los extrae por nombre
for f in KERNEL dtb.img dtb-rg35xx-h.img dtb-rg35xx-h-rev6.img dtb-rg40xx-h-v2.img; do
    [ -f "${IMAGES_DIR}/${f}" ] || die "falta ${IMAGES_DIR}/${f}"
    cp "${IMAGES_DIR}/${f}" "${PKG_DIR}/${f}"
done
[ -f "${BOARD_DIR}/bootloader/u-boot.bin" ] || die "falta ${BOARD_DIR}/bootloader/u-boot.bin"
cp "${BOARD_DIR}/bootloader/u-boot.bin" "${PKG_DIR}/u-boot.bin"

# Contenido determinista (orden, propietario y fecha fijos)
rm -f "${PKG_TAR}"
# shellcheck disable=SC2046
tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner --mtime='@0' \
    -C "${PKG_DIR}" -cf "${PKG_TAR}" $(cd "${PKG_DIR}" && LC_ALL=C ls)

SQ_SIZE=$(stat -c %s "${SQUASHFS}")
PKG_SIZE=$(stat -c %s "${PKG_TAR}")
KERNEL_SIZE=$(stat -c %s "${PKG_DIR}/KERNEL")
PKG_SHA=$(sha256sum "${PKG_TAR}" | cut -d' ' -f1)
[ "${SQ_SIZE}" -le "${PKG_OFFSET}" ] || die "rootfs.squashfs (${SQ_SIZE} B) no cabe antes del paquete (offset ${PKG_OFFSET})"
TOTAL=$(( PKG_OFFSET + 512 + PKG_SIZE ))
[ "${TOTAL}" -le "${SLOT_SIZE}" ] || die "el paquete no cabe en el slot (${TOTAL} > ${SLOT_SIZE})"

printf 'ARMIGAPKG1\nSIZE=%s\nKERNEL_SIZE=%s\nSHA256=%s\n' "${PKG_SIZE}" "${KERNEL_SIZE}" "${PKG_SHA}" > "${PKG_HDR}"
truncate -s 512 "${PKG_HDR}"

rm -f "${SYSTEM_IMG}"
dd if="${SQUASHFS}" of="${SYSTEM_IMG}" bs=1M status=none
truncate -s "${PKG_OFFSET}" "${SYSTEM_IMG}"
cat "${PKG_HDR}" "${PKG_TAR}" >> "${SYSTEM_IMG}"

# --- Autocomprobacion: releer system.img y comparar con lo que se ha generado ---
[ "$(stat -c %s "${SYSTEM_IMG}")" -eq "${TOTAL}" ] || die "tamano inesperado de system.img"
cmp -s -n "${SQ_SIZE}" "${SQUASHFS}" "${SYSTEM_IMG}" || die "el squashfs de system.img no coincide"
[ "$(dd if="${SYSTEM_IMG}" bs=512 skip=$(( PKG_OFFSET / 512 )) count=1 status=none | head -c 10)" = "ARMIGAPKG1" ] \
    || die "cabecera del paquete ilegible"
RE_SHA=$(dd if="${SYSTEM_IMG}" bs=4M iflag=skip_bytes,count_bytes skip=$(( PKG_OFFSET + 512 )) count="${PKG_SIZE}" status=none | sha256sum | cut -d' ' -f1)
[ "${RE_SHA}" = "${PKG_SHA}" ] || die "el sha256 del paquete releido no coincide"

echo ">>> bootpkg: $(ls "${PKG_DIR}" | tr '\n' ' ')"
echo ">>> bootpkg: squashfs ${SQ_SIZE} B, paquete ${PKG_SIZE} B, sha256 ${PKG_SHA}"
echo ">>> bootpkg: system.img ${TOTAL} B (slot ${SLOT_SIZE} B)"
rm -rf "${PKG_DIR}" "${PKG_TAR}" "${PKG_HDR}"
