# ARMIGA kernel

ARMIGA ships its own kernel. It is built from the vanilla source tree on kernel.org (currently 7.3-rc7, reported as `7.3.0-rc7-armiga`) with the patches and device trees in this directory. No third-party kernel is used.

## Contents

| Path | Purpose |
|---|---|
| `linux.aarch64.conf` | Kernel configuration (based on the config of the H700 reference distribution) |
| `patches/` | Patches applied to the kernel source before building, in filename order (see `patches/README.md`) |
| `dts/` | Device trees; copied over the kernel tree before building |
| `build_kernel.sh` | Local build script (see Local build) |
| `KERNEL-UPDATE.md` | Step-by-step guide to update the kernel version |

## Devices

`dts/` holds the device trees for the Anbernic RG40XX H, RG40XX H (v2 panel), RG35XX H and RG35XX H (rev6 panel). The RG40XX H is the tested device; the RG35XX variants are experimental. The v2-panel tree includes the RG40XX H one, so it inherits its changes.

## Gamepad driver

The gamepad driver is not in this directory. It lives in its own repository, [amigagamesplus/armiga-joypad](https://github.com/amigagamesplus/armiga-joypad):

- `build-kernel.yml` clones it (input `joypad_ref`, default `main`), builds it against the new kernel and ships `armiga-joypad.ko` inside the modules tarball. `S15modules` loads it at boot.
- The device tree node is `armiga-singleadc-joypad` (patch `0140`). On the RG40XX H the X and Y button pins are swapped in `dts/sun50i-h700-anbernic-rg40xx-h.dts`.
- It requires patch `0004` (`adc-keys`), which exports the `joypad_input_g` symbol used to report the MODE button. Without that patch the module cannot resolve the symbol.

## Building

CI is the reference build: the `build-kernel.yml` workflow (`workflow_dispatch`, inputs `kernel_version` and `joypad_ref`) produces `KERNEL`, the `dtb*.img` files, the modules tarball and `BUILD_INFO.txt`.

The kernel is consumed prebuilt: Buildroot does not compile it. The artifacts are copied by hand into `board/armiga/bootloader/` and `board/armiga/rootfs_overlay/lib/modules/`. See `KERNEL-UPDATE.md` and `../bootloader/README.md`.

## Local build

`build_kernel.sh` downloads the kernel, applies the patches, copies the device trees and builds. It is not kept in sync with CI: `KERNEL_VERSION` is hardcoded inside the script (currently 7.1.8) and it does not build the gamepad driver.

## Rules

Every device tree change goes in `dts/` first. Never edit the files in the kernel source tree directly without syncing them back here.
