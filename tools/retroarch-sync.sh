#!/bin/bash
# retroarch-sync.sh — descarga retroarch.cfg y PUAE 2021.opt del dispositivo,
# muestra diff contra el repo y opcionalmente los aplica (con bump de
# ARMIGA_CFG_VERSION automatico).
#
# Uso:
#   tools/retroarch-sync.sh root@10.250.109.130            # solo diff
#   tools/retroarch-sync.sh root@10.250.109.130 --apply    # diff + aplica

set -e
DEVICE="${1:?Uso: $0 root@IP [--apply]}"
APPLY="${2:-}"

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RA_CFG_REPO="$REPO_DIR/board/armiga/rootfs_overlay/etc/retroarch/retroarch.cfg.template"
PUAE_OPT_REPO="$REPO_DIR/board/armiga/rootfs_overlay/etc/retroarch/PUAE 2021.opt"

TMP_RA_CFG="/tmp/retroarch.cfg.device"
TMP_PUAE_OPT="/tmp/PUAE_2021.opt.device"

echo "Descargando ficheros de $DEVICE..."
ssh "$DEVICE" "cat /media/amiga_data/retroarch/retroarch.cfg" > "$TMP_RA_CFG"
ssh "$DEVICE" "cat '/media/amiga_data/retroarch/config/PUAE 2021/PUAE 2021.opt'" > "$TMP_PUAE_OPT"

sync_file() {
    local device_file="$1"
    local repo_file="$2"
    local label="$3"

    grep -v "^# ARMIGA_CFG_VERSION=" "$device_file" > "${device_file}.nv"
    grep -v "^# ARMIGA_CFG_VERSION=" "$repo_file" > "${repo_file}.nv.tmp"

    if diff -q "${repo_file}.nv.tmp" "${device_file}.nv" >/dev/null 2>&1; then
        echo "== $label: sin cambios =="
        rm -f "${device_file}.nv" "${repo_file}.nv.tmp"
        return 0
    fi

    echo "== $label: DIFF (repo -> dispositivo) =="
    diff -u "${repo_file}.nv.tmp" "${device_file}.nv" || true
    rm -f "${repo_file}.nv.tmp"

    if [ "$APPLY" = "--apply" ]; then
        CUR_VER=$(grep "^# ARMIGA_CFG_VERSION=" "$repo_file" | cut -d= -f2)
        CUR_VER=${CUR_VER:-0}
        NEW_VER=$((CUR_VER + 1))
        { echo "# ARMIGA_CFG_VERSION=$NEW_VER"; cat "${device_file}.nv"; } > "$repo_file"
        echo ">> $label actualizado. ARMIGA_CFG_VERSION: $CUR_VER -> $NEW_VER"
    fi
    rm -f "${device_file}.nv"
}

sync_file "$TMP_RA_CFG" "$RA_CFG_REPO" "retroarch.cfg"
sync_file "$TMP_PUAE_OPT" "$PUAE_OPT_REPO" "PUAE 2021.opt"

if [ "$APPLY" != "--apply" ]; then
    echo
    echo "Solo diff. Para aplicar: $0 $DEVICE --apply"
fi
