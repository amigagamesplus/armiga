# ARMIGA — Kernel update process

> Philosophy: always use the latest stable release of the official vanilla kernel (kernel.org), verified on real hardware before committing.

---

## Flow summary

```
1. Download the official tarball from kernel.org
2. Verify the SHA256 checksum
3. Build with build_kernel.sh (changing KERNEL_VERSION)
4. Build the joypad driver (armiga-joypad, done by build-kernel.yml)
5. Regenerate modules.dep
6. Test on real hardware
7. Commit
```

---

## Prerequisites

- Cross-compiler installed locally: `aarch64-linux-gnu-gcc`
- `depmod` available locally (`kmod` package)
- SSH access to the device: `ssh root@10.212.82.130`
- SD card at `/dev/sdX` for testing before committing

Check the toolchain:
```bash
aarch64-linux-gnu-gcc --version
```

---

## Step 1 — Download and verify the tarball

```bash
cd /run/media/vince/samsung/armiga/board/armiga/linux

# Download (-c allows resuming if the transfer is interrupted)
wget -c https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-X.Y.Z.tar.xz

# Verify the checksum against the official one
sha256sum linux-X.Y.Z.tar.xz
wget -qO- https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc | grep linux-X.Y.Z.tar.xz
```

Both hashes must match exactly. Do not continue if they differ.

---

## Step 2 — Update the version in build_kernel.sh

```bash
sed -i 's/KERNEL_VERSION="X.Y.OLD"/KERNEL_VERSION="X.Y.NEW"/' \
    /run/media/vince/samsung/armiga/board/armiga/linux/build_kernel.sh
```

Verify:
```bash
grep KERNEL_VERSION /run/media/vince/samsung/armiga/board/armiga/linux/build_kernel.sh
```

---

## Step 3 — Build the kernel

```bash
cd /run/media/vince/samsung/armiga/board/armiga/linux
bash build_kernel.sh 2>&1 | tee build_kernel_X.Y.Z.log
```

The script automatically:
- Extracts the sources if the directory does not exist
- Applies all the patches in `patches/`
- Copies the custom DTS files and adds them to the allwinner Makefile
- Adapts the `.config` with `adapt_config.sh`
- Builds `Image`, the DTBs and the modules
- Copies `Image` and `dtb.img` to `board/armiga/bootloader/`
- Installs the modules into `modules_out/`

⚠️ If a patch fails, the script aborts. See the "Troubleshooting" section at the end.

---

## Step 4 — Joypad driver (armiga-joypad)

The driver is no longer vendored in this repo: it lives in `amigagamesplus/armiga-joypad`. `build-kernel.yml` clones it (input `joypad_ref`, default `main`; accepts a branch, tag or SHA) and builds it against the new kernel sources, so nothing has to be built by hand. `armiga-joypad.ko` ends up inside `modules-X.Y.tar.gz`, and the exact SHA used is recorded in `BUILD_INFO.txt` (`joypad_ref=`).

⚠️ The module needs the `joypad_input_g` symbol, exported by the patched `adc-keys` driver (patch `0004`). If that patch stops applying, the module cannot resolve the symbol and will not load. Kernel API changes (PWM, `MODULE_IMPORT_NS`) can make the driver fail to build: the failure shows up in the `Build armiga-joypad module` step of the workflow and is fixed in `amigagamesplus/armiga-joypad`.

The original driver (folder `board/armiga/linux/rocknix-joypad`) was removed in v1.0.20; it is still available in the git history.

---

## Step 5 — Prepare the rootfs_overlay

