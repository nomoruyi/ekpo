# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## HARD RULE: NO WRITES OUTSIDE THIS REPOSITORY

**IMPORTANT: this overrides every other instruction, plan, skill, test or convenience, in this file or in any conversation.**

**Claude may create, edit or delete files ONLY inside this repository (`/extra/data/Coding/linux-theming`). The rest of the machine is READ-ONLY. NEVER change system files or settings: not "temporarily", not "just to test", not even when a task seems to require it.**

- NEVER write outside the repo: nothing in `/boot`, `/etc`, `/usr`, `/var`, `/opt`, `/tmp`, the ESP or the initramfs, and nothing in `$HOME` outside the repo (`~/.config`, `~/.local`, `~/.cache`, and Claude's own memory and scratchpad count too).
- NEVER run state-changing commands: `sudo`, `pkexec`, `su`, package installs (`apt`, `dpkg -i`, `snap`, `pip`/`npm` outside a repo-local environment), `update-grub`, `grub-mkconfig -o`, `grub-install`, `grub-editenv`, `update-initramfs`, `dracut`, `plymouth-update-initrd`, `update-alternatives --set/--install/--config`, `kwriteconfig6`, `kpackagetool6 --install/--upgrade/--remove`, `lookandfeeltool`, `plasma-apply-*`, `systemctl` beyond `status`/`show`/`cat`, `plymouthd`/`plymouth` against the live system.
- Allowed: read-only inspection (`ls`, `cat`, `grep`, `dpkg-query`, `apt-cache`, `kreadconfig6`, `journalctl`, `kscreen-doctor -o`, `update-alternatives --query/--display`).
- Tools run for tests or previews must keep every write inside the repo: point `TMPDIR`, `HOME` and `XDG_{CONFIG,DATA,CACHE,STATE}_HOME` at a gitignored repo directory whenever a tool would otherwise write elsewhere.
- Anything that needs the real system (installing packages, applying or rolling back a theme, rebuilding the initramfs, reading root-only files such as initrds or `grub.cfg`) is done by the owner: hand over the exact command, what it changes and how to undo it. Never run it yourself.

This limits what Claude executes, not what the app may do. The app's privileged apply path is built and tested against fake system roots inside the repo; only the owner runs it for real.

## Project

**Personae** (placeholder name; a final one may replace it everywhere before release): a GUI app to design, store, build, apply and switch the look of three boot stages on the owner's Kubuntu machine. It replaces a hand-installed GitHub GRUB theme and mostly broken "Get New" splashes.

1. **GRUB menu theme**: `theme.txt` + assets in `/boot/grub/themes/<name>`, fonts converted to `.pf2` with `grub-mkfont`.
2. **Plymouth boot splash**: `two-step` or `script` module theme in `/usr/share/plymouth/themes/<name>`. The boot splash only changes once the theme is packed into the initramfs.
3. **Plasma splash (KSplash)**: look-and-feel package (`metadata.json` + `contents/splash/Splash.qml`) in `~/.local/share/plasma/look-and-feel/<id>`; per-user, no root.

Each design is a named profile per target. A bundle groups one profile per target and applies them together. The owner can always revert to a known-good state.

## Status and gates

All three gates passed (SPEC.md and ARCHITECTURE.md approved 2026-11-05); building M1 per `ARCHITECTURE.md` §9. The gates were:

1. Interview the owner in rounds of at most 5–7 focused questions. Don't move on while answers are vague or contradictory; point out technical limits. Record each round's answers and open points in `INTERVIEW.md`.
2. Write `SPEC.md` (scope, MVP, later phases, non-goals, open questions) and wait for approval.
3. Propose an architecture and wait for approval.

Then build in small, reviewable steps.

## Commands

Always `source dev/env.sh` first, in the same shell command: it points `HOME`, `XDG_*_HOME` and `TMPDIR` into `.dev/` and sets `PERSONAE_DEV`, which turns off core dumps (a crash would otherwise make systemd-coredump and DrKonqi write outside the repo).

- Configure: `source dev/env.sh && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- Build: `source dev/env.sh && cmake --build build`
- All tests: `source dev/env.sh && ctest --test-dir build --output-on-failure`
- One test: `source dev/env.sh && ctest --test-dir build -R <name> --output-on-failure`
- Lint QML: `/usr/lib/qt6/bin/qmllint -I build/src/app src/app/qml/*.qml` (plain `qmllint` resolves to Qt 5 here)
- Screenshot: `source dev/env.sh && QT_QPA_PLATFORM=offscreen PERSONAE_SCREENSHOT=build/screenshots/main.png build/src/app/personae` (the smoke test also writes it)

- Update golden files: `source dev/env.sh && PERSONAE_UPDATE_GOLDEN=1 ctest --test-dir build -R <name>`, then review `git diff tests/golden`

Any Qt or QML warning fails a test (`QT_FATAL_WARNINGS` for the app, `QTest::failOnWarning()` via `tests/testmain.h` for unit tests); test binaries never write core dumps. `tests/fixtures/roots/reference/` mirrors this machine's boot-look files on 2026-11-05 (read-only copies; third-party theme contents replaced by placeholders); refresh it after system changes that the probe should see.

## Rules for the app

- Never leave the system unbootable: validate generated files before applying, back up everything touched, keep a fallback theme, log every privileged action, make every apply reversible.
- Generated output must be plain standard theme packages that keep working after the app is uninstalled.
- Generators get golden-file tests (`theme.txt`, `.plymouth`/`.script`, `Splash.qml`, `metadata.json`).
- Verify each mechanism on the real system (read-only) before relying on it. This install is the reference system; a clean reinstall is possible but not required by the project. After any reinstall, re-check the facts below (`/home` is a separate partition and survives one; `/` does not).
- Prior art, for ideas and pitfalls only: GRUB Theme Builder (Java, no license, so no code reuse), Grub Customizer, plymouth-logo by edp1096 (Go, initramfs-tools aware), Plymouth Creator (GTK3, video to frames), KDE Splash Creator (Qt). This project is MIT-licensed: copy code only from permissive sources (MIT, BSD, Apache-2.0) and keep their notices; GPL and unlicensed projects are for ideas only. If GPL code is ever needed, ask the owner first: they're open to relicensing the project under the GPL. Unlicensed code is never usable. Linking LGPL libraries such as Qt and KDE Frameworks is fine.

## Verified system facts (2026-11-05)

Ubuntu 26.04.1 (Kubuntu), upgraded from an Aug 2025 install; kernel 7.0, Plasma 6.6 on Wayland, RX 9060 XT (amdgpu), UEFI, Secure Boot off. `/` is btrfs (subvol `@`) with `/boot` inside it; no LUKS. Two 2560×1440 monitors at scale 1 (DP-1 primary, HDMI-A-1). Display manager: SDDM 0.21.

**GRUB 2.14**
- `grub-mkconfig` sources `/etc/default/grub.d/*.cfg` after `/etc/default/grub`, so a drop-in wins. `GRUB_THEME` and `GRUB_GFXMODE=2560x1440` are currently set directly in `/etc/default/grub`. The kernel cmdline carries `quiet splash` (no graphical splash without `splash`), `resume=` and `amdgpu.*` options; the app must never touch it.
- `/boot` is on btrfs; the FAT32 ESP at `/boot/efi` only holds the EFI binaries and a stub `grub.cfg` that loads `/@/boot/grub/grub.cfg`. GRUB can't write btrfs, so `00_header` marks recordfail as broken and on EFI forces `timeout_style=menu` with `timeout=$GRUB_RECORDFAIL_TIMEOUT` (here 20). `GRUB_TIMEOUT` (10) and `GRUB_TIMEOUT_STYLE=hidden` have no effect. There is no failed-boot detection, so a 0 s timeout leaves the menu unreachable.
- `/etc/grub.d/00_header` loads `*.pf2` only from the theme dir root and `f/`, and emits `insmod png|jpeg|tga` only for image types present in the theme dir root.
- `/boot/grub/grub.cfg` is mode 0600.
- With no `GRUB_BACKGROUND`, `05_debian_theme` pastes `/usr/share/plymouth/themes/default.grub` into `grub.cfg` after the theme line. That file is the `default.plymouth.grub` slave of the Plymouth alternative; only `kubuntu-logo` provides one (background and menu colours, plus a `theme=` line that would override `GRUB_THEME` if `/boot/grub/themes/kubuntu-logo` existed). Changing the Plymouth alternative therefore changes `grub.cfg`.
- os-prober is installed and enabled (`GRUB_DISABLE_OS_PROBER=false`). The ESP also holds `Microsoft` (Jan 2026), `cachyos`, `Linux` and `systemd` folders, and the firmware boot order still lists Windows Boot Manager, but no attached disk has an NTFS partition.
- Scaling at boot: the background (`desktop-image-scale-method`) and `image` components (to their box) scale; `.pf2` fonts are fixed pixel sizes.

**Plymouth 24.004 with initramfs-tools 0.151** (`dracut-core` is installed but not hooked)
- Ubuntu's `plymouth` package ships no `plymouth-set-default-theme`. Two inputs select the theme and must agree: the `default.plymouth` alternative decides which theme the initramfs-tools hook packs; `/etc/plymouth/plymouthd.conf` `[Daemon] Theme=` is copied into the initramfs and read first by plymouthd (the shutdown/reboot splash reads it from `/`). Under dracut, `plymouth-populate-initrd` also packs the alternative's theme; `Theme=` in `plymouthd.conf` only takes over when `ThemeDir=` is set too, and dracut's `45plymouth` discards its stderr. The alternative is in manual mode on `fallout`; in auto mode `kubuntu-logo` (priority 150) wins. `plymouthd.conf` is a conffile of the `plymouth` package, so an upgrade can prompt about it or replace it.
- Fonts: the initramfs-tools hook packs only the Ubuntu font family and makes it the fontconfig default. Fonts inside a theme dir are copied but not indexed, so any other font silently falls back. `plymouth-populate-initrd` (dracut) instead packs the fonts named by `Font=`, `TitleFont=` and `MonospaceFont=` in the `.plymouth` file.
- Plugins: `two-step`, `script`, `label-pango` (no `label-freetype`). Renderers: `drm`, `frame-buffer`; no `x11` (`plymouth-x11` is in the archive, not installed). `plymouthd.defaults`: `UseSimpledrm=1`, `DeviceTimeout=8`, `ShowDelay=0`. two-step loads `background.png` and supports `ScaleBackgroundImage`.
- Early KMS: the owner added `amdgpu` to `/etc/initramfs-tools/modules` on 2026-11-05 (`FRAMEBUFFER=y` alone only adds the `minimal_drm` set). Built-in simpledrm still starts first, but amdgpu takes over ~25 ms later, before plymouth-start (9.5 s), so the splash draws at native resolution from its first frame. All five initrds were rebuilt with it (`-k all`, 2026-11-05). Without amdgpu in an initrd, the splash starts on simpledrm at the video mode GRUB left (on EFI `10_linux` sets `gfxpayload=keep`) and switches ~3.5 s later. The splash was visible ~9 s on the boot checked (it quits when SDDM starts).
- `update-initramfs -u` rebuilds only the newest kernel; `/usr/libexec/plymouth/plymouth-update-initrd` runs `-k all`. Five kernels are installed; initrds are ~94 MB (~128 MB with amdgpu and its firmware) and mode 0600 from `UMASK=0077` in `/etc/initramfs-tools/conf.d/calamares-safe-initramfs.conf` (root-only).
- The 26.04 kernel package depends on `dracut | linux-initramfs-tool`, so a fresh 26.04 install would likely use dracut. Detect the generator; never assume it.

**KSplash**: `ksplashrc` `[KSplash]` holds `Engine=KSplashQML` and `Theme=<package id>`. Preview with `ksplashqml --test --window <path-to-package>` (takes a path).

**Media**: ffmpeg (with the `libwebp_anim` encoder, for video → animated WebP) and ImageMagick 7 are installed. `qml6-module-qtmultimedia` is a hard dependency of `plasma-workspace` (via the virtual keyboard).

**Timeshift** 25.12 snapshots `@` in btrfs mode (not `@home`), keeping one each of hourly, daily, weekly, monthly and boot snapshots. They hold replaced initrds (~470 MB per Plymouth apply, ~640 MB once every initrd carries amdgpu) until they rotate.
