# Armiga — RetroArch and PUAE2021 Configuration System

**Date:** 2026-08-21

Documents how RetroArch/PUAE2021 config files actually work in Armiga, and the workflow to modify them efficiently.

---

## 1. The three files involved

| File | Location | Role |
|---|---|---|
| `retroarch.cfg.template` | repo: `board/armiga/rootfs_overlay/etc/retroarch/retroarch.cfg.template` | Versioned master template. Initial seed for `retroarch.cfg` on the device. |
| `retroarch.cfg` | device: `/media/amiga_data/retroarch/retroarch.cfg` | **Live** config, editable by the user and by RetroArch itself (rewritten in full on exit). |
| `armiga.cfg` | repo: `board/armiga/rootfs_overlay/etc/retroarch/armiga.cfg` | Fixed keys, always forced via `--appendconfig` on every launch. Immutable regardless of `retroarch.cfg`. Not versioned, ships with the system image (not in `amiga_data`). |
| `PUAE 2021.opt` | repo: `board/armiga/rootfs_overlay/etc/retroarch/PUAE 2021.opt` → device: `/media/amiga_data/retroarch/config/PUAE 2021/PUAE 2021.opt` | PUAE2021 core options. Same versioning pattern as `retroarch.cfg.template` since 2026-08-21. |

### Launch wrapper (`/usr/bin/retroarch`)

```sh
/usr/bin/retroarch.real \
  --config /media/amiga_data/retroarch/retroarch.cfg \
  --appendconfig /etc/retroarch/armiga.cfg \
  "$@"
```

`armiga.cfg` is applied **on top of** `retroarch.cfg` on every launch — any key defined there always wins, even if the user changes it from the RetroArch menu.

---

## 2. Versioning and sync-to-existing-devices mechanism

Both `retroarch.cfg.template` and `PUAE 2021.opt` carry a first line:

```
# ARMIGA_CFG_VERSION=N
```

In `S40partitions` (boot), for each of the two files:

```sh
TEMPLATE_VER=$(grep "^# ARMIGA_CFG_VERSION=" <repo_template> | cut -d= -f2)
INSTALLED_VER=$(grep "^# ARMIGA_CFG_VERSION=" <file_in_amiga_data> 2>/dev/null | cut -d= -f2)
if [ ! -f <file_in_amiga_data> ] || [ "$TEMPLATE_VER" -gt "$INSTALLED_VER" ]; then
    cp <repo_template> <file_in_amiga_data>
fi
```

**Important — this is destructive:** if you bump the template version, the entire file on the device gets overwritten with the repo's version on the next boot. There's no per-key merge; it's all-or-nothing. This is why the sync workflow (section 3) always goes device → repo before bumping the version, never the other way around without going through it.

This mechanism runs in two places in `S40partitions`:
- First boot after expanding the `amiga_data` partition (initial format).
- Every normal boot, as part of self-repair if the file is missing or its version is behind.

---

## 3. Agile workflow (live editing → repo)

### 3.1. Edit on the device

Change whatever parameters you want from RetroArch's own menu (Settings, PUAE2021 core options, etc.) and save. RetroArch rewrites the entire `retroarch.cfg` on exit; `PUAE 2021.opt` is saved when core options are applied.

### 3.2. Bring the changes into the repo: `tools/retroarch-sync.sh`

```bash
# Diff only, doesn't touch the repo
./tools/retroarch-sync.sh root@<IP>

# Apply: overwrites the full template and bumps ARMIGA_CFG_VERSION by +1
./tools/retroarch-sync.sh root@<IP> --apply
```

What it does:
1. Downloads `retroarch.cfg` and `PUAE 2021.opt` from the device over SSH (`cat | ssh` — no scp, Dropbear has no sftp-server).
2. Compares them (ignoring the version line) against the repo files and shows the diff.
3. With `--apply`: writes the full device file into the repo, with the version header incremented by +1.

### 3.3. Expected noise in the diff

RetroArch rewrites the entire `.cfg` with **every** key from its current version, including ones you never touched, at their default value. If time has passed since the template was generated, the diff will show many irrelevant new lines (`input_playerN_hold_*`, `video_hdr_*`, etc.) mixed in with your real changes. This is harmless — they're legitimate defaults for that RetroArch version, not corruption. Once the template is rebaselined with `--apply`, subsequent diffs go back to being small and readable as long as the bundled RetroArch version doesn't change again.

### 3.4. After `--apply`

```bash
git status --short
git add -A
git commit -m "Sync retroarch.cfg / PUAE 2021.opt templates with live device config"
git push origin <branch>
```

Confirm both version headers were bumped correctly:

```bash
grep "^# ARMIGA_CFG_VERSION=" board/armiga/rootfs_overlay/etc/retroarch/retroarch.cfg.template
grep "^# ARMIGA_CFG_VERSION=" "board/armiga/rootfs_overlay/etc/retroarch/PUAE 2021.opt"
```

With this, any existing device that boots this build will receive the updated file (its local copy will be overwritten, see the warning in section 2).

---

## 4. When to use `armiga.cfg` instead of `retroarch.cfg.template`

- **`armiga.cfg`** (always forced, not versioned, ships with the image): for keys that should **never** be changeable from the device — audio/video drivers, system font paths, default menu theme. Editing it requires no version bump or sync: it ships with every build.
- **`retroarch.cfg.template`**: for starting values the user is free to change afterward (and will, via the menu itself). Requires a version bump to reach existing devices.

---

## 5. Known limitation

Versioning is all-or-nothing per file — there's no selective per-key merge between what the new template brings and what the user already personalized on their own device. If a user manually changed `audio_volume` on their unit and the template version is later bumped for an unrelated reason, their custom value is lost. No mitigation is implemented for this yet.
