# Armiga — Sistema de configuración de RetroArch y PUAE2021

**Fecha:** 2026-08-21

Documenta cómo funcionan realmente los ficheros de configuración de RetroArch/PUAE2021 en Armiga, y el flujo para modificarlos de forma ágil.

---

## 1. Los tres ficheros involucrados

| Fichero | Ubicación | Rol |
|---|---|---|
| `retroarch.cfg.template` | repo: `board/armiga/rootfs_overlay/etc/retroarch/retroarch.cfg.template` | Plantilla maestra versionada. Semilla inicial de `retroarch.cfg` en el dispositivo. |
| `retroarch.cfg` | dispositivo: `/media/amiga_data/retroarch/retroarch.cfg` | Config **viva**, editable por el usuario y por el propio RetroArch (se reescribe entera al cerrar la app). |
| `armiga.cfg` | repo: `board/armiga/rootfs_overlay/etc/retroarch/armiga.cfg` | Claves fijas, forzadas siempre vía `--appendconfig` en cada arranque. Inmutable pase lo que pase en `retroarch.cfg`. No tiene versión, va en la imagen del sistema (no en `amiga_data`). |
| `PUAE 2021.opt` | repo: `board/armiga/rootfs_overlay/etc/retroarch/PUAE 2021.opt` → dispositivo: `/media/amiga_data/retroarch/config/PUAE 2021/PUAE 2021.opt` | Opciones del core PUAE2021. Mismo patrón de versionado que `retroarch.cfg.template` desde 2026-08-21. |

### Wrapper de lanzamiento (`/usr/bin/retroarch`)

```sh
/usr/bin/retroarch.real \
  --config /media/amiga_data/retroarch/retroarch.cfg \
  --appendconfig /etc/retroarch/armiga.cfg \
  "$@"
```

`armiga.cfg` se aplica **por encima** de `retroarch.cfg` en cada arranque — cualquier clave definida ahí gana siempre, aunque el usuario la cambie desde el menú de RetroArch.

---

## 2. Mecanismo de versionado y sincronización a dispositivos existentes

Tanto `retroarch.cfg.template` como `PUAE 2021.opt` llevan una primera línea:

```
# ARMIGA_CFG_VERSION=N
```

En `S40partitions` (arranque), para cada uno de los dos ficheros:

```sh
TEMPLATE_VER=$(grep "^# ARMIGA_CFG_VERSION=" <template_repo> | cut -d= -f2)
INSTALLED_VER=$(grep "^# ARMIGA_CFG_VERSION=" <fichero_en_amiga_data> 2>/dev/null | cut -d= -f2)
if [ ! -f <fichero_en_amiga_data> ] || [ "$TEMPLATE_VER" -gt "$INSTALLED_VER" ]; then
    cp <template_repo> <fichero_en_amiga_data>
fi
```

**Importante — esto es destructivo:** si subes la versión del template, el fichero completo del dispositivo se sobrescribe con el del repo en el siguiente arranque. No hay merge de claves individuales; es todo o nada. Por eso el flujo de sincronización (sección 3) va siempre en la dirección dispositivo → repo antes de subir versión, nunca al revés sin pasar por ahí.

Este mecanismo corre en dos sitios de `S40partitions`:
- Primer arranque tras expandir la partición `amiga_data` (formateo inicial).
- Cada arranque normal, como parte de la autorreparación si el fichero falta o su versión quedó atrás.

---

## 3. Flujo ágil de trabajo (edición en vivo → repo)

### 3.1. Editar en el dispositivo

Cambia los parámetros que quieras desde el propio menú de RetroArch (Configuración, Opciones del núcleo de PUAE2021, etc.) y guarda. RetroArch reescribe `retroarch.cfg` completo al salir; `PUAE 2021.opt` se guarda al aplicar opciones del core.

### 3.2. Traer los cambios al repo: `tools/retroarch-sync.sh`

```bash
# Solo ver el diff, sin tocar el repo
./tools/retroarch-sync.sh root@<IP>

# Aplicar: sobrescribe el template completo y sube ARMIGA_CFG_VERSION +1
./tools/retroarch-sync.sh root@<IP> --apply
```

Qué hace:
1. Descarga `retroarch.cfg` y `PUAE 2021.opt` del dispositivo por SSH (`cat | ssh` — sin scp, Dropbear no tiene sftp-server).
2. Compara (ignorando la línea de versión) contra los ficheros del repo y muestra el diff.
3. Con `--apply`: escribe el fichero completo del dispositivo en el repo, con la cabecera de versión incrementada en +1.

### 3.3. Ruido esperado en el diff

RetroArch reescribe el `.cfg` entero con **todas** las claves de su versión actual, incluidas las que nunca tocaste, con su valor por defecto. Si ha pasado tiempo desde que se generó el template, el diff tendrá muchas líneas nuevas irrelevantes (`input_playerN_hold_*`, `video_hdr_*`, etc.) mezcladas con tus cambios reales. Esto es inofensivo — son defaults legítimos de esa versión de RetroArch, no corrupción. Una vez rebaseado el template con `--apply`, los diffs posteriores vuelven a ser pequeños y legibles mientras no cambie la versión de RetroArch integrada.

### 3.4. Después de `--apply`

```bash
git status --short
git add -A
git commit -m "Sync retroarch.cfg / PUAE 2021.opt templates with live device config"
git push origin <rama>
```

Confirma que las dos cabeceras de versión subieron correctamente:

```bash
grep "^# ARMIGA_CFG_VERSION=" board/armiga/rootfs_overlay/etc/retroarch/retroarch.cfg.template
grep "^# ARMIGA_CFG_VERSION=" "board/armiga/rootfs_overlay/etc/retroarch/PUAE 2021.opt"
```

Con esto, cualquier dispositivo existente que arranque con esta build recibirá el fichero actualizado (se sobrescribirá el suyo local, ver advertencia de la sección 2).

---

## 4. Cuándo usar `armiga.cfg` en vez de `retroarch.cfg.template`

- **`armiga.cfg`** (forzado siempre, no versionado, vive en la imagen): para claves que **nunca** deben poder cambiarse desde el dispositivo — drivers de audio/vídeo, rutas de fuentes del sistema, tema de menú por defecto. Si lo tocas, no hace falta tocar versión ni sincronizar nada: va con cada build.
- **`retroarch.cfg.template`**: para valores de partida que el usuario sí puede cambiar libremente después (y de hecho ya lo hará usando el propio menú). Requiere bump de versión para llegar a dispositivos existentes.

---

## 5. Limitación conocida

El versionado es "todo o nada" por fichero — no hay merge selectivo de claves individuales entre lo que trae el template nuevo y lo que el usuario ya tenía personalizado en su propio dispositivo. Si un usuario cambió manualmente `audio_volume` en su unidad y luego se sube de versión el template por otro motivo, su valor personalizado se pierde. No hay mitigación implementada para esto todavía.
