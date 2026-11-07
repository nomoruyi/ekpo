# Ekpo: architecture

Status: **approved by the owner** (2026-11-05). Implements `SPEC.md` (approved). Stack from round 7: C++ / Qt 6.10 Quick + Kirigami 6.24, CMake 4 + Ninja, QtTest.

## 1. Processes 

```
 ┌──────────────────────────── user session (unprivileged) ───────────────────────────┐
 │  ekpo            one binary: GUI without arguments, CLI with a subcommand        │
 │   ├─ Library         plain JSON + files in ~/.local/share/ekpo/                  │
 │   ├─ Generators      Design → plain theme package (pure functions)                   │
 │   ├─ Probe           reads system state (no root needed)                             │
 │   ├─ Planner         desired state vs current → Plan (steps, diffs, commands)        │
 │   └─ Executor        Plasma step itself; boot steps via the helper                   │
 └───────────────┬─────────────────────────────────────────────────────────────────────┘
                 │ systemd-inhibit --what=shutdown:sleep  pkexec  ekpo-helper
                 │ stdin: request (JSON) + payload (framed files)   stdout: JSON progress lines
 ┌───────────────▼──────────────────────── root ───────────────────────────────────────┐
 │  ekpo-helper     fixed operations only: apply, recover, uninstall                │
 │   validate → journal → install → configure → rebuild → verify (rollback on failure)  │
 └──────────────────────────────────────────────────────────────────────────────────────┘
```

- **Why pkexec, not KAuth:** round 6 chose a pkexec helper with a polkit policy. KAuth would add a D-Bus system service and `libkf6auth-dev` (not installed) for no gain here.
- **Polkit policy** `io.github.nomoruyi.ekpo.apply`: `allow_active = auth_admin_keep` (password kept ~5 min), `exec.path` annotation pins `/usr/libexec/ekpo/ekpo-helper`.
- The helper never reads `/home`. Everything it installs arrives on stdin.
- The inhibitor wrapper blocks shutdown and suspend for the duration (polkit allows `inhibit-block-shutdown` for active sessions without a prompt; checked); the helper ignores `SIGPIPE`, so closing the window can't cut an apply in half.

## 2. The trust boundary (helper)

The caller is untrusted: any process of the owner can start the helper once the password is cached.

**Request** (JSON, `"format": 1`): an ordered list of stage changes.
- `grub`: `{theme: <installed id> | <payload id> | "original", gfxmode, timeout, timeout_style}`
- `plymouth`: `{theme: <installed id> | <payload id> | "original"}`
- "original" values are captured by the app and sent back verbatim (e.g. alternative path + manual/auto mode, `Theme=` value), then validated like any other value.

**Payload** (`src/common/payload.h`): after the request's JSON line, `EKPO-PAYLOAD 1`, then `F <size> <id>/<path>` + bytes per file, then `E`. No modes: the helper writes 0644 files only. Own framing (~100 lines) instead of tar, so there is no symlink, device, owner or `..` handling to get wrong.

**Validation, all before the first write:**
- IDs `^ekpo-[a-z0-9-]{1,48}-v[0-9]+$`; paths relative, no `..`, regular files only, size caps (Plymouth 15 MB, GRUB 64 MB), allowed extensions.
- Image headers checked (PNG/JPEG/TGA), `.pf2` magic checked.
- `theme.txt`: own parser of GRUB's grammar with the global properties and component types from GRUB 2.14's `gfxmenu.mod` (an unknown global makes GRUB reject the theme). Unknown component properties pass, since GRUB ignores them (hollow-grub uses one). Image references must be relative paths inside the theme; a missing image isn't an error.
- `.plymouth`: `ModuleName` must be `two-step` or `script`; `ImageDir`/`ScriptFile` must point into the theme's own folder. Other keys are inert to plymouthd and pass. Imported `script` themes are code run by plymouthd at boot (its script language has no file or process access); the import report says so.
- Config values: gfxmode from a fixed pattern, timeout 0–600, style `menu|countdown|hidden`; an installed theme ID must exist under the fixed theme roots.

**Writes** use fixed absolute paths, `O_NOFOLLOW`, write-to-temp + `rename`. Commands run with absolute paths and a clean environment.

## 3. Helper transaction

