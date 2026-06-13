# ARMIGA - Panel de Control del Contexto
 
## 1. ESTADO ACTUAL (Source of Truth)
 
- **Última actualización:** 2026-06-13
- **Hito actual:** Stack gráfico completo verificado en hardware — build #88. `kmsdrm_test` OK: pantalla verde, `Video driver: kmsdrm`, `Renderer: opengles2`.
- **Rama activa:** `experiment/mesa-gbm-only` — pendiente merge a `main`

### Componentes listos (100% funcionales)
 
- ✅ Arranque correcto (U-Boot + kernel 7.0.2-armiga + DTB)
- ✅ Pantalla DSI 640x480
- ✅ WiFi RTL8821CS (módulos =m, carga automática en mmc1/4021000)
- ✅ SSH con Dropbear (sin contraseña, clave fija, alias `ssh armiga`)
- ✅ Teclado español desde arranque (S05keymap)
- ✅ CPU governor en performance (S06cpufreq)
- ✅ Particiones kickstarts y roms montadas como exFAT
- ✅ Autoexpansión de roms en primer arranque
- ✅ `lsblk`, `evtest`, `fastfetch`, `acpi`
- ✅ OS name: `armiga 1.0 aarch64`
- ✅ `rocknix-singleadc-joypad` — H700 Gamepad funcionando (`/dev/input/event6`)
- ✅ Panfrost kernel built-in (Mali-G31 @ 432MHz, DRM initialized)
- ✅ `libdrm 2.4.131` instalado en imagen y staging
- ✅ SDL3 3.4.10 con `SDL_KMSDRM=ON` — verificado en hardware
- ✅ `kmsdrm_test` — verificado en hardware: `Video driver: kmsdrm / Renderer: opengles2 / OK`
- ✅ Mesa 26.1.2 GBM+Panfrost — .so stripped commiteados en rootfs_overlay
- ✅ Stack gráfico completo operativo en Mali-G31

### Stack gráfico (definitivo, verificado en hardware)
```
kmsdrm_test
  └── SDL3 3.4.10 (backend: kmsdrm) ✅
      ├── libgbm.so.1.0.0 (Mesa 26.1.2) ✅
      │   └── gbm/dri_gbm.so → libgallium-26.1.2.so (Panfrost) ✅
      ├── libEGL.so.1.0.0 ✅
      ├── libGLESv2.so.2.0.0 ✅
      └── libdrm 2.4.131 ✅
          └── DRM/KMS kernel (sun4i-drm + Panfrost, Mali-G31 @ 432MHz) ✅
```

### Arquitectura Mesa 25+ (cambio importante respecto a versiones anteriores)
- A partir de Mesa 24.2/25.0, `panfrost_dri.so` **ya no existe**
- El driver Panfrost está en `libgallium-<version>.so`
- `libEGL` y `libgbm` cargan `libgallium` directamente
- `dri_gbm.so` actúa como backend GBM loader → carga `libgallium-26.1.2.so`
- Stack en runtime: `libgbm → dri_gbm.so → libgallium-26.1.2.so (Panfrost)`

### .so commiteados en rootfs_overlay (todos stripped)
```
board/armiga/rootfs_overlay/usr/lib/
├── libgallium-26.1.2.so   (23MB stripped)
├── libgbm.so → libgbm.so.1.0.0
├── libgbm.so.1 → libgbm.so.1.0.0
├── libgbm.so.1.0.0
├── libEGL.so → libEGL.so.1.0.0
├── libEGL.so.1 → libEGL.so.1.0.0
├── libEGL.so.1.0.0
├── libGLESv2.so → libGLESv2.so.2.0.0
├── libGLESv2.so.2 → libGLESv2.so.2.0.0
├── libGLESv2.so.2.0.0     (46KB stripped)
└── gbm/
    └── dri_gbm.so
```

Dependencias verificadas (readelf):
- `libgbm.so.1.0.0`: libdrm, libm, libc ✅
- `libgallium-26.1.2.so`: libdrm, libstdc++, libm, libgcc_s, libc ✅ (sin LLVM, sin X11)
- `libEGL.so.1.0.0`: libgallium-26.1.2.so, libgbm, libdrm, libm, libc ✅
- `dri_gbm.so`: libgallium-26.1.2.so, libdrm, libm, libc ✅

