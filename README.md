# Armiga

Distribución Linux mínima basada en **Buildroot** para la consola **Anbernic RG40XX H** (Allwinner H700).  
Objetivo final: arranque directo en **Amiberry** para emulación nativa de Commodore Amiga.

## Hardware

| Componente | Detalle |
|---|---|
| SoC | Allwinner H700 (ARM64, Cortex-A53 x4) |
| GPU | Mali-G31 MP2 |
| RAM | 1 GB DDR3 |
| WiFi/BT | RTL8821CS (SDIO + UART) |
| Pantalla | 640×480 DSI |
| Almacenamiento | MicroSD (mmcblk0) |

## Estado actual

**Hito:** Amiberry 8.2.0 arranca en hardware. DPAD resuelto en kernel. Ejes analógicos pendientes.  
**Rama activa:** `feature/amiberry`

### Componentes funcionales

- ✅ Arranque (U-Boot + kernel 7.0.2-armiga + DTB)
- ✅ Pantalla DSI 640×480
- ✅ WiFi RTL8821CS (módulos `=m`, carga automática)
- ✅ SSH con Dropbear (alias `ssh armiga`)
- ✅ Teclado español desde arranque
- ✅ CPU governor en performance
- ✅ Particiones `kickstarts` y `roms` montadas como exFAT
- ✅ Autoexpansión de `roms` en primer arranque
- ✅ Panfrost built-in (Mali-G31 @ 432MHz, DRM inicializado)
- ✅ `libdrm 2.4.131` instalado
- ✅ SDL3 3.4.10 con backend `kmsdrm`
- ✅ Mesa 26.1.2 — GBM + Panfrost (`.so` stripped en `rootfs_overlay`)
- ✅ Stack gráfico completo verificado en hardware (`kmsdrm_test`: `opengles2 / OK`)
- ✅ H700 Gamepad (`/dev/input/event6`) con DPAD como `ABS_HAT0X/Y`
- ✅ Amiberry 8.2.0 arrancando con `SDL_VIDEO_DRIVER=kmsdrm amiberry`
- ⚠️ Ejes analógicos ABS_X/Y/RX/RY con rango ±1800 (pendiente → ±32767 via `button-adc-scale` en DTS)
- ⚠️ Daemon `h700-normalize` en imagen como solución temporal para ejes (eliminar cuando se resuelva en kernel)
- ⚠️ Botones X/Y invertidos en DTS (BTN_NORTH / BTN_WEST) — fix pendiente

### Stack gráfico

```
Amiberry 8.2.0
    └── SDL3 3.4.10 (backend: kmsdrm)
        ├── libgbm.so.1.0.0 (Mesa 26.1.2)
        │   └── dri_gbm.so → libgallium-26.1.2.so (Panfrost)
        ├── libEGL.so.1.0.0
        ├── libGLESv2.so.2.0.0
        └── libdrm 2.4.131
            └── DRM/KMS kernel (sun4i-drm + Panfrost, Mali-G31 @ 432MHz)
```

> **Nota Mesa 25+:** `panfrost_dri.so` ya no existe. El driver Panfrost está en
> `libgallium-<version>.so`, cargado vía `dri_gbm.so`. No buscar `/usr/lib/dri/`.

## Estructura del repositorio

```
armiga/
├── .github/workflows/
│   ├── build.yml              # CI principal — imagen Buildroot completa
│   └── build-mesa.yml         # CI Mesa 26.1.2 (misión cumplida, .so commiteados)
├── board/armiga/
│   ├── bootloader/            # Kernel, DTB y U-Boot precompilados (commiteados)
│   ├── linux/
│   │   ├── dts/               # DTS propios (rg40xx-h, v2-panel)
│   │   └── patches/           # Parches al kernel 7.0.2
│   ├── rootfs_overlay/
│   │   ├── usr/lib/           # Mesa 26.1.2 .so (stripped)
│   │   ├── usr/share/amiberry/controllers/   # gamecontrollerdb.txt
│   │   ├── root/.config/amiberry/            # amiberry.conf
│   │   ├── etc/init.d/        # Scripts de inicio
│   │   └── etc/udev/rules.d/  # Reglas udev
│   ├── tests/                 # kmsdrm_test.c, sdl_joystick_info.c
│   ├── post-build.sh
│   ├── post-image.sh
│   └── genimage.cfg
├── configs/armiga_defconfig
├── package/
│   ├── sdl3/
│   ├── sdl3_image/
│   ├── kmsdrm-test/
│   ├── amiberry/
│   ├── sdl-joystick-info/
│   ├── h700-normalize/        # Daemon normalización H700 Gamepad (temporal)
│   └── fastfetch/
├── Config.in
├── external.mk
└── external.desc
```

