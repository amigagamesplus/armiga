# armiga — User Guide

Welcome to armiga, your Commodore Amiga emulation console!

This guide walks you through everything you can do with your device — from first boot to fine-tuning it exactly how you like it.

## Supported devices

armiga works out of the box on the **Anbernic RG40XX H** and **RG40XX V** — no extra steps needed.

It also runs experimentally on the **Anbernic RG35XX H**, but you'll need to swap one file after flashing:

1. Flash armiga normally (Rufus/balenaEtcher) to your SD card.
2. Open the `boot` partition on the SD card from a computer.
3. Delete (or rename) `dtb.img`.
4. Rename `dtb-rg35xx-h.img` to `dtb.img`.
5. Eject the SD card and boot your RG35XX H.

> If you skip this step on an RG35XX H, the device may not boot, or some hardware (display, buttons) may not work correctly.

### RG35XX H "rev6" panel variant

Some RG35XX H units use a different internal display panel. There's no way to tell which one your unit has without opening it up, so if your screen shows garbage, artifacts, or stays blank after the steps above, try this instead:

1. Power off the device (long-press power for ~10 seconds).
2. Open the `boot` partition again.
3. Delete (or rename) `dtb.img`.
4. Rename `dtb-rg35xx-h-rev6.img` to `dtb.img`.
5. Eject the SD card and boot your RG35XX H again.

### RG40XX H "v2" panel variant

Some RG40XX H units use a newer internal display panel revision ("v2"). There's no way to tell which one your unit has without opening it up, so if your screen shows garbage, artifacts, or stays blank on first boot:

1. Power off the device (long-press power for ~10 seconds).
2. Open the `boot` partition on the SD card from a computer.
3. Delete (or rename) `dtb.img`.
4. Rename `dtb-rg40xx-h-v2.img` to `dtb.img`.
5. Eject the SD card and boot your RG40XX H again.

## Controls

| Physical button                   | Function                           |
| ---------------------------------- | ----------------------------------- |
| **B**                               | Confirm / Select                    |
| **A**                               | Cancel / Go back                    |
| **X**                               | Delete (backup screen only)         |
| **D-Pad**                           | Navigate menus                      |
| **L1**                              | Switch language (Español/English)   |
| **SELECT + START + L1** (hold 3s)  | Developer mode                      |

Not sure which physical button does what on your controller? Head to **Settings → Controller Test** — see below.

## Main menu

- **Amiga Catalog** — access your game collection
- **System update** — check for and install new armiga versions
- **System diagnostics** — technical device info (CPU, memory, network, versions)
- **Settings** — all device settings
- **Power off device**

## Settings

### Wireless network

Enter your network name (SSID) and password with the on-screen keyboard. Use the D-Pad to move between keys, B to select a letter, and START to switch between uppercase, lowercase, and numbers.

### Bluetooth

Turn Bluetooth on or off and connect wireless controllers or headphones. When enabled, armiga shows a list of available devices nearby — select one and press B to pair. Once a device is connected, its name appears right next to the Bluetooth toggle so you always know what's paired.

### Performance profile

Choose how your device balances speed against battery life:

- **Maximum performance** — CPU and GPU always run at full speed. Best for demanding games, but drains the battery faster.
- **Balanced** — CPU adjusts automatically while GPU stays at full speed. A good middle ground for most games.
- **Battery saver** — CPU and GPU run at minimum speed. Ideal for older, less demanding games or squeezing out extra playtime.

You can switch profiles anytime — the change applies instantly, no reboot needed.

### Screen refresh rate (60Hz / 120Hz)

Some games and menus feel noticeably smoother at 120Hz. Toggle this on if you'd like a smoother picture, or leave it at 60Hz for a small battery-life bonus. If you notice display bugs during gameplay, try switching back to 60Hz.

### Samba network share (\\armiga)

Enable this to access your device's game/save folders directly from your computer over your local network, without unplugging the SD card. Once enabled, look for `\\armiga` from Windows (or the Samba/SMB equivalent on Mac/Linux).

### Controller Test

Not sure which physical button is A, B, X or Y on your specific controller? This screen lights up every button, stick, and trigger as you press it, so you can see exactly what's what — handy since button labels vary between controllers. Hold **L2** to test the rumble motor. Press **SELECT + START** together to exit.

### Backup

- **Create**: generates a backup of your configuration, saved games, and RetroArch settings. Up to 3 backups are kept; creating a fourth automatically deletes the oldest one.
- **Restore**: pick a backup from the list and press B to restore it (the device will reboot). Press X on a backup to delete it.

> Backups do not include ROMs or kickstarts — only your configuration and progress.

### Analog stick RGB LEDs

Adjust the color and brightness of both analog sticks' LED rings independently. Use L1/R1 for quick jumps or the D-Pad to fine-tune each value.

### Timezone

Select your timezone from a list of cities. Affects the time shown on the device.

### Screensaver

Configure how long the device must be idle before the screen dims, and to what brightness percentage. Any button press restores brightness instantly. While the screen is dimmed, the device also reduces battery consumption.

### Screen brightness

Adjust overall screen brightness with D-Pad up/down, in 5% steps.

### SSH

Enable or disable remote SSH access to the device. Enabled by default; if you won't use it, you can disable it for extra security.

### Factory reset

Erases all your custom settings (WiFi, preferences, backups) and resets the device to its original state. **Your ROMs, kickstarts, save games, and save states are not affected.**

## System updates

armiga checks whether a newer version is available. If there is, you can download and install it directly from the menu — the download happens in the background, and the device reboots automatically once it's done. Just don't power off the device mid-update.

## Having trouble?

- **The device doesn't boot after an update** — no need to panic. armiga automatically reverts to the previous version if it detects repeated boot failures, with no action required from you.
- **Forgot your saved WiFi password** — you can re-enter it anytime in Settings → Wireless network.
- **Not sure which button is which on your controller** — check Settings → Controller Test.
- **Want to start fresh** — use Factory reset from Settings.