---
 
## 2. REGLAS DE DESARROLLO Y ESTILO
 
### Entorno
- **Repositorio local:** `/run/media/vince/samsung/armiga`
- **Buildroot:** 2026.02.2 — solo disponible en CI, **nunca en local**
- **CI:** GitHub Actions, runner `ubuntu-22.04`, timeout 180 min, **runners gratuitos únicamente**
- **Cross-compiler:** `aarch64-linux-gnu-gcc` (Ubuntu 15.2.0)
- **Kernel:** precompilado y commiteado en `board/armiga/bootloader/`
- **Artefactos:** `armiga-sdcard-N` (N = run number), flash con `sudo bash flash.sh /dev/sdg`

### Convenciones
- Siempre ofrecer comandos listos para ejecutar en terminal
- Modificar archivos YAML con scripts Python (`python3 - << 'EOF'`), nunca con sed directo (problema de escaping con caracteres de Makefile)
- Verificar YAML con `python3 -c "import yaml; yaml.safe_load(...)"` antes de hacer commit
- Verificar resultados con grep/cat antes de cada commit
- Caché en CI: `dl/` (descargas Buildroot), `ccache`, `output/host` (toolchain)
- Commits atómicos con mensajes descriptivos
- **.so grandes (>100MB)**: siempre strip con `aarch64-linux-gnu-strip --strip-unneeded` antes de commitear (límite GitHub: 100MB)

### Estructura del repositorio
```
board/armiga/
├── bootloader/          # Kernel, DTB, U-Boot precompilados (commiteados)
├── linux/               # Fuentes kernel (compilación local)
├── rootfs_overlay/      # Overlay del sistema de archivos
│   └── usr/lib/         # Mesa 26.1.2 .so (ver lista completa arriba)
├── tests/               # kmsdrm_test.c
├── post-build.sh
├── post-image.sh
└── genimage.cfg
 
configs/armiga_defconfig
package/
├── sdl3/
├── kmsdrm-test/
└── fastfetch/
Config.in
external.mk
external.desc
.github/workflows/build.yml
.github/workflows/build-mesa.yml
```
 
### Ramas
- `main` — estable, funcionando
- `experiment/mesa-gbm-only` — **pendiente merge a main** (stack gráfico verificado)

---
 
## 3. HISTORIAL DE DECISIONES CLAVE
 
### Mesa3D con LLVM completo → DESCARTADO
- Tiempo de compilación: 3+ horas. Timeout en CI a 180 min. Inviable con runners gratuitos.

### Mesa3D con patch para eliminar LLVM → DESCARTADO
- Patch frágil. El `mesa3d.mk` real tiene bloques condicionales. El patch rompía el Makefile.

### SDL3 framebuffer fbdev → DESCARTADO
- Tearing inaceptable para emulación Amiga. El hardware requiere vsync preciso.

### SDL2 en lugar de SDL3 → DESCARTADO
- Amiberry 8.x requiere SDL3 exclusivamente. Migración completa desde v8.0.0.

### Amiberry 7.x (SDL2) → DESCARTADO
- Dar un paso atrás sin justificación técnica.

### minigbm (ChromiumOS) → DESCARTADO
- No tiene driver para Mali-G31/Panfrost.

### libgbm standalone → DESCARTADO
- Superado: tenemos libgbm compilado desde Mesa 26.1.2 con dependencias limpias.

### softpipe/swrast como driver GBM → DESCARTADO
- Compila sin LLVM pero desperdicia el Mali-G31 @ 432MHz disponible.

### mesa-panfork (ROCKNIX fork) → DESCARTADO
- Repo archivado noviembre 2024. Fork orientado a Mali G710/G610 (Valhall), nuestro Mali-G31 es Bifrost.

### .so de ROCKNIX/Alpine/Debian → DESCARTADO
- ROCKNIX: arrastraba X11 (libxcb, libxshmfence)
- Alpine: musl vs glibc — incompatible
- Debian: arrastra libLLVM.so, libxcb, libelf

### Mesa3D con Panfrost vía Buildroot (mesa3d.mk) → DESCARTADO
- Panfrost en Mesa 26.x requiere CLC + LLVM 21 en compilación
- Runner ubuntu-22.04 tiene LLVM 15 como máximo
- Solución: workflow dedicado `build-mesa.yml` fuera de Buildroot

