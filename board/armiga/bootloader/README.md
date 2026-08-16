# Bootloader binaries

This directory contains precompiled binaries used to boot the device:

- `KERNEL`      — Linux kernel (ARM64 Image), built from kernel.org vanilla source with Armiga's patches and DTS applied.
- `dtb.img`     — Device Tree Blob for Anbernic RG40XX H (Allwinner H700).
- `dtb-rg35xx-h.img` — Device Tree Blob for Anbernic RG35XX H (Allwinner H700). Users swap this manually for `dtb.img` if running on that hardware — see USER-GUIDE.md.
- `u-boot.bin`  — U-Boot SPL + proper, written at raw offset 8K.

## Regenerating KERNEL / dtb.img / dtb-rg35xx-h.img

These are built via the `build-kernel.yml` GitHub Actions workflow (`workflow_dispatch`, input `kernel_version`), which:

1. Downloads the specified kernel version from kernel.org
2. Applies all patches in `board/armiga/linux/patches/`
3. Copies the custom DTS files and registers them in the DTS Makefile
4. Compiles `Image dtbs modules`
5. Compiles the vendored joypad driver (`board/armiga/linux/rocknix-joypad/`)
6. Packages everything (`KERNEL`, `dtb.img`, `dtb-rg35xx-h.img`, joypad `.ko`, in-tree modules tarball) as a single artifact

After a successful run, download the artifact and manually copy `KERNEL`, `dtb.img`, and `dtb-rg35xx-h.img` into this directory, then commit.

For local kernel development/iteration, `board/armiga/linux/build_kernel.sh` performs the same steps without going through CI.
