#!/bin/sh
# DESC_ES: Verifica los hashes MD5 de las kickstarts instaladas (correcto/incorrecto/no existe)
# DESC_EN: Verifies MD5 hashes of installed kickstarts (correct/incorrect/missing)

KICK_DIR="/media/amiga_data/kickstarts"

echo "========================================================"
echo "  Verificacion de Kickstarts (MD5)"
echo "========================================================"

if [ ! -d "$KICK_DIR" ]; then
    echo "[ERROR] No existe el directorio:"
    echo "        $KICK_DIR"
    exit 1
fi

KICK_DB="
kick33180.A500|85ad74194e87c08904327de1a9443b7a
kick34005.A500|82a21c1890cae844b3df741f2762d48d
kick34005.CDTV|89da1838a24460e4b93f4f0c5d92d48d
kick37175.A500|dc10d7bdd1b6f450773dfb558477c230
kick37350.A600|465646c9b6729f77eea5314d1f057951
kick39106.A1200|b7cc148386aa631136f510cd29e42fc3
kick39106.A4000|9b8bdd5a3fd32c2a5a6f5b1aefc799a5
kick40060.CD32|5f8924d013dd57a89cf349f4cdedc6b1
kick40060.CD32.ext|bb72565701b1b6faece07d68ea5da639
kick40063.A600|e40a5dfb3d017ba8779faba30cbd1c8e
kick40068.A1200|646773759326fbac3b2311fd8c8793ee
kick40068.A4000|9bdedde6a4f33555b4a270c8ca53297d
"

echo "$KICK_DB" | while IFS='|' read -r NOMBRE MD5_OK; do
    [ -z "$NOMBRE" ] && continue
    FILE="$KICK_DIR/$NOMBRE"
    if [ ! -f "$FILE" ]; then
        echo "$NOMBRE - $MD5_OK | N/A - [NO EXISTE]"
        continue
    fi
    MD5_REAL=$(md5sum "$FILE" | awk '{print $1}')
    if [ "$MD5_REAL" = "$MD5_OK" ]; then
        echo "$NOMBRE - $MD5_OK | $MD5_REAL - [CORRECTO]"
    else
        echo "$NOMBRE - $MD5_OK | $MD5_REAL - [INCORRECTO]"
    fi
done

echo "--------------------------------------------------------"
echo ">> Verificacion completada."
