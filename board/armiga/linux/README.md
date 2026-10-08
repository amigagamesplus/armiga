# Placeholder — Kernel config y parches
#
# Ficheros que se añadirán cuando compilemos el kernel propio:
#
#   linux.aarch64.conf  — Config del kernel (base upstream H700)
#   patches/            — Parches upstream para el kernel
#
# Fuente:
#   git clone --depth=1 https://github.com/ROCKNIX/distribution.git upstream-src
#   Config: upstream-src/projects/ROCKNIX/devices/H700/linux/linux.aarch64.conf
#   Parches: upstream-src/projects/ROCKNIX/devices/H700/patches/linux/
#
# En Fase 1 usamos el kernel binario precompilado del proyecto upstream,
# por lo que estos ficheros no son necesarios todavía.
