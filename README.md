<p align="center">
  <img src="docs/assets/logo-armiga.png" alt="Armiga" width="400">
</p>

Distribución Linux mínima basada en **Buildroot** para la consola **Anbernic RG40XX H** (Allwinner H700), enfocada en emulación de Commodore Amiga vía **RetroArch + núcleo PUAE**.

## Hardware

| Componente | Detalle |
|---|---|
| SoC | Allwinner H700 (ARM64, Cortex-A53 x4 @ 1.51GHz) |
| GPU | Mali-G31 MP2 (Panfrost) |
| RAM | 1 GB LPDDR4 |
| WiFi/BT | RTL8821CS (SDIO) |
| Pantalla | 640×480 |
| Almacenamiento | MicroSD (mmcblk0) |

## Estado actual

**Versión:** 1.0

### Componentes funcionales

- ✅ Arranque (U-Boot + kernel 7.0.14-armiga + DTB)
- ✅ Esquema de particiones **A/B** con rollback automático (ver más abajo)
- ✅ Rootfs en **SquashFS + zstd** (system/system_b, 300MB por slot, ro)
- ✅ WiFi RTL8821CS (`wpa_supplicant` + `udhcpc`, config generada en `/tmp` — `/etc` es ro)
- ✅ SSH (Dropbear, root, puerto 22) — activado por defecto, toggleable desde Configuración
- ✅ CPU governor dinámico: `performance` en uso normal, `powersave` durante ahorro de pantalla
- ✅ GPU governor forzado a `performance` (648MHz fijo — `simple_ondemand` no escalaba bien bajo carga real)
- ✅ ZRAM swap (512MB, LZ4, `swappiness=100`)
- ✅ Partición `amiga_data` (exFAT, autoexpansión al 100% de la SD en primer arranque)
- ✅ Stack gráfico: SDL3 3.4.12 (kmsdrm) + Mesa 26.1.4 (GBM + Panfrost, `.so` stripped en overlay)
- ✅ Launcher propio en C/SDL3 (`armiga-launcher`), interfaz bilingüe **Español/English** (toggle `L1`, persistente)
- ✅ RetroArch 1.22.2 + núcleo PUAE 2021
- ✅ Sistema de actualización OTA (GitHub Releases → descarga → verificación SHA256 → flasheo del slot inactivo, todo asíncrono sin bloquear la UI)
- ✅ Rollback automático A/B ante fallo de arranque (contador de intentos + reversión de slot)
- ✅ Menú Configuración: red inalámbrica, copia de seguridad (crear/restaurar/eliminar), LED RGB de los analógicos, zona horaria, ahorro de pantalla, brillo, SSH, restablecer valores de fábrica
- ✅ Modo desarrollador (terminal, btop) accesible con combo `SELECT+START+L1`

### Particiones (MBR, nunca GPT) — esquema A/B

| # | Etiqueta | FS | Tamaño | Contenido | Montaje |
|---|---|---|---|---|---|
| 1 | boot | FAT32 | 64 MB | Kernel, dtb.img, extlinux.conf (labels Armiga-A/B) | `/boot` (bajo demanda) |
| 2 | system | squashfs | 300 MB | Rootfs — slot A | `/` (ro) si `DEFAULT=Armiga-A` |
| 3 | system_b | squashfs | 300 MB | Rootfs — slot B (copia idéntica en cada build) | `/` (ro) si `DEFAULT=Armiga-B` |
| 4 | amiga_data | exFAT | resto del disco | Kickstarts, ROMs, configs, saves | `/media/amiga_data` |

`amiga_data` **siempre** debe ser la última partición física, para que la auto-expansión al 100% del disco funcione en SD de cualquier tamaño.

### Rollback automático A/B

Al arrancar, `S02bootcheck` compara el slot activo (leído de `/proc/cmdline`) contra un contador de intentos persistido en `p1`. Si el launcher no confirma un arranque exitoso (primer frame SDL renderizado) en 3 intentos consecutivos, el sistema revierte automáticamente `DEFAULT` en `extlinux.conf` al último slot conocido como bueno y reinicia — sin intervención del usuario.

### Stack gráfico

```
armiga-launcher / RetroArch (PUAE)
    └── SDL3 3.4.12 (backend: kmsdrm)
        ├── libgbm.so.1.0.0 (Mesa 26.1.4)
        │   └── dri_gbm.so → libgallium-26.1.4.so (Panfrost)
        ├── libEGL.so.1.0.0
        ├── libGLESv2.so.2.0.0
        └── libdrm
            └── DRM/KMS kernel (Panfrost, Mali-G31 @ 648MHz fijo)
```

