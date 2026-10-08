# Bootloader binaries

This directory contains precompiled binaries used to boot the device:

- `KERNEL`      — Linux kernel (ARM64 Image), built from kernel.org vanilla source with Armiga's patches and DTS applied.
- `dtb.img`     — Device Tree Blob for Anbernic RG40XX H (Allwinner H700).
- `dtb-rg35xx-h.img` — Device Tree Blob for Anbernic RG35XX H (Allwinner H700). Users swap this manually for `dtb.img` if running on that hardware — see USER-GUIDE.md.
- `dtb-rg35xx-h-rev6.img` — Device Tree Blob for Anbernic RG35XX H units with the "rev6" display panel revision. Only needed if the standard `dtb-rg35xx-h.img` produces a garbled/blank screen — see USER-GUIDE.md.
- `dtb-rg40xx-h-v2.img` — Legacy Device Tree Blob for the RG40XX H "v2" (DDR3) variant. Not supported (support was discontinued); kept in the repo as-is and not updated when the kernel is rebuilt.
- `u-boot.bin`  — U-Boot SPL + proper (`u-boot-sunxi-with-spl.bin`), written at raw offset 8K (sector 16) of the image by `board/armiga/post-image.sh`. Currently U-Boot 2026.01, precompiled by the upstream H700 distribution, DDR4 variant, with AXP717 charge-only boot support.

## How the kernel reaches the image

`build-kernel.yml` does **not** feed `build.yml`. The kernel is consumed as a prebuilt binary (`BR2_LINUX_KERNEL=n` in `armiga_defconfig`) from fixed paths in the repo:

| Artifact | Destination |
|---|---|
| `KERNEL` | `board/armiga/bootloader/KERNEL` |
| `dtb*.img` | `board/armiga/bootloader/` (only if they differ from the committed ones) |
| `modules-X.Y.tar.gz` (extracted) | `board/armiga/rootfs_overlay/lib/modules/<real kernel version>/` |

## Regenerating KERNEL / DTBs / modules

Built via the `build-kernel.yml` GitHub Actions workflow (`workflow_dispatch`, input `kernel_version`; check that "Use workflow from" points to the right branch), which:

1. Downloads the specified kernel version from kernel.org
2. Applies all patches in `board/armiga/linux/patches/`
3. Copies the custom DTS files and registers them in the DTS Makefile
4. Compiles `Image dtbs modules`
5. Compiles the vendored joypad driver (`board/armiga/linux/rocknix-joypad/`)
6. Packages everything as a single artifact: `KERNEL`, the `dtb*.img` files, the in-tree modules tarball (`modules-X.Y.tar.gz`, which already includes the joypad `.ko`), a standalone copy of `rocknix-singleadc-joypad.ko` and `BUILD_INFO.txt`

After a successful run, download the artifact and copy by hand:

1. `KERNEL` → `board/armiga/bootloader/KERNEL`.
2. DTBs: compare with `md5sum` against the committed ones and copy only those that changed.
3. Modules: the directory name under `lib/modules/` is `VERSION.PATCHLEVEL.SUBLEVEL` + `EXTRAVERSION` from the kernel Makefile (e.g. `7.3-rc6` → `7.3.0-rc6-armiga`), not the workflow input. Check it with `tar tzf modules-X.Y.tar.gz | head`, delete the `build` and `source` symlinks from the extracted directory, remove the old version directory from `rootfs_overlay/lib/modules/` and copy the new one.
4. Check the DTB: `dtc -I dtb -O dts board/armiga/bootloader/dtb.img 2>/dev/null | grep -E "dpad-hat|adc-scale|adc-deadzone" | wc -l` must print exactly `6`.
5. Update the kernel version in the main `README.md`.

For local kernel development/iteration, `board/armiga/linux/build_kernel.sh` performs the same steps without going through CI.

## Updating u-boot.bin

The upstream H700 distribution does not publish U-Boot binaries in its repository (only sources and patches); the compiled ones ship inside its images, under `/usr/share/bootloader/` of the `SYSTEM` squashfs.

1. Download a recent upstream H700 nightly (`<name>-H700.aarch64-<date>-DDR4.img.gz`), check that its changelog includes the change you want, and decompress it.
2. Extract the binary:

```bash
   IMG=<name>-H700.aarch64-<date>-DDR4.img
   L=$(sudo losetup -fP --show $IMG)
   sudo mkdir -p /mnt/rx && sudo mount -o ro ${L}p1 /mnt/rx
   unsquashfs -q -d /tmp/sq /mnt/rx/SYSTEM usr/share/bootloader
   sudo umount /mnt/rx; sudo losetup -d $L
   cp /tmp/sq/usr/share/bootloader/H700_DDR4_u-boot-sunxi-with-spl.bin board/armiga/bootloader/u-boot.bin
```

3. Choose the variant by the DRAM voltage of the target console, as the upstream `update.sh` does: `vdd-dram` at 1.2 V → `H700_DDR3_…`, at 1.1 V → `H700_DDR4_…`.

```bash
   for r in /sys/class/regulator/regulator.*/; do [ "$(cat $r/name)" = "vdd-dram" ] && echo "$r $(cat $r/microvolts)"; done
```

   A U-Boot built for the wrong RAM type will not boot.
4. `strings board/armiga/bootloader/u-boot.bin | grep -E "^U-Boot 20"` shows the version. A bad U-Boot leaves the device unbootable until the SD card is reflashed from a PC, so verify the first boot on hardware before publishing.