### Stack adoptado (definitivo, verificado en hardware ✅)
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
Sin X11, sin Wayland, sin OpenGL completo. Amiberry en software rendering (suficiente para OCS/ECS/AGA/WHDLoad en 4x A53 @ 1.42GHz).

### Resolución SDL_UNIX_CONSOLE_BUILD
- `FATAL_ERROR` en `cmake/macros.cmake` línea 414
- Fix: `sed -i 's/if(NOT SDL_UNIX_CONSOLE_BUILD)/if(FALSE)/g' $(@D)/cmake/macros.cmake`
- Aplicado como `SDL3_POST_EXTRACT_HOOKS` en `sdl3.mk`

### Resolución pkg-config libdrm para SDL3
- Fix: `SDL3_CONF_OPTS += -DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/libdrm"` + `SDL3_CONF_ENV += PKG_CONFIG_PATH="$(STAGING_DIR)/usr/lib/pkgconfig"`

### Resolución host-mesa3d
- `mesa3d.mk` de Buildroot 2026.02.2 tiene `$(eval $(host-meson-package))` en la última línea
- Fix: `sed -i 's/$(eval $(host-meson-package))//' buildroot/package/mesa3d/mesa3d.mk`

### Resolución libgbm no encontrado en staging por SDL3
- `rootfs_overlay` no es visible en el sysroot durante la compilación
- Fix: step "Inject Mesa GBM into staging" en `build.yml` — copia `libgbm.so` + `gbm.pc` al sysroot tras el configure y antes del build
- `pkg-config gbm` → Found 26.1.2 → `SDL_KMSDRM=ON` efectivo

### Workflow dedicado build-mesa.yml ✅ MISIÓN CUMPLIDA
Mesa versión: **26.1.2**

Dependencias host CI:
- LLVM 21 via apt.llvm.org
- SPIRV-Tools desde `main` con `git-sync-deps`
- SPIRV-LLVM-Translator desde `llvm_release_210`
- Meson `1.5.0` via pip

Opciones meson host:
```
-Dgallium-drivers=panfrost -Dtools=panfrost -Dinstall-mesa-clc=true
-Dllvm=enabled -Dmesa-clc=enabled -Dprecomp-compiler=enabled
-Dplatforms= -Dglx=disabled -Dvulkan-drivers=""
-Dgbm=disabled -Degl=disabled -Dgles1=disabled -Dgles2=disabled
-Dmicrosoft-clc=disabled -Dgallium-rusticl=false
-Dvalgrind=disabled -Dlibunwind=disabled
```

Opciones meson target:
```
-Dgallium-drivers=panfrost -Dgbm=enabled -Degl=enabled -Dgles2=enabled
-Dshared-glapi=enabled -Dllvm=disabled -Dmesa-clc=system -Dprecomp-compiler=system
-Dplatforms= -Dglx=disabled -Dvulkan-drivers=""
-Dgles1=disabled -Dzstd=disabled -Dmicrosoft-clc=disabled
-Dgallium-rusticl=false -Dvalgrind=disabled -Dlibunwind=disabled -Dglvnd=disabled
```

---
 
## 4. PRÓXIMOS PASOS (Backlog Inmediato)
 
- [ ] **Paso 1 (ACTIVO):** Merge `experiment/mesa-gbm-only` → `main` (squash, commit limpio)
- [ ] **Paso 2:** Eliminar ramas obsoletas (`experiment/mesa-gbm-only`, `feature/amiberry`)
- [ ] **Paso 3:** Limpiar `build.yml` — eliminar `BR2_PACKAGE_MESA3D*` del defconfig y pasos innecesarios
- [ ] **Paso 4:** Integrar Amiberry 8.2.0 como paquete externo
- [ ] **Paso 5:** ALSA — configurar audio
- [ ] **Paso 6:** Fix botones X/Y invertidos en DTS
- [ ] **Paso 7:** Splash screen / arranque silencioso

---
 
## 5. ESTADO DEL WORKFLOW CI

### build-mesa.yml ✅ MISIÓN CUMPLIDA
- Los .so están commiteados en rootfs_overlay (stripped)
- Puede archivarse — no es necesario volver a ejecutarlo salvo actualización de Mesa

