# Bootloader binaries

This directory contains precompiled binaries used to boot the device:

- `KERNEL`      — Linux kernel (ARM64 Image), built from kernel.org vanilla source with Armiga's patches and DTS applied.
- `dtb.img`     — Device Tree Blob for Anbernic RG40XX H (Allwinner H700).
- `dtb-rg35xx-h.img` — Device Tree Blob for Anbernic RG35XX H (Allwinner H700). Users swap this manually for `dtb.img` if running on that hardware — see USER-GUIDE.md.
- `dtb-rg35xx-h-rev6.img` — Device Tree Blob for Anbernic RG35XX H units with the "rev6" display panel revision. Only needed if the standard `dtb-rg35xx-h.img` produces a garbled/blank screen — see USER-GUIDE.md.
- `dtb-rg40xx-h-v2.img` — Legacy Device Tree Blob for the RG40XX H "v2" (DDR3) variant. Not supported (support was discontinued); kept in the repo as-is and not updated when the kernel is rebuilt.
- `u-boot.bin`  — U-Boot SPL + proper (`u-boot-sunxi-with-spl.bin`), written at raw offset 8K (sector 16) of the image by `board/armiga/post-image.sh`. Currently U-Boot 2027.01-rc2 with TF-A v2.14.0, built by `.github/workflows/build-uboot.yml` (LPDDR4 defconfig, with AXP717 charge-only boot support); see `board/armiga/uboot/README.md`.

## How the kernel reaches the image

`build-kernel.yml` does **not** feed `build.yml`. The kernel is consumed as a prebuilt binary (`BR2_LINUX_KERNEL=n` in `armiga_defconfig`) from fixed paths in the repo:

| Artifact | Destination |
|---|---|
| `KERNEL` | `board/armiga/bootloader/KERNEL` |
| `dtb*.img` | `board/armiga/bootloader/` (only if they differ from the committed ones) |
| `modules-X.Y.tar.gz` (extracted) | `board/armiga/rootfs_overlay/lib/modules/<real kernel version>/` |

## Regenerating KERNEL / DTBs / modules

Built via the `build-kernel.yml` GitHub Actions workflow (`workflow_dispatch`, inputs `kernel_version` and `joypad_ref`; check that "Use workflow from" points to the right branch), which:

1. Downloads the specified kernel version from kernel.org
2. Applies all patches in `board/armiga/linux/patches/`
3. Copies the custom DTS files and registers them in the DTS Makefile
4. Compiles `Image dtbs modules`
5. Clones `amigagamesplus/armiga-joypad` (input `joypad_ref`, default `main`) and compiles the `armiga-joypad` driver against the new kernel
6. Packages everything as a single artifact: `KERNEL`, the `dtb*.img` files, the in-tree modules tarball (`modules-X.Y.tar.gz`, which already includes the joypad `.ko`), a standalone copy of `armiga-joypad.ko` and `BUILD_INFO.txt` (which records the exact `joypad_ref` SHA)

After a successful run, download the artifact and copy by hand:

1. `KERNEL` → `board/armiga/bootloader/KERNEL`.
2. DTBs: compare with `md5sum` against the committed ones and copy only those that changed.
3. Modules: the directory name under `lib/modules/` is `VERSION.PATCHLEVEL.SUBLEVEL` + `EXTRAVERSION` from the kernel Makefile (e.g. `7.3-rc6` → `7.3.0-rc6-armiga`), not the workflow input. Check it with `tar tzf modules-X.Y.tar.gz | head`, delete the `build` and `source` symlinks from the extracted directory, remove the old version directory from `rootfs_overlay/lib/modules/` and copy the new one.
4. Check the DTB: `dtc -I dtb -O dts board/armiga/bootloader/dtb.img 2>/dev/null | grep -E "dpad-hat|adc-scale|adc-deadzone" | wc -l` must print exactly `6`.
5. Update the kernel version in the main `README.md`.

For local kernel development/iteration, `board/armiga/linux/build_kernel.sh` performs the same steps without going through CI.

## Updating u-boot.bin

U-Boot is built from mainline U-Boot and TF-A by the `Build U-Boot (H700)` workflow (`.github/workflows/build-uboot.yml`, listed in Actions from `main`). Armiga patches live in `board/armiga/uboot/patches/<uboot_version>/`; a version without its directory fails on purpose. See `board/armiga/uboot/README.md`.

1. Actions → `Build U-Boot (H700)` → Run workflow, with `uboot_version` (e.g. `v2027.01-rc2`), `tfa_version` (`v2.12.0` or newer: older TF-A does not support the AXP717), `uboot_defconfig` (`anbernic_rg35xx_h700_lpddr4_defconfig`) and `device_tree`.
2. In the log check that every `Applying …` patch succeeds, then download the `u-boot-sunxi-with-spl-<version>` artifact to its own folder.
3. Copy it into the repo:

```bash
   cp <folder>/u-boot-sunxi-with-spl.bin board/armiga/bootloader/u-boot.bin
```

   The console uses LPDDR4; a U-Boot built for the wrong RAM type will not boot. The previous precompiled U-Boot from the upstream H700 distribution (2026.01) remains in git history as a rollback.
4. `strings board/armiga/bootloader/u-boot.bin | grep -E "^U-Boot 20"` shows the version. A bad U-Boot leaves the device unbootable until the SD card is reflashed from a PC, so verify the first boot on hardware before publishing.