1. Take the dpkg frontend lock (`/var/lib/dpkg/lock-frontend`, as apt does) so apt, Discover and unattended-upgrades wait. Check free space on `/` and `/boot`.
2. Write the journal `/var/lib/ekpo/txn/` (only thing Ekpo keeps on the system during an apply): the plan, pre-state values, copies of every file it will change (`zz-ekpo.cfg`, `plymouthd.conf`), the alternative's state.
3. Stage payload into `/var/lib/ekpo/txn/stage/`, validate, then move into versioned folders `/boot/grub/themes/<id>`, `/usr/share/plymouth/themes/<id>`.
4. GRUB: write `/etc/default/grub.d/zz-ekpo.cfg` (or delete it for "original": Original GRUB means "no Ekpo file"); `update-grub` (grub-mkconfig itself runs `grub-script-check` before replacing `grub.cfg`).
5. Plymouth: `update-alternatives --install` (priority 50, below every distro theme) and `--set`, or restore the captured mode; set only the `Theme=` line in `plymouthd.conf`; rebuild all initrds, or only the newest kernel's (version pinned in the journal so verify and rollback touch the same initrds; dracut gets `--force /boot/initrd.img-<v> <v>`), through the detected generator adapter; then `update-grub` again (the alternative's `default.grub` slave lands in `grub.cfg`).
6. Verify: `grub.cfg` has the expected `theme=` line and the file exists; every initrd contains the theme (`lsinitramfs` / `lsinitrd`). The result includes the real menu entry titles from `grub.cfg` (root-only), so the app can re-run the entry-width fit check and warn.
7. Success: emit the result (post-state values, backups of replaced files), delete the journal. Failure at any step: undo in reverse from the journal (incl. rebuilding initrds again), emit the log.

On start, a leftover journal (crash, power loss) blocks every operation except `recover`, which rolls back to the journal's pre-state. The app sees the journal (directory is world-readable, contents root-only) and offers recovery.

