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
| Pantalla | 640×480 MIPI DPI SPI |
| Almacenamiento | MicroSD (mmcblk0) |

## Estructura del repo

Este repositorio es un **Buildroot external tree**. No contiene Buildroot, lo descarga en build time.

```
armiga/
├── .github/workflows/build.yml   # CI GitHub Actions
├── board/armiga/                  # Board-specific files
│   ├── bootloader/                # Kernel Image, DTB, U-Boot (binarios)
│   ├── linux/                     # Kernel config y parches
│   ├── rootfs_overlay/            # Overlay sobre el rootfs generado
│   ├── genimage.cfg               # Layout de particiones SD
│   ├── post-build.sh              # Ajustes pre-imagen
│   └── post-image.sh              # dd U-Boot + formateo exFAT
├── configs/armiga_defconfig       # Defconfig Buildroot
├── package/                       # Paquetes custom
└── external.mk / external.desc   # Registro Buildroot external
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

### Build local

```bash
# Descargar Buildroot
make BR2_EXTERNAL=$PWD -C buildroot O=$PWD/output armiga_defconfig
make -C output -j$(nproc)
```

### Build en GitHub Actions

Trigger manual en Actions → Build → Run workflow.

### Flashear SD

```bash
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
sudo sgdisk -e /dev/sdX   # Corregir GPT backup table
sync
```

## Fases

- **Fase 1** — Base estable: boot, WiFi, BT, SSH (Dropbear), teclado ES, fastfetch ← **actual**
- **Fase 2** — Pantalla: driver panel-mipi-dpi-spi, firmware .panel
- **Fase 3** — Gráficos: Mesa3D/Panfrost, SDL3
- **Fase 4** — Amiberry: compilación, autoarranque, capsimg
- **Fase 5** — Polish: splash, optimización, overclock

## Kernel

Binario precompilado de Rocknix (7.0.10). Se compila localmente y se commitea al repo.  
El CI lo copia sin recompilar.

## Licencia

Cada componente mantiene su licencia original.
