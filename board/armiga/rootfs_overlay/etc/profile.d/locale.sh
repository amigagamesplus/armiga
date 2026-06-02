# =============================================================================
# locale.sh — Teclado español y locale
# Se carga en cada login vía /etc/profile.d/
# =============================================================================

# Locale
export LANG=es_ES.UTF-8
export LC_ALL=es_ES.UTF-8

# Teclado español (consola)
if [ -f /usr/share/keymaps/es.bmap ]; then
    loadkmap < /usr/share/keymaps/es.bmap 2>/dev/null
fi
