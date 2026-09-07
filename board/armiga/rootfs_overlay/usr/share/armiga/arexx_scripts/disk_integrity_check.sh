#!/bin/sh
# DESC_ES: Verifica integridad de discos/particiones y da un veredicto (OK/AVISO/CRITICO)
# DESC_EN: Checks disk/partition integrity and gives a verdict (OK/WARNING/CRITICAL)

WARN=0
CRIT=0

echo "========================================================"
echo "  Verificacion de Integridad de Discos"
echo "========================================================"

echo ""
echo ">> [1/5] Estado de montaje"
ROOT_MOUNT="$(mount | grep ' / ' | head -1)"
DATA_MOUNT="$(mount | grep ' /media/amiga_data ' | head -1)"
echo "   / : ${ROOT_MOUNT:-[NO ENCONTRADO]}"
echo "   /media/amiga_data : ${DATA_MOUNT:-[NO MONTADO]}"
if [ -z "$DATA_MOUNT" ]; then
    echo "   [CRITICO] La particion de datos no esta montada."
    CRIT=$((CRIT+1))
fi

echo ""
echo ">> [2/5] Prueba de escritura en /media/amiga_data"
TEST_FILE="/media/amiga_data/.armiga_disk_check_tmp"
if echo "test" > "$TEST_FILE" 2>/dev/null && [ -f "$TEST_FILE" ]; then
    rm -f "$TEST_FILE"
    echo "   [OK] Escritura correcta."
else
    echo "   [CRITICO] No se pudo escribir en la particion de datos."
    CRIT=$((CRIT+1))
fi

echo ""
echo ">> [3/5] Espacio libre"
ROOT_LINE="$(df -h / 2>/dev/null | awk 'NR==2{printf "%s | %s usados de %s (%s libres) | %s usado", $1, $3, $2, $4, $5}')"
DATA_LINE="$(df -h /media/amiga_data 2>/dev/null | awk 'NR==2{printf "%s | %s usados de %s (%s libres) | %s usado", $1, $3, $2, $4, $5}')"
DATA_PCT="$(df /media/amiga_data 2>/dev/null | awk 'NR==2{gsub("%","",$5); print $5}')"
echo "   Raiz (/):"
echo "     ${ROOT_LINE:-[NO DISPONIBLE]}"
echo "   Datos (/media/amiga_data):"
echo "     ${DATA_LINE:-[NO DISPONIBLE]}"
if [ -n "$DATA_PCT" ] && [ "$DATA_PCT" -ge 95 ] 2>/dev/null; then
    echo "   [AVISO] Particion de datos casi llena."
    WARN=$((WARN+1))
fi

echo ""
echo ">> [4/5] Errores de E/S recientes (dmesg)"
IO_ERRORS="$(dmesg 2>/dev/null | grep -iE 'i/o error|ext4-fs error|fat-fs.*error|blk_update_request|remounting filesystem read-only' | tail -n 10)"
if [ -n "$IO_ERRORS" ]; then
    echo "$IO_ERRORS"
    echo "   [CRITICO] Se han detectado errores de E/S en el kernel."
    CRIT=$((CRIT+1))
else
    echo "   [OK] Sin errores de E/S en dmesg."
fi

echo ""
echo ">> [5/5] Ficheros de configuracion criticos"
for f in /media/amiga_data/armiga.cfg /media/amiga_data/retroarch/retroarch.cfg; do
    if [ -f "$f" ]; then
        echo "   [OK] $f"
    else
        echo "   [AVISO] No existe: $f"
        WARN=$((WARN+1))
    fi
done

echo ""
echo "========================================================"
if [ "$CRIT" -gt 0 ]; then
    echo "  VEREDICTO: [CRITICO] $CRIT problema(s) grave(s) detectado(s)"
elif [ "$WARN" -gt 0 ]; then
    echo "  VEREDICTO: [AVISO] $WARN aviso(s), sin problemas graves"
else
    echo "  VEREDICTO: [OK] Todo correcto"
fi
echo "========================================================"