```bash
NEW_VERSION="X.Y.Z"
OLD_VERSION="X.Y.OLD"
OVERLAY=/run/media/vince/samsung/armiga/board/armiga/rootfs_overlay
MODULES_OUT=/run/media/vince/samsung/armiga/board/armiga/linux/modules_out

# Remove symlinks that point to local paths (they do not exist on the device)
rm -f ${MODULES_OUT}/lib/modules/${NEW_VERSION}-armiga/build
rm -f ${MODULES_OUT}/lib/modules/${NEW_VERSION}-armiga/source

# armiga-joypad.ko already comes in modules-X.Y.tar.gz (built by build-kernel.yml): it is not copied separately

# Remove the modules of the previous version from the overlay
rm -rf ${OVERLAY}/lib/modules/${OLD_VERSION}-armiga

# Copy the new modules to the overlay
cp -r ${MODULES_OUT}/lib/modules/${NEW_VERSION}-armiga \
      ${OVERLAY}/lib/modules/

# Regenerate modules.dep (required for modprobe to work)
sudo depmod -a -b ${OVERLAY} ${NEW_VERSION}-armiga

# Update KERNEL in the bootloader directory (U-Boot looks for KERNEL, not Image)
cp /run/media/vince/samsung/armiga/board/armiga/bootloader/Image \
   /run/media/vince/samsung/armiga/board/armiga/bootloader/KERNEL
```

---

## Step 6 — Test on real hardware BEFORE committing

Mount the SD card and copy the new binaries:

```bash
sudo mkdir -p /mnt/armiga_boot /mnt/armiga_root

# Boot partition (p1)
sudo mount /dev/sdX1 /mnt/armiga_boot
sudo cp /run/media/vince/samsung/armiga/board/armiga/bootloader/KERNEL /mnt/armiga_boot/KERNEL
sudo cp /run/media/vince/samsung/armiga/board/armiga/bootloader/dtb.img /mnt/armiga_boot/dtb.img
sudo sync
sudo umount /mnt/armiga_boot

# Rootfs partition (p2)
sudo mount /dev/sdX2 /mnt/armiga_root

# Remove the old modules and copy the new ones
sudo rm -rf /mnt/armiga_root/lib/modules/${OLD_VERSION}-armiga
sudo cp -r /run/media/vince/samsung/armiga/board/armiga/linux/modules_out/lib/modules/${NEW_VERSION}-armiga \
           /mnt/armiga_root/lib/modules/

# Regenerate modules.dep on the SD card
sudo depmod -a -b /mnt/armiga_root ${NEW_VERSION}-armiga

sudo sync
sudo umount /mnt/armiga_root
```

**Critical DTB check BEFORE flashing:**

```bash
dtc -I dtb -O dts \
    /run/media/vince/samsung/armiga/board/armiga/bootloader/dtb.img \
    2>/dev/null | grep -E "dpad-hat|adc-scale|adc-deadzone"
```

The output must contain exactly these 6 lines — no more, no fewer:

```
button-adc-scale = <0x02>;
button-adc-deadzone = <0x80>;
        rocknix,dpad-hat = <0x01>;
        rocknix,dpad-hat = <0x02>;
        rocknix,dpad-hat = <0x03>;
        rocknix,dpad-hat = <0x04>;
```

If any of them is missing, the D-pad will not work as a hat. Do not continue until the check passes.

Boot the device and verify:

```bash
# Kernel version
ssh root@10.212.82.130 "uname -r"
# Must return: X.Y.Z-armiga

# WiFi (critical)
ssh root@10.212.82.130 "lsmod | grep rtw"
# Must show: rtw88_core, rtw88_8821c, rtw88_sdio, rtw88_8821cs

# Joypad driver (loaded by S15modules at boot)
ssh root@10.212.82.130 "lsmod | grep joypad"
# Must show: armiga_joypad

# Check the D-pad as a hat (critical)
ssh root@10.212.82.130 "evtest /dev/input/event3"
# The "Supported events" section must list:
#   Event code 16 (ABS_HAT0X)
#   Event code 17 (ABS_HAT0Y)
# If BTN_DPAD_UP/DOWN/LEFT/RIGHT appear instead of ABS_HAT0X/Y,
# the DTB does not have the correct sw1-sw4 subnodes.
```

Do not commit until all the checks pass.

---

## Step 7 — Commit