**Generator adapters** (the one interface with two real implementations): `initramfs-tools` (`update-initramfs -u -k all`, `lsinitramfs`) and `dracut` (`dracut --regenerate-all --force`, `lsinitrd`). Detection: which installed package provides `linux-initramfs-tool` (here `initramfs-tools`; `dracut-core` alone, as installed here, doesn't count).

## 4. App modules

| Module | Responsibility |
|---|---|
| `library` | Plain-file store, format version + migrations, versions on save, crash draft, trash via `QFile::moveToTrash`, export (design/Mask packages, whole-library archive) |
| `model` | `GrubDesign`, `PlymouthDesign`, `SplashDesign`, `Mask`, `Asset`, `HistoryEntry`; JSON in/out |
| `gen` | `grub` (theme.txt, 9-slice images, `.pf2` via `grub-mkfont`, entry-width fit; built for one resolution, fonts and pixel values scaled by screen height), `plymouth` (two-step/script files, frames, size meter), `ksplash` (`metadata.json`, `Splash.qml`), `pf2` reader (glyph widths and bitmaps for fit check and preview), `media` (ffmpeg via `QProcess`: video → palette PNG frames, video → silent MP4, first-frame still) |
| `importer` | Find theme root, copy verbatim, report + verdict, normalized install copy when needed |
| `probe` | Read-only state: toolchain, alternative (`update-alternatives --query`), `plymouthd.conf`, effective GRUB values (sources `/etc/default/grub` + `grub.d/*.cfg` as the user, like grub-mkconfig does), `ksplashrc` (KConfig), kernels and initrd dates, `/proc/cmdline`, installed themes, leftover journal, free space, primary screen mode, the mode GRUB booted in (`/sys/kernel/boot_params` `screen_info`) |
| `planner` | Mask or restore → `Plan`: per-stage from/to, file diffs, commands, checks, estimates; Original capture on first change of a stage |
| `executor` | Plasma step (copy package, write `ksplashrc` via KConfig), helper call with progress, Plasma rollback if the helper fails, history entry, after-reboot check (stores `/proc/sys/kernel/random/boot_id`) |
| `ui` | QML + Kirigami pages per `notes/wireframes-round8.md`; C++ models exposed with `QML_ELEMENT`. The editor's state lives in `DesignEditor` (C++): undo/redo as snapshots (a drag is one step), the crash draft 0.5 s after the last change, the preview package in a temp folder (assets linked, not copied), warnings at 1440p/1080p/720p |
| `cli` | `status`, `apply`, `restore` on the same planner and executor |

**Previews:** GRUB is drawn by the app with real `.pf2` glyphs over the sample menu; Plymouth is drawn by a QML scene from the design; the Plasma splash preview loads the generated `Splash.qml` itself in a `Loader`, plus *Run real splash* via `ksplashqml --test --window`.

**Desktop basics:** single instance through a D-Bus name on the session bus (QtDBus); notifications through `org.freedesktop.Notifications` (QtDBus), only when unfocused; translation-ready via `qsTr`/`tr`. No KI18n, KNotifications, KDBusAddons or KIO (none are installed as dev packages, and Qt covers it).

## 5. Library layout

```
~/.local/share/ekpo/
  library.json                 format version, settings
  designs/<uuid>/design.json   current; versions/<n>.json; draft.json (crash recovery)
  imports/<uuid>/import.json   metadata + report; files/ (verbatim copy)
  assets/<sha256>.<ext>        content-addressed masters; assets.json (names, licences, sources)
  shared.json                  shared logos and palettes that designs link (resolved before building)
  masks/<uuid>.json
  builds/<sha256>/             built packages, kept while a history entry needs them
  history/<n>/entry.json       desired + resulting state, backups from the helper; RESTORE.txt
  history/original.json
  pending-check.json           after-reboot question
```

A restore re-sends the build from `builds/`, so history survives a reinstall or a deleted theme folder.

## 6. Dependencies

- **Build (installed):** qt6-base-dev, qt6-declarative-dev (+ tools: qmllint, qmltestrunner), libkirigami-dev, libkf6config-dev, extra-cmake-modules, cmake, ninja-build, g++.
- **Runtime tools (installed):** grub-mkfont, update-grub, update-alternatives, update-initramfs/dracut, lsinitramfs/lsinitrd, ffmpeg, ksplashqml, pkexec, systemd-inhibit, unzip, tar.
- **None added.** Packaging uses CPack's DEB generator (dpkg-deb + fakeroot, both installed); no debhelper. ImageMagick isn't needed (QImage covers images).

## 7. Source layout

```
CMakeLists.txt
src/common/     request schema, payload framing, validators (linked by app and helper)
src/helper/     main, operations, journal, lock, generator adapters
src/app/        library, model, gen, importer, probe, planner, executor, cli, main
src/app/qml/    pages and components
packaging/      polkit policy, .desktop, icon, CPack config
tests/          unit/, golden/, fixtures/roots/ (fake system roots), stubs/ (fake update-grub etc.)
dev/            env.sh (HOME, XDG_*, TMPDIR → .dev/), sandbox root builder
```

## 8. Testing and the repo-only rule

- **Golden files** for every generator output; regenerate with an explicit flag, review the diff.
- **Helper against fake roots:** every build also produces `ekpo-helper-test`, the same code compiled with `EKPO_HELPER_TESTING`: it reads its root from `EKPO_HELPER_ROOT` and runs the Python stubs in `tests/stubs/` from `EKPO_HELPER_BIN`. Only `ekpo-helper` is installed; it has `/` and absolute command paths compiled in, no code that reads them from the environment, and refuses to run unless it is root. Apply, rollback and recover leave nothing behind in `/var/lib/ekpo`.
- **Sandbox mode:** the test build of the app talks to the test helper without pkexec, against `.dev/root/` (a copy of this system's relevant files). Lets the owner and Claude click through applies safely.
- **Screenshot mode:** `QT_QPA_PLATFORM=offscreen` renders pages to PNG so Claude can check the UI.
- `dev/env.sh` points `HOME`, `XDG_*_HOME` and `TMPDIR` into `.dev/` (gitignored) for every build, test and run, per `CLAUDE.md`.
- Commands go into `CLAUDE.md` once they exist: configure, build, all tests, one test (`ctest -R`), lint (`qmllint`; clang-format isn't installed).

## 9. Build order for M1 (small, reviewable steps)

1. CMake skeleton, `dev/env.sh`, empty Kirigami window with the sidebar, one passing test.
2. `probe` + fixtures captured read-only from this system.
3. `library` + `model` + migrations scaffold (format 1).
4. `importer` for all three stages, with reports; Overview and stage pages show real data.
5. `common`: payload framing + validators, with hostile-input tests.
6. Helper: transaction, journal, rollback, both adapters, against fake roots.
7. `planner` + review screen; `executor` + history + Original; restore.
8. Drift detection, after-reboot check, timeout setting, first start and intro dialog.
9. CLI.
10. Polkit policy, `.deb` via CPack; owner's first real applies (GRUB only → Plymouth only → Mask), with a Timeshift snapshot and live USB ready.

Spikes for SPEC §10 run before M2 (reading Plymouth 24 sources and measuring frame sizes need no system changes).

## 9a. Build order for M2 (proposed 2026-11-06)

1. Library format 2: `designs/` (version per save, crash draft) and `assets/` (content-addressed masters, `assets.json` with name, licence, source); migration from format 1; system fonts copied in with their licence file.
2. `gen ksplash` from a `SplashDesign` (background, image, text, spinner) with golden files; a design becomes a stage source like an import, so it reaches the existing build, review and apply path.
3. Editor shell on the Plasma splash: element list · canvas · properties, % positions, snapping and nudging, 1440p/1080p/720p switch, warnings bar, undo/redo, Ctrl+S versions, crash draft; preview loads the generated `Splash.qml`; *Run real splash*.
4. `media`: video → silent MP4 and video → palette PNG frames through ffmpeg with progress and cancel; MP4 element in the Plasma splash.
5. `gen plymouth`: two-step (background, logo, throbber up to 2 s, progress bar, static text as images, `SuppressMessages`) and the script module for longer loops, with golden files.
6. Plymouth designer in the editor shell: QML preview, animation from frames or video, file-size and decoded-memory meters.
7. Starters tinted with the Plasma accent colour; linked shared elements (logo, palette) with "update available" on Masks that use them.

## 10. Decisions for the owner

1. pkexec helper with stdin payload and own framing (above), or KAuth?
2. One `ekpo` binary for GUI and CLI, or two?
3. Plymouth alternative priority 50 for Ekpo themes (never wins in auto mode), or match the distro's 100?
