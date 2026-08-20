# armiga — User Guide

Welcome to armiga, your Commodore Amiga emulation console.

## Supported devices

armiga ships configured for the **Anbernic RG40XX H** by default. It also supports the **Anbernic RG35XX H**, but you must manually swap one file after flashing:

1. Flash armiga normally (Rufus/balenaEtcher) to your SD card.
2. Open the `boot` partition on the SD card from a computer.
3. Delete (or rename) `dtb.img`.
4. Rename `dtb-rg35xx-h.img` to `dtb.img`.
5. Eject the SD card and boot your RG35XX H.

> If you skip this step on an RG35XX H, the device may not boot or some hardware (display, buttons) may not work correctly.

## Controls

| Physical button                       | Function                                   |
| -------------------------------------- | ------------------------------------------- |
| **B**                                  | Confirm / Select                            |
| **A**                                  | Cancel / Go back                            |
| **X**                                  | Delete (backup screen only)                 |
| **D-Pad**                              | Navigate menus                              |
| **L1**                                 | Switch language (Español/English)           |
| **SELECT + START + L1** (hold 3s)      | Developer mode                              |

## Main menu

- **Amiga Catalog** — access your game collection
- **System update** — check for and install new armiga versions
- **System diagnostics** — technical device info (CPU, memory, network, versions)
- **Settings** — all device settings
- **Power off device**

## Settings

### Wireless network

Enter your network name (SSID) and password with the on-screen keyboard. Use the D-Pad to move between keys, B to select a letter, START for uppercase/lowercase/numbers.

### Backup

- **Create**: generates a backup of your configuration, saved games, and RetroArch settings. Up to 3 backups are kept; creating a fourth automatically deletes the oldest one.
- **Restore**: pick a backup from the list and press B to restore it (the device will reboot). Press X on a backup to delete it.

> Backups do not include ROMs or kickstarts — only your configuration and progress.

### Analog stick RGB LEDs

Adjust the color and brightness of both analog sticks' LED rings independently.

### Timezone

Select your timezone from a list of cities. Affects the time shown on the device.

### Screensaver

Configure how long the device must be idle before the screen dims, and to what brightness percentage. Any button press restores brightness instantly. While the screen is dimmed, the device also reduces battery consumption.

### Screen brightness

Adjust overall screen brightness with D-Pad up/down, in 5% steps.

### SSH

Enable or disable remote SSH access to the device. Enabled by default; if you won't use it, you can disable it for security.

### Factory reset

Erases all your custom settings (WiFi, preferences, backups) and resets the device to its original state. **Your ROMs, kickstarts, save games, and save states are not affected.**

## System updates

armiga checks whether a newer version is available. If there is, you can download and install it directly from the menu — the download happens in the background and the device will reboot automatically when finished. Do not power off the device during an update.

## Having trouble?

- **The device doesn't boot after an update**: armiga has a safety system that automatically reverts to the previous version if it detects repeated boot failures, with no action required from you.
- **Forgot your saved WiFi password**: you can re-enter it in Settings → Wireless network at any time.
- **Want to start fresh**: use Factory reset from Settings.
