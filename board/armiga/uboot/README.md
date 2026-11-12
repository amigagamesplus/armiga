# U-Boot build inputs

Inputs used by `.github/workflows/build-uboot.yml` to build `u-boot-sunxi-with-spl.bin` for the Allwinner H700 (LPDDR4) from mainline U-Boot `v2026.01`.

Copied unchanged from the ROCKNIX distribution (https://github.com/ROCKNIX/distribution, branch `next`, `projects/ROCKNIX/devices/H700/packages/u-boot-DDR4/`, fetched 2026-11-11). U-Boot is GPL-2.0+; the patches follow the U-Boot license.

| File | Purpose |
|---|---|
| `configs/anbernic_rg35xx_h700_lpddr4_defconfig` | H700 LPDDR4 defconfig (same DRAM parameters as mainline `anbernic_rg35xx_h700_defconfig`, plus `AXP_DISABLE_BOOT_ON_POWERON`, `BOOTDELAY=0`, `EFI_LOADER=n` and a status LED) |
| `patches/v2026.01/0001-Update-dram_sun50i_h616.c.patch` | Fixes the LPDDR3 `tpr6` shift (no effect on LPDDR4, kept to match the upstream build) |
| `patches/v2026.01/0002-sunxi-axp717-charge-only-boot.patch` | AXP717 charge-only boot: with the console off, plugging in USB-C only charges the battery |

Patches live in one directory per U-Boot version (`patches/<uboot_version>/`); a build for a version without its directory fails on purpose. `patches/v2027.01-rc2/0002-...` is the same patch with the Kconfig hunk adapted (`help` instead of `---help---`); `0001` is already upstream in that version.