> **Nota Mesa 25+:** `panfrost_dri.so` ya no existe. El driver Panfrost está en `libgallium-<version>.so`, cargado vía `dri_gbm.so`. No buscar en `/usr/lib/dri/`.

## Estructura del repositorio

```
armiga/
├── .github/workflows/
│   ├── build.yml              # CI principal — imagen Buildroot completa
│   └── build-mesa.yml         # CI Mesa (cross-compile GBM+Panfrost, .so commiteados)
├── board/armiga/
│   ├── bootloader/            # Kernel, DTB y U-Boot precompilados (commiteados)
│   ├── linux/dts/             # DTS propios (rg40xx-h, v2-panel)
│   ├── rootfs_overlay/
│   │   ├── usr/lib/           # Mesa .so (stripped)
│   │   ├── usr/bin/           # armiga-launcher-wrapper, retroarch (wrapper)
│   │   ├── etc/retroarch/     # retroarch.cfg.template, armiga.cfg, autoconfig/
│   │   ├── etc/init.d/        # Scripts de arranque (S02bootcheck, S06gpu, S07zram, S40partitions, S41wifi, S43update, ...)
│   │   └── media/amiga_data/  # Mountpoint estático (necesario: rootfs es ro)
│   ├── post-build.sh          # Genera /etc/armiga-release
│   ├── post-image.sh          # Genera extlinux.conf, amiga_data.img, invoca genimage
│   └── genimage.cfg
├── configs/armiga_defconfig
├── package/
│   ├── sdl3/
│   ├── sdl3_ttf/
│   └── armiga-launcher/       # Launcher propio (C + SDL3)
├── Config.in
├── external.mk
└── external.desc
```

## Build

### Requisitos

- Ubuntu 22.04+ (o GitHub Actions)
- Paquetes: `build-essential wget cpio unzip rsync bc python3 libssl-dev`

### Build en GitHub Actions

Trigger manual (`workflow_dispatch`) en Actions → Build Armiga → Run workflow. **Verificar que la rama seleccionada en "Use workflow from" sea la correcta** antes de lanzar.
Tiempo estimado: 30-40 min (~1h+ si se modifica `configs/armiga_defconfig`, invalida la cache de ccache/toolchain).

### Build local

```bash
make BR2_EXTERNAL=$PWD -C buildroot-2026.02.2 O=$PWD/output armiga_defconfig
make -C output -j$(nproc)
```

## Notas críticas

- **Rootfs es SquashFS de solo lectura.** Cualquier código que escriba en tiempo de ejecución debe apuntar a `/media/amiga_data` o a tmpfs (`/tmp`, `/var/*`), nunca a rutas de rootfs. Esto incluye configs generadas (`wpa_supplicant.conf`), caches (shader cache de Mesa), y mountpoints (deben existir ya en el overlay, no crearse con `mkdir` en runtime).
- **`amiga_data` es siempre la última partición física** (hoy p4) — necesario para que la auto-expansión funcione.
- **`rootfstype=` en `extlinux.conf` debe coincidir siempre con el filesystem real** de system/system_b. Un desajuste causa fallo de arranque silencioso.
- **Bit ejecutable en git**: usar siempre `git update-index --chmod=+x` tras añadir un script nuevo; verificar con `git ls-files -s` (debe mostrar `100755`). No asumir que `chmod +x` local se preserva en el commit.
- **`.so` grandes**: siempre `aarch64-linux-gnu-strip --strip-unneeded` antes de commitear. `libgallium` sin stripar pesa ~110-120MB; stripped, ~23MB.
- **`dd` sobre el dispositivo es destructivo** — verificar siempre offsets (`sfdisk -d`, posicional) antes de flashear.
- **GitHub API `/releases/latest` no devuelve prereleases** — el launcher usa `/releases`.
- **BusyBox awk no soporta `match()` con array** — usar `grep`+`cut` para parsear `sfdisk -d`.
- **Nunca parchear el squashfs manualmente en la SD** (`unsquashfs`/`mksquashfs` a mano) — no preserva atributos extendidos de forma fiable. Cualquier cambio se recompila desde Buildroot.

## Licencia

Cada componente mantiene su licencia original.
