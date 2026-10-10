# Parches del kernel
#
# Los .patch de este directorio se aplican, por orden, sobre el código fuente de
# Linux (kernel.org) antes de compilar, tanto en build_kernel.sh como en el workflow
# build-kernel.yml. Son específicos del Allwinner H700 y de las pantallas de las
# RG40XX / RG35XX.
#
# Parten de los parches de la distribución de referencia para H700 y están adaptados
# a las versiones de kernel que usamos. Al saltar de versión algunos hunks pueden
# fallar o estar ya integrados en mainline: ver ../KERNEL-UPDATE.md.