## Particiones SD

| # | Etiqueta | FS | Tamaño | Contenido |
|---|---|---|---|---|
| — | raw | — | 8K–2MB | U-Boot SPL |
| 1 | boot | FAT32 | 64 MB | Image, dtb.img, extlinux.conf |
| 2 | system | ext4 | 1100 MB | Rootfs |
| 3 | kickstarts | exFAT | 250 MB | Kickstart ROMs |
| 4 | roms | exFAT | resto | Juegos |

## Build

### Requisitos

- Ubuntu 22.04+ (o GitHub Actions)
- Paquetes: `build-essential wget cpio unzip rsync bc python3 libssl-dev`

### Build en GitHub Actions

Trigger manual en Actions → Build → Run workflow.  
Tiempo estimado: ~38 min. Artefactos: `armiga-sdcard-N` (N = run number).

### Build local

```bash
make BR2_EXTERNAL=$PWD -C buildroot-2026.02.2 O=$PWD/output armiga_defconfig
make -C output -j$(nproc)
```

> ⚠️ Buildroot **no está en el repositorio** — solo disponible en CI. No ejecutar grep local sobre sus fuentes.

### Flashear SD

```bash
cp ~/Descargas/armiga-sdcard-N/sdcard.img /ruta/local/armiga/sdcard.img
sudo bash /ruta/local/armiga/flash.sh /dev/sdX
```

## Ramas

| Rama | Estado |
|---|---|
| `main` | Estable — stack gráfico verificado en hardware |
| `feature/amiberry` | Amiberry 8.2.0 + fix DPAD en kernel, ejes analógicos pendientes |

## Fases

- **Fase 1** ✅ Base estable: boot, WiFi, BT, SSH, teclado ES
- **Fase 2** ✅ Pantalla: driver DSI 640×480 operativo
- **Fase 3** ✅ Gráficos: Mesa 26.1.2/Panfrost, SDL3 KMS/DRM
- **Fase 4** 🔧 Amiberry: arranca en hardware, input en progreso
- **Fase 5** — Audio ALSA
- **Fase 6** — Polish: splash, arranque silencioso, overclock

## Próximos pasos

- [ ] Resolver ejes analógicos ±1800 → ±32767 via `button-adc-scale` en DTS
- [ ] Eliminar daemon `h700-normalize` cuando ejes funcionen en kernel
- [ ] Actualizar `gamecontrollerdb.txt` con GUID raw si se elimina el daemon
- [ ] Merge `feature/amiberry` → `main`
- [ ] ALSA — configurar audio
- [ ] Fix botones X/Y invertidos en DTS (BTN_NORTH / BTN_WEST)
- [ ] Splash screen / arranque silencioso

## Kernel

Binario precompilado (7.0.2-armiga) commiteado en `board/armiga/bootloader/`.  
El CI lo copia sin recompilar. Los cambios en `.dts` solo requieren recompilar el DTB.

## Notas críticas

- **WiFi en mmc1** (4021000), NO mmc2.
- **RTW88 como módulos** (`=m`), nunca built-in (`=y`).
- **U-Boot siempre en offset 8K** (`seek=16` con `bs=512`). Siempre MBR, nunca GPT.
- **YAML en CI**: usar scripts Python para modificar, nunca `sed` con caracteres especiales de Makefile.
- **`.so` grandes**: siempre `aarch64-linux-gnu-strip --strip-unneeded` antes de commitear (límite GitHub: 100MB). `libgallium` sin strip pesa ~112MB; stripped ~23MB.
- **Paquetes locales Buildroot**: incrementar `VERSION` en el `.mk` cuando cambie el código fuente.
- **Amiberry runtime**: lanzar siempre con `SDL_VIDEO_DRIVER=kmsdrm`.
- **`sdl_joystick_info`**: ejecutar con `SDL_VIDEO_DRIVER=kmsdrm sdl_joystick_info` para obtener GUIDs reales.

## Licencia

Cada componente mantiene su licencia original.
