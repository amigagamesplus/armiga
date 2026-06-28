# ARMIGA — Proceso de actualización del kernel

> Filosofía: usar siempre la versión estable más reciente del kernel vanilla oficial (kernel.org), contrastada en hardware real antes de commitear.

---

## Resumen del flujo

```
1. Descargar tarball oficial de kernel.org
2. Verificar checksum SHA256
3. Compilar con build_kernel.sh (cambiando KERNEL_VERSION)
4. Recompilar el driver joypad con DEVICE=H700
5. Regenerar modules.dep
6. Probar en hardware real
7. Commitear
```

---

## Requisitos previos

- Cross-compiler instalado en local: `aarch64-linux-gnu-gcc`
- `depmod` disponible en local (paquete `kmod`)
- Acceso SSH al dispositivo: `ssh root@10.212.82.130`
- SD en `/dev/sdX` para prueba antes de commitear

Verificar el toolchain:
```bash
aarch64-linux-gnu-gcc --version
```

---

## Paso 1 — Descargar y verificar el tarball

```bash
cd /run/media/vince/samsung/armiga/board/armiga/linux

# Descargar (el -c permite reanudar si se corta)
wget -c https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-X.Y.Z.tar.xz

# Verificar checksum contra el oficial
sha256sum linux-X.Y.Z.tar.xz
wget -qO- https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc | grep linux-X.Y.Z.tar.xz
```

Los dos hashes deben coincidir exactamente. No continuar si no coinciden.

---

## Paso 2 — Actualizar la versión en build_kernel.sh

```bash
sed -i 's/KERNEL_VERSION="X.Y.OLD"/KERNEL_VERSION="X.Y.NEW"/' \
    /run/media/vince/samsung/armiga/board/armiga/linux/build_kernel.sh
```

Verificar:
```bash
grep KERNEL_VERSION /run/media/vince/samsung/armiga/board/armiga/linux/build_kernel.sh
```

---

## Paso 3 — Compilar el kernel

```bash
cd /run/media/vince/samsung/armiga/board/armiga/linux
bash build_kernel.sh 2>&1 | tee build_kernel_X.Y.Z.log
```

El script hace automáticamente:
- Extraer las fuentes si no existe el directorio
- Aplicar todos los parches de `patches/`
- Copiar el DTS personalizado y añadirlo al Makefile de allwinner
- Adaptar el `.config` con `adapt_config.sh`
- Compilar `Image`, DTBs y módulos
- Copiar `Image` y `dtb.img` a `board/armiga/bootloader/`
- Instalar módulos en `modules_out/`

⚠️ Si algún parche falla, el script aborta. Ver sección "Resolución de problemas" al final.

---

## Paso 4 — Recompilar el driver joypad

**Siempre** hay que recompilar el `.ko` contra las fuentes nuevas. Un `.ko` compilado contra una versión distinta no carga.

```bash
cd /run/media/vince/samsung/armiga/board/armiga/linux/rocknix-joypad

make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     DEVICE=H700 \
     KERNEL_SRC=/run/media/vince/samsung/armiga/board/armiga/linux/linux-X.Y.Z \
     -C /run/media/vince/samsung/armiga/board/armiga/linux/linux-X.Y.Z \
     M=$(pwd) modules
```

⚠️ El `DEVICE=H700` es obligatorio. Sin él, el Makefile compila también `rocknix-joypad.c` que usa `input-polldev.h` (eliminado en kernels recientes) y falla.

---

## Paso 5 — Preparar el rootfs_overlay

```bash
NEW_VERSION="X.Y.Z"
OLD_VERSION="X.Y.OLD"
OVERLAY=/run/media/vince/samsung/armiga/board/armiga/rootfs_overlay
MODULES_OUT=/run/media/vince/samsung/armiga/board/armiga/linux/modules_out

# Eliminar symlinks que apuntan a rutas locales (no existen en el dispositivo)
rm -f ${MODULES_OUT}/lib/modules/${NEW_VERSION}-armiga/build
rm -f ${MODULES_OUT}/lib/modules/${NEW_VERSION}-armiga/source

# Copiar el .ko del joypad a los módulos nuevos
cp /run/media/vince/samsung/armiga/board/armiga/linux/rocknix-joypad/rocknix-singleadc-joypad.ko \
   ${MODULES_OUT}/lib/modules/${NEW_VERSION}-armiga/

# Eliminar módulos de la versión anterior del overlay
rm -rf ${OVERLAY}/lib/modules/${OLD_VERSION}-armiga

# Copiar módulos nuevos al overlay
cp -r ${MODULES_OUT}/lib/modules/${NEW_VERSION}-armiga \
      ${OVERLAY}/lib/modules/

# Regenerar modules.dep (necesario para que modprobe funcione)
sudo depmod -a -b ${OVERLAY} ${NEW_VERSION}-armiga

# Actualizar KERNEL en bootloader (U-Boot lo busca como KERNEL, no como Image)
cp /run/media/vince/samsung/armiga/board/armiga/bootloader/Image \
   /run/media/vince/samsung/armiga/board/armiga/bootloader/KERNEL
```

