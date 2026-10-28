#!/bin/sh
# DESC_ES: Verifica integridad de discos/particiones y da un veredicto (OK/AVISO/CRITICO)
# DESC_EN: Checks disk/partition integrity and gives a verdict (OK/WARNING/CRITICAL)

DATA_DIR="${DATA_DIR:-/media/amiga_data}"
WARN=0
CRIT=0

echo "========================================"
echo "  Verificacion de Integridad de Discos"
echo "========================================"

echo ""
echo ">> [1/5] Estado de montaje"
ROOT_MOUNT="$(mount | grep ' / ' | head -1)"
DATA_MOUNT="$(mount | grep " $DATA_DIR " | head -1)"
ROOT_FS="$(mount | awk '$3=="/"{print $5; exit}')"
echo "   / : ${ROOT_MOUNT:-[NO ENCONTRADO]}"
echo "   $DATA_DIR : ${DATA_MOUNT:-[NO MONTADO]}"
SD_MOUNTS="$(mount | grep -E '^/dev/mmcblk')"
if [ -n "$SD_MOUNTS" ]; then
    echo "   Particiones de la SD montadas:"
    echo "$SD_MOUNTS" | awk '{print "     " $1 " en " $3 " (" $5 ")"}'
fi
if [ -z "$DATA_MOUNT" ]; then
    echo "   [CRITICO] La particion de datos no esta montada."
    CRIT=$((CRIT+1))
fi

echo ""
echo ">> [2/5] Prueba de escritura y lectura"
TEST_FILE="$DATA_DIR/.armiga_disk_check_tmp"
TOKEN="armiga-check-$$"
if echo "$TOKEN" > "$TEST_FILE" 2>/dev/null && sync && [ "$(cat "$TEST_FILE" 2>/dev/null)" = "$TOKEN" ]; then
    rm -f "$TEST_FILE"
    sync
    echo "   [OK] Escritura y lectura correctas."
else
    rm -f "$TEST_FILE" 2>/dev/null
    echo "   [CRITICO] No se pudo escribir o leer de vuelta en la particion de datos."
    CRIT=$((CRIT+1))
fi

echo ""
echo ">> [3/5] Espacio libre"
ROOT_LINE="$(df -h / 2>/dev/null | awk 'NR==2{printf "%s | %s usados de %s (%s libres) | %s usado", $1, $3, $2, $4, $5}')"
DATA_LINE="$(df -h "$DATA_DIR" 2>/dev/null | awk 'NR==2{printf "%s | %s usados de %s (%s libres) | %s usado", $1, $3, $2, $4, $5}')"
DATA_PCT="$(df "$DATA_DIR" 2>/dev/null | awk 'NR==2{gsub("%","",$5); print $5}')"
echo "   Raiz (/):"
echo "     ${ROOT_LINE:-[NO DISPONIBLE]}"
if [ "$ROOT_FS" = "squashfs" ]; then
    echo "     (squashfs de solo lectura: el 100% de uso es normal)"
fi
echo "   Datos ($DATA_DIR):"
echo "     ${DATA_LINE:-[NO DISPONIBLE]}"
if [ -n "$DATA_PCT" ] && [ "$DATA_PCT" -ge 95 ] 2>/dev/null; then
    echo "   [AVISO] Particion de datos casi llena."
    WARN=$((WARN+1))
fi

echo ""
echo ">> [4/5] Errores del sistema de ficheros y de la SD (dmesg)"
IO_ERRORS="$(dmesg 2>/dev/null | grep -iE 'i/o error|ext4-fs error|exfat.*error|fat-fs.*error|squashfs.*error|blk_update_request|remounting filesystem read-only' | tail -n 10)"
if [ -n "$IO_ERRORS" ]; then
    echo "$IO_ERRORS"
    echo "   [CRITICO] Se han detectado errores de E/S en el kernel."
    CRIT=$((CRIT+1))
else
    echo "   [OK] Sin errores de E/S en dmesg."
fi
MMC_ERRORS="$(dmesg 2>/dev/null | grep -iE 'mmc[0-9a-z]*(p[0-9]+)?:.*(error|timeout|crc)' | tail -n 5)"
if [ -n "$MMC_ERRORS" ]; then
    echo "$MMC_ERRORS"
    echo "   [AVISO] Errores en el controlador mmc (pueden ser de la SD u otro dispositivo SDIO)."
    WARN=$((WARN+1))
else
    echo "   [OK] Sin errores del controlador mmc."
fi

echo ""
echo ">> [5/5] Ficheros de configuracion criticos"
for f in "$DATA_DIR/armiga.cfg" "$DATA_DIR/retroarch/retroarch.cfg"; do
    if [ -f "$f" ]; then
        echo "   [OK] $f"
    else
        echo "   [AVISO] No existe: $f"
        WARN=$((WARN+1))
    fi
done

echo ""
echo "========================================"
if [ "$CRIT" -gt 0 ]; then
    echo "  VEREDICTO: [CRITICO] $CRIT problema(s) grave(s) detectado(s)"
elif [ "$WARN" -gt 0 ]; then
    echo "  VEREDICTO: [AVISO] $WARN aviso(s), sin problemas graves"
else
    echo "  VEREDICTO: [OK] Todo correcto"
fi
echo "========================================"