### build.yml — workflow principal Buildroot
- Tiempo de build: ~24 min (sin compilación Mesa)
- Step "Inject Mesa GBM into staging": copia libgbm al sysroot antes del build para que SDL3 cmake lo encuentre

### defconfig relevante (experiment/mesa-gbm-only)
```
BR2_PACKAGE_LIBDRM=y
BR2_PACKAGE_LIBDRM_PANFROST=y
BR2_PACKAGE_MESA3D=y
BR2_PACKAGE_MESA3D_GBM=y
BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_PANFROST=y
BR2_PACKAGE_MESA3D_OPENGL_EGL=y
BR2_PACKAGE_MESA3D_OPENGL_ES=y
BR2_PACKAGE_SDL3=y
BR2_PACKAGE_KMSDRM_TEST=y
```

### sdl3.mk relevante
```makefile
SDL3_VERSION = 3.4.10
SDL3_DEPENDENCIES = host-pkgconf libdrm alsa-lib
SDL3_CONF_OPTS += -DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/libdrm"
SDL3_CONF_ENV += PKG_CONFIG_PATH="$(STAGING_DIR)/usr/lib/pkgconfig"
SDL3_CONF_OPTS = \
    -DSDL_WAYLAND=OFF -DSDL_X11=OFF -DSDL_KMSDRM=ON \
    -DSDL_ALLOW_NO_DISPLAY_DRIVER=ON -DSDL_KMSDRM_SHARED=OFF \
    -DSDL_ALSA=ON -DSDL_OPENGLES=ON ...
define SDL3_FIX_KMSDRM_CHECK
    sed -i 's/if(NOT SDL_UNIX_CONSOLE_BUILD)/if(FALSE)/g' $(@D)/cmake/macros.cmake
endef
SDL3_POST_EXTRACT_HOOKS += SDL3_FIX_KMSDRM_CHECK
$(eval $(cmake-package))
```

---
 
## 6. NOTAS CRÍTICAS
 
> ⚠️ **WiFi en mmc1** (4021000), NO mmc2.
 
> ⚠️ **RTW88 como módulos** (`=m`), nunca built-in (`=y`).
 
> ⚠️ **U-Boot siempre en offset 8K** (`seek=16` con bs=512). Siempre MBR, nunca GPT.
 
> ⚠️ **Buildroot NO está en local** — solo en CI. No usar grep local sobre archivos de Buildroot.
 
> ⚠️ **Modificar yaml**: usar siempre scripts Python, nunca sed con caracteres especiales de Makefile.

> ⚠️ **Heredoc `<< 'EOF'` dentro de YAML**: rompe el parser YAML. Usar `printf` con `\n` en su lugar.
 
> ⚠️ **Botones X/Y invertidos** en joypad — BTN_NORTH y BTN_WEST intercambiados. Fix pendiente en DTS.
 
> ⚠️ **Arquitectura Mesa 25+**: ya no existe `panfrost_dri.so` ni `/usr/lib/dri/`. El driver es `libgallium-<version>.so` cargado via `dri_gbm.so`. No buscar `panfrost_dri.so`.

> ⚠️ **.so grandes**: `libgallium` sin strip pesa ~112MB, por encima del límite de GitHub (100MB). Siempre hacer `aarch64-linux-gnu-strip --strip-unneeded` antes de commitear. Resultado: ~23MB.

> ⚠️ **libgbm no visible en staging**: `rootfs_overlay` no es el sysroot. SDL3 cmake no ve los .so del overlay durante la compilación. Solución: step "Inject Mesa GBM into staging" en `build.yml`.

> ⚠️ **El linker del target busca en `/usr/lib64`** además de `/usr/lib`. Los .so deben estar en ambos o el sistema debe tener `ld.so.conf` configurado. Buildroot usa `/usr/lib` — verificar que no hay conflicto con `lib64`.
 
> ℹ️ **ssh armiga** → `~/.ssh/config` apunta a `10.73.154.130`, user root, key `~/.ssh/id_rsa`
 
> ℹ️ **Caché CI**: `dl/`, `ccache`, `output/host` (toolchain). Sin caché de `output/build/`.
 
> ℹ️ **Tiempo de build**: ~24 min (sin Mesa3D en CI). Indicador de build sano.

> ℹ️ **Amiberry**: versión objetivo **8.2.0** (no 8.1.6 — nueva release junio 2026).
