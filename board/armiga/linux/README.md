# Kernel propio de ARMIGA
#
# El kernel se compila a partir del código fuente de kernel.org (vanilla)
# con los parches y los DTS de este directorio, no se usa un kernel de terceros.
#
#   linux.aarch64.conf  — Config del kernel (base: config de la distribución de referencia H700)
#   patches/            — Parches aplicados al código fuente antes de compilar
#   dts/                — DTS propios de cada dispositivo
#   rocknix-joypad/     — Driver del joypad (se compila como módulo)
#   build_kernel.sh     — Compilación local (mismos pasos que el workflow)
#
# En CI lo compila el workflow build-kernel.yml. Pasos para actualizar la versión
# y copiar los artifacts al repo: KERNEL-UPDATE.md y ../bootloader/README.md.
