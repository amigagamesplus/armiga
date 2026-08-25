#!/bin/sh
# DESC_ES: Renombra ROMs de Amiga a su nombre canonico (APLICA cambios reales)
# DESC_EN: Renames Amiga ROMs to their canonical name (APPLIES real changes)

ROMS_BASE="/media/amiga_data/roms"
DAT_FILE="$ROMS_BASE/Commodore - Amiga.dat"
DB_CACHE="/tmp/amiga_scraper_db.txt"
MODO_PRUEBA=0

SUBDIRS="adf ipf hdf whdload"

echo "========================================================"
echo "  Renombrado de ROMs Amiga (MODO REAL)"
echo "========================================================"

if [ ! -f "$DAT_FILE" ]; then
    echo "[ERROR] No se encuentra el .dat en:"
    echo "        $DAT_FILE"
    echo "Copia 'Commodore - Amiga.dat' a esa ruta y vuelve a ejecutar."
    exit 1
fi

if [ ! -f "$DB_CACHE" ]; then
    echo ">> [1/3] Indexando .dat con awk..."
    awk '
    /^[[:space:]]*name "/ {
        sub(/^[[:space:]]*name "/, "")
        sub(/".*$/, "")
        gname = $0
    }
    /^[[:space:]]*rom \(/ {
        line = $0
        sub(/^.*name "/, "", line)
        rname = line
        sub(/".*$/, "", rname)

        if (match($0, /crc [0-9A-Fa-f]+/)) {
            rcrc = substr($0, RSTART + 4, RLENGTH - 4)
        } else { rcrc = "" }

        if (match($0, /sha1 [0-9A-Fa-f]+/)) {
            rsha1 = substr($0, RSTART + 5, RLENGTH - 5)
        } else { rsha1 = "" }

        print rname "|" toupper(rcrc) "|" tolower(rsha1) "|" gname
    }
    ' "$DAT_FILE" > "$DB_CACHE"
    TOTAL_DB=$(wc -l < "$DB_CACHE")
    echo ">> [1/3] Base de datos indexada: $TOTAL_DB titulos."
else
    echo ">> [1/3] Usando indice en cache ($DB_CACHE)."
fi

TOTAL_PROCESADOS=0
TOTAL_RENOMBRADOS=0
TOTAL_DESCONOCIDOS=0

for SUB in $SUBDIRS; do
    DIR="$ROMS_BASE/$SUB"
    [ -d "$DIR" ] || continue
    if [ "$SUB" != "whdload" ]; then
        echo ">> [2/3] Omitido $DIR (el .dat no cubre este formato)"
        echo "--------------------------------------------------------"
        continue
    fi
    echo ">> [2/3] Analizando: $DIR"
    echo "--------------------------------------------------------"

    for archivo in "$DIR"/*; do
        [ -f "$archivo" ] || continue
        case "$archivo" in
            *.dat|*.DAT) continue ;;
        esac

        TOTAL_PROCESADOS=$((TOTAL_PROCESADOS + 1))
        NOMBRE_ACTUAL=$(basename "$archivo")
        EXT="${NOMBRE_ACTUAL##*.}"
        NOMBRE_CANONICO=""

        MATCH=$(grep -F -m 1 "$NOMBRE_ACTUAL|" "$DB_CACHE")

        if [ -n "$MATCH" ]; then
            NOMBRE_CANONICO=$(echo "$MATCH" | cut -d'|' -f4)
        else
            if command -v crc32 >/dev/null 2>&1; then
                CRC=$(crc32 "$archivo" 2>/dev/null | tr 'a-z' 'A-Z')
                [ -n "$CRC" ] && MATCH=$(grep "|${CRC}|" "$DB_CACHE" | head -n 1)
            fi
            if [ -z "$MATCH" ] && command -v sha1sum >/dev/null 2>&1; then
                SHA1=$(sha1sum "$archivo" | awk '{print $1}')
                MATCH=$(grep "|${SHA1}|" "$DB_CACHE" | head -n 1)
            fi
            if [ -n "$MATCH" ]; then
                NOMBRE_CANONICO=$(echo "$MATCH" | cut -d'|' -f4)
            fi
        fi

        if [ -n "$NOMBRE_CANONICO" ]; then
            NOMBRE_LIMPIO=$(echo "$NOMBRE_CANONICO" | tr '/\t' '- ' | tr -s ' ')
            NUEVO_ARCHIVO="${NOMBRE_LIMPIO}.${EXT}"

            if [ "$NOMBRE_ACTUAL" = "$NUEVO_ARCHIVO" ]; then
                echo "[OK]        $NOMBRE_ACTUAL"
            else
                echo "[RENOMBRAR] $NOMBRE_ACTUAL"
                echo "       ---> $NUEVO_ARCHIVO"
                TOTAL_RENOMBRADOS=$((TOTAL_RENOMBRADOS + 1))
                mv "$archivo" "$DIR/$NUEVO_ARCHIVO"
            fi
        else
            echo "[IGNORADO]  No encontrado en .dat: $NOMBRE_ACTUAL"
            TOTAL_DESCONOCIDOS=$((TOTAL_DESCONOCIDOS + 1))
        fi
    done
done

echo "--------------------------------------------------------"
echo ">> [3/3] Resumen:"
echo "   - Total analizados:  $TOTAL_PROCESADOS"
echo "   - Para renombrar:    $TOTAL_RENOMBRADOS"
echo "   - No identificados:  $TOTAL_DESCONOCIDOS"
echo "--------------------------------------------------------"
echo ">> [EXITO] Archivos renombrados correctamente. Listo para el scraper."