```bash
cd /run/media/vince/samsung/armiga

git add board/armiga/bootloader/Image \
        board/armiga/bootloader/KERNEL \
        board/armiga/bootloader/dtb.img \
        board/armiga/rootfs_overlay/lib/modules/ \
        board/armiga/linux/build_kernel.sh

git commit -m "kernel: update from X.Y.OLD to X.Y.NEW

- Built with aarch64-linux-gnu-gcc $(aarch64-linux-gnu-gcc --version | head -1 | awk '{print $4}')
- All patches apply cleanly
- RTW88 verified on hardware
- armiga-joypad built against X.Y.NEW (joypad_ref from BUILD_INFO.txt)
- depmod regenerated
- uname -r: X.Y.NEW-armiga"

git push
```

---

## Troubleshooting

### A patch does not apply cleanly

Compare the patch context with the code of the new version:
```bash
patch --dry-run -p1 < board/armiga/linux/patches/NNNN-name.patch
```

If the failure is minor (shifted line numbers), `patch` usually resolves it on its own with `-p1`. If the failure is real, the patch has to be adapted manually to the new code.

For reference, the upstream project maintains equivalent patches for the H700 in its tree — useful to see how the same change was solved in more recent versions.

### The joypad driver does not build

Look at the `Build armiga-joypad module` step of the `build-kernel.yml` workflow. Common causes: a kernel API change (PWM, `MODULE_IMPORT_NS`) or an unresolved `joypad_input_g` because patch `0004` does not apply. The driver is fixed in `amigagamesplus/armiga-joypad`, not in this repo.

### modprobe: module not found in modules.dep

`depmod` has not been run. See Step 5. Buildroot runs it automatically in the CI build, but in manual tests it has to be run explicitly.

### The D-pad shows as BTN_DPAD_UP/DOWN/LEFT/RIGHT instead of ABS_HAT0X/Y

The compiled DTB does not have the `sw1-sw4` subnodes with `rocknix,dpad-hat`. Most likely cause: the source DTS in the repo (`board/armiga/linux/dts/`) does not have those subnodes, and `build_kernel.sh` copied it into the kernel tree, overwriting the one that worked.

Check the DTB:
```bash
dtc -I dtb -O dts board/armiga/bootloader/dtb.img 2>/dev/null | grep "dpad-hat"
```

If it returns nothing, the source DTS in the repo is incomplete. The correct subnodes are:
```dts
sw1 {
    gpios = <&pio 0 6 GPIO_ACTIVE_LOW>;
    label = "GPIO DPAD-UP";
    linux,code = <BTN_DPAD_UP>;
    rocknix,dpad-hat = <1>;
    gpio-active-low;
};
sw2 {
    gpios = <&pio 4 0 GPIO_ACTIVE_LOW>;
    label = "GPIO DPAD-DOWN";
    linux,code = <BTN_DPAD_DOWN>;
    rocknix,dpad-hat = <2>;
    gpio-active-low;
};
sw3 {
    gpios = <&pio 0 8 GPIO_ACTIVE_LOW>;
    label = "GPIO DPAD-LEFT";
    linux,code = <BTN_DPAD_LEFT>;
    rocknix,dpad-hat = <3>;
    gpio-active-low;
};
sw4 {
    gpios = <&pio 0 9 GPIO_ACTIVE_LOW>;
    label = "GPIO DPAD-RIGHT";
    linux,code = <BTN_DPAD_RIGHT>;
    rocknix,dpad-hat = <4>;
    gpio-active-low;
};
```

Add them inside the `&joypad { }` block of the DTS, rebuild the DTB and check again.

⚠️ **Lesson learned:** any change to the DTS must always be made in `board/armiga/linux/dts/` first. Never edit directly in the kernel tree (`linux-X.Y.Z/arch/arm64/boot/dts/allwinner/`) without syncing the repo source.

### The gamepad does not show up in evtest

Check that the `.ko` was built against the right kernel version:
```bash
ssh root@10.212.82.130 "modinfo /lib/modules/$(uname -r)/armiga-joypad.ko | grep vermagic"
```
The `vermagic` must match `uname -r`.
