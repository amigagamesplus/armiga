#!/bin/bash
# ci-lint.sh — comprobaciones rapidas del repositorio (se ejecuta en CI y en local).
# Uso, desde cualquier sitio:  bash scripts/ci-lint.sh
#   1. Permisos: los scripts de init.d y arexx_scripts deben estar en 100755.
#   2. Formato de release-notes/v*.md (### Added / ### Fixed / ### Changed).
#   3. Sintaxis: scripts del dispositivo con BusyBox sh -n; scripts del host con bash -n.
#   4. shellcheck: informativo (LINT_SHELLCHECK_STRICT=1 lo hace bloqueante en errores).
set -u
cd "$(dirname "$0")/.." || exit 2

fail=0
err()  { echo "ERROR: $*"; fail=1; }
warn() { echo "AVISO: $*"; }

DEV_RE='^board/armiga/rootfs_overlay/(etc/init\.d|usr/share/armiga/arexx_scripts)/'

# --- 1. Permisos -----------------------------------------------------------
echo "== 1/4 permisos (100755)"
n_mode=0
while IFS=$'\t' read -r meta path; do
    mode=${meta%% *}
    [ "$mode" = "120000" ] && continue
    if [[ "$path" =~ $DEV_RE ]]; then
        n_mode=$((n_mode + 1))
        [ "$mode" = "100755" ] || err "$path: modo $mode (esperado 100755; usar git update-index --chmod=+x)"
    fi
done < <(git ls-files -s)
echo "   revisados: $n_mode"

# --- 2. Formato de las notas de release ------------------------------------
echo "== 2/4 release-notes"
n_notes=0
for f in release-notes/v*.md; do
    [ -f "$f" ] || continue
    n_notes=$((n_notes + 1))
    [ -z "$(tail -c1 "$f")" ] || err "$f: no termina en salto de linea"
    if ! awk -v f="$f" '
        BEGIN { bad = 0; head = 0; sections = 0; items = 0 }
        /\t/ { print "ERROR: " f ":" NR ": contiene tabuladores"; bad = 1 }
        /^### / {
            if ($0 != "### Added" && $0 != "### Fixed" && $0 != "### Changed") {
                print "ERROR: " f ":" NR ": encabezado no permitido: " $0; bad = 1 }
            sections++; head = NR; next }
        head && NR == head + 1 && $0 != "" {
            print "ERROR: " f ":" NR ": falta la linea en blanco tras el encabezado"; bad = 1 }
        /^- / { items++; next }
        /^$/ { next }
        { print "ERROR: " f ":" NR ": linea que no es encabezado, viñeta ni vacia: " $0; bad = 1 }
        END {
            if (sections == 0) { print "ERROR: " f ": sin secciones"; bad = 1 }
            if (items == 0)    { print "ERROR: " f ": sin viñetas"; bad = 1 }
            exit bad }' "$f"; then
        fail=1
    fi
done
echo "   revisadas: $n_notes"

# --- 3. Sintaxis ---------------------------------------------------------------
echo "== 3/4 sintaxis"
mapfile -t DEV_FILES < <(git ls-files | grep -E "$DEV_RE" | while read -r p; do [ -f "$p" ] && [ ! -L "$p" ] && echo "$p"; done)
mapfile -t HOST_FILES < <(git ls-files '*.sh' | grep -Ev "$DEV_RE")
if command -v busybox >/dev/null 2>&1; then
    for f in "${DEV_FILES[@]}"; do
        out=$(busybox sh -n "$f" 2>&1) || err "$f (busybox sh -n): $out"
    done
    echo "   dispositivo (busybox sh -n): ${#DEV_FILES[@]}"
elif [ -n "${CI:-}" ]; then
    err "busybox no esta instalado en CI (apt install busybox-static)"
else
    warn "busybox no esta instalado: se omite la sintaxis de los scripts del dispositivo (${#DEV_FILES[@]})"
fi
for f in "${HOST_FILES[@]}"; do
    out=$(bash -n "$f" 2>&1) || err "$f (bash -n): $out"
done
echo "   host (bash -n): ${#HOST_FILES[@]}"

# --- 4. shellcheck (informativo salvo LINT_SHELLCHECK_STRICT=1) -----------
echo "== 4/4 shellcheck"
if command -v shellcheck >/dev/null 2>&1; then
    n_err=$(shellcheck -s sh -S error -f gcc "${DEV_FILES[@]}" 2>/dev/null | wc -l)
    n_warn=$(shellcheck -s sh -S warning -f gcc "${DEV_FILES[@]}" 2>/dev/null | wc -l)
    echo "   errores: $n_err | avisos o mas graves: $n_warn (scripts del dispositivo)"
    if [ "$n_err" -gt 0 ]; then
        shellcheck -s sh -S error -f gcc "${DEV_FILES[@]}" 2>/dev/null | head -20
        if [ "${LINT_SHELLCHECK_STRICT:-0}" = "1" ]; then
            err "shellcheck: $n_err errores"
        else
            warn "shellcheck: $n_err errores (informativo; LINT_SHELLCHECK_STRICT=1 lo hace bloqueante)"
        fi
    fi
else
    warn "shellcheck no esta instalado: se omite"
fi

echo
if [ "$fail" -eq 0 ]; then echo "LINT OK"; else echo "LINT FALLA"; fi
exit "$fail"