---

## Paso 6 — Probar en hardware real ANTES de commitear

Montar la SD y copiar los binarios nuevos:

```bash
sudo mkdir -p /mnt/armiga_boot /mnt/armiga_root

# Partición boot (p1)
sudo mount /dev/sdX1 /mnt/armiga_boot
sudo cp /run/media/vince/samsung/armiga/board/armiga/bootloader/KERNEL /mnt/armiga_boot/KERNEL
sudo cp /run/media/vince/samsung/armiga/board/armiga/bootloader/dtb.img /mnt/armiga_boot/dtb.img
sudo sync
sudo umount /mnt/armiga_boot

# Partición rootfs (p2)
sudo mount /dev/sdX2 /mnt/armiga_root

# Eliminar módulos viejos y copiar nuevos
sudo rm -rf /mnt/armiga_root/lib/modules/${OLD_VERSION}-armiga
sudo cp -r /run/media/vince/samsung/armiga/board/armiga/linux/modules_out/lib/modules/${NEW_VERSION}-armiga \
           /mnt/armiga_root/lib/modules/

# Regenerar modules.dep en la SD
sudo depmod -a -b /mnt/armiga_root ${NEW_VERSION}-armiga

sudo sync
sudo umount /mnt/armiga_root
```

Arrancar el dispositivo y verificar:

```bash
# Versión del kernel
ssh root@10.212.82.130 "uname -r"
# Debe devolver: X.Y.Z-armiga

# WiFi (crítico)
ssh root@10.212.82.130 "lsmod | grep rtw"
# Debe mostrar: rtw88_core, rtw88_8821c, rtw88_sdio, rtw88_8821cs

# Driver joypad
ssh root@10.212.82.130 "modprobe rocknix-singleadc-joypad && lsmod | grep joypad"
# Debe mostrar: rocknix_singleadc_joypad

# Verificar input del gamepad
ssh root@10.212.82.130 "evtest --list"
```

No commitear hasta que las tres verificaciones sean correctas.

---

## Paso 7 — Commit

```bash
cd /run/media/vince/samsung/armiga

git add board/armiga/bootloader/Image \
        board/armiga/bootloader/KERNEL \
        board/armiga/bootloader/dtb.img \
        board/armiga/rootfs_overlay/lib/modules/ \
        board/armiga/linux/build_kernel.sh \
        board/armiga/linux/rocknix-joypad/

git commit -m "kernel: actualizar de X.Y.OLD a X.Y.NEW

- Compilado con aarch64-linux-gnu-gcc $(aarch64-linux-gnu-gcc --version | head -1 | awk '{print $4}')
- Todos los parches aplican limpiamente
- RTW88 verificado en hardware
- Driver joypad recompilado contra X.Y.NEW con DEVICE=H700
- depmod regenerado
- uname -r: X.Y.NEW-armiga"

git push
```

---

## Resolución de problemas

### Un parche no aplica limpiamente

Comparar el contexto del parche con el código de la nueva versión:
```bash
patch --dry-run -p1 < board/armiga/linux/patches/NNNN-nombre.patch
```

Si el fallo es menor (números de línea desplazados), `patch` normalmente lo resuelve solo con `-p1`. Si el fallo es real, hay que adaptar el parche manualmente al código nuevo.

Para referencia, ROCKNIX mantiene parches equivalentes para el H700 en su árbol — útil para ver cómo han resuelto el mismo cambio en versiones más recientes.

### El driver joypad no compila

Verificar que se pasa `DEVICE=H700`. Sin esa variable, el Makefile compila ambos drivers y `rocknix-joypad.c` usa APIs eliminadas en kernels recientes.

### modprobe: module not found in modules.dep

Falta ejecutar `depmod`. Ver Paso 5. Buildroot lo ejecuta automáticamente en el build de CI, pero en pruebas manuales hay que hacerlo explícitamente.

### El gamepad no aparece en evtest

Verificar que el `.ko` está compilado contra la versión correcta del kernel:
```bash
ssh root@10.212.82.130 "modinfo /lib/modules/$(uname -r)/rocknix-singleadc-joypad.ko | grep vermagic"
```
El `vermagic` debe coincidir con `uname -r`.
