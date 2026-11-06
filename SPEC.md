# Ekpo: specification

Status: **approved by the owner** (2026-11-05). Source: `INTERVIEW.md` rounds 1–9; where they disagree, the later round wins. System facts are in `CLAUDE.md`. Named Ekpo (Edo/Bini for "mask") on 2026-11-06; earlier drafts called it Personae.

## 1. Purpose

One desktop app to design, store, build, apply and switch the look of the three boot stages on the owner's Kubuntu 26.04 machine, and to get back to a known-good state at any time:

| Stage | UI name | What gets installed |
|---|---|---|
| GRUB menu theme | Boot Menu | `theme.txt` + assets in `/boot/grub/themes/<id>` |
| Plymouth splash | Boot Splash | `two-step` (or `script`) theme in `/usr/share/plymouth/themes/<id>`, packed into every initrd |
| KSplash | Plasma Splash | look-and-feel package in `~/.local/share/plasma/look-and-feel/<id>` |

It replaces a hand-installed GRUB theme and the mostly broken "Get New" splashes. Audience: the owner; private repo, MIT licence.

## 2. Principles

1. **Never leave the system unbootable.** Validate every generated file, back up every file touched, keep a fallback, log every privileged action, make every apply reversible.
2. **Change as little on the host as possible.** No extra `/etc/grub.d` scripts, no font or initramfs hooks, no daemon, no edits to `/etc/default/grub` or the kernel command line.
3. **Output is plain, standard theme packages** that keep working after Ekpo is uninstalled.
4. **Nothing happens silently.** Every apply shows a review (file changes and commands) first; changes made outside the app are detected, never overwritten without asking.
5. **Detect, don't assume** the toolchain (initramfs-tools or dracut, GRUB paths, Plymouth alternative state).

## 3. Concepts

- **Design:** a named profile for one stage, made in Ekpo's editor. Editable; every save is a restorable version.
- **Imported theme:** a third-party theme copied into the library read-only. It can be previewed, applied and put into Masks, not edited (hand edits happen outside the app; *Re-import* pulls them in).
- **Mask** (a bundle): one slot per stage, each holding a design, an imported theme, or *Leave as is*. Slots reference items; nothing is copied. A single design applied on its own is a one-slot Mask.
- **Asset:** an image, video, spinner or font stored once in the library and referenced by designs. Shared elements (e.g. a logo or palette) are *linked*: editing them updates every design that uses them; applied looks change only when re-applied ("update available"). A design can unlink an element.
- **History entry:** one apply (or restore) of the system state, restorable. **Original** is a pinned entry: each stage's state right before Ekpo first changed it.
- **Library:** plain files in `~/.local/share/ekpo/` (location configurable). Holds designs, versions, imports, assets, Masks and the history.

## 4. Functional requirements

### 4.1 Main window and navigation

- Sidebar: Overview · Boot Menu · Boot Splash · Plasma Splash · Masks · Assets · History; Settings and About at the bottom.
- **Overview** (start page): three stage cards in boot order with thumbnail, current theme, status ("as applied", "changed outside Ekpo", "N of 5 kernels outdated"), the GRUB timeout, the detected toolchain and whether `splash` is on the kernel command line.
- **Stage pages:** grid of designs and imports with Edit (designs only), Preview, Apply, Duplicate, Delete; New design and Import.
- Apply, restore and first run take over the window. Wireframes: `notes/wireframes-round8.md`.
- One instance at a time. Notifications only when the window isn't focused. Full keyboard access. English UI, text prepared for translation. Follows the Plasma colour scheme.

### 4.2 First start

- System check: toolchain, `splash` on the command line, installed themes, free space. Offers to import the installed themes.
- **Intro dialog**: what the three stages are; how to back up first (Timeshift snapshot, live USB, library export); how to get back to a working system (in-app restore, `ekpo restore original`, an older kernel from GRUB's Advanced options, Timeshift from a live USB). "Don't show again" checkbox, off by default; reachable from Help.
- After a reinstall (library present, system state unknown), a "welcome back" view offers to re-apply or adopt the current state.

### 4.3 Import

- Sources: theme folders and archives. Finds the real theme root (e.g. themes nested one folder deep next to an install script); never runs bundled scripts.
- Copies the theme into the library verbatim, remembers the source path, writes an import report with a verdict (works / repaired / broken and why).
- Installed, unchanged themes (fallout, mint-galaxy, Overload, hollow-grub, crossgrub) are applied in place; broken or nested ones get a cleaned-up installed copy under a Ekpo ID.
- Imported Plasma splashes are QML that runs at every login; Ekpo doesn't sandbox them, and the report says so.

### 4.4 Editors (all stages)

- Layout: element list · canvas · properties panel; resolution switch 1440p / 1080p / 720p; warnings bar.
- Drag on the canvas with snapping (centre, edges, other elements); number fields and arrow-key nudging. Positions and sizes are stored as % of the screen.
- Explicit save (Ctrl+S); each save is a version (all kept); undo/redo per open design for the session; one crash-recovery draft.
- New designs start from a few starters tinted with the Plasma accent colour, from blank, or by duplicating a Ekpo design.
- Validation at all three resolutions; problems that would break or clip the result warn, invalid output blocks the build.
- Fonts picked from the system are copied into the library with their licence file where one exists.

### 4.5 Boot Menu designer (GRUB)

- Elements: background, title or logo image, the entry list (font, colours, selected-entry style: flat colour, rounded box or glow, baked into 9-slice images), per-entry icons (small built-in monochrome set: Linux, Kubuntu-style, Windows, firmware settings, memtest; plus imports), countdown as text and/or progress bar, help line. No scrollbar.
- One high-resolution master per asset; Ekpo derives what each resolution needs at build time. The background and `image` components scale at boot; `.pf2` fonts don't.
- **Entry text is never cut off.** GRUB draws each entry on one line and clips it, so the menu width adapts: the build measures the longest expected entry (real `.pf2` glyph widths, icon and padding, headroom for longer kernel versions) and widens the menu box up to full width; if it still doesn't fit, the build warns and offers a smaller font. Expected entries come from the kernels in `/boot`, `GRUB_DISTRIBUTOR` and the menu scripts present (kernels, Advanced options, memtest, os-prober, UEFI Firmware Settings, fwupd); the helper re-checks against the real `grub.cfg` after `update-grub`.
- Terminal font stays GRUB's built-in Unifont.
- Preview: drawn by the app with the real `.pf2` text over that sample menu.

### 4.6 Boot Splash designer (Plymouth)

- Module: `two-step`; `script` only when a design needs it.
- Elements: background (scaled to the screen), logo, a centred animation (spinner, imported frames or frames converted from a video), optional progress bar, static text (rendered into images at build time, any font).
- Live text (status messages) is always white Ubuntu (initramfs-tools packs only that font). **"Hide status messages"** checkbox per design, off by default; password and emergency prompts always show; Esc still shows details.
- Shutdown and reboot use the boot look, with optional per-mode text such as "Restarting…".
- **Size:** a theme is capped at 15 MB (it is copied into every initrd and every Timeshift snapshot); the editor shows a size meter and warns before the cap. Video → frames uses lossy 8-bit palette PNGs (ffmpeg `palettegen`/`paletteuse`); the converter shows the maximum length for the chosen frame size and quality before it builds. Length is limited by frame size, not by a fixed number of seconds; a full-screen video does not fit.
- Preview: drawn by the app.

### 4.7 Plasma Splash designer (KSplash)

- Full-screen looping video: one silent MP4 played through QtMultimedia, scaled by the GPU, with a still of the first frame until playback starts. Plus image, logo and text layers, optional progress indicator, fade-in.
- `Splash.qml` imports only QtQuick (and QtMultimedia for video); no Kirigami or Plasma modules. Fonts are bundled.
- Preview in the editor, plus **Run real splash** (`ksplashqml --test --window <package>`).

### 4.8 Masks

- One slot per stage: design, imported theme, or *Leave as is*.
- Missing references are shown; items used by a Mask, history entry or saved version can't be deleted.

### 4.9 Apply

- **Review screen** before every apply: per stage, from → to, what is written and run, time estimate (~40 s for 5 initrds), extra Timeshift cost; checks (files valid, free space, no apt/dpkg running, fallback kept); file diffs and commands collapsed by default (Settings can expand them).
- **Order:** Plasma splash first (no root), then GRUB and Plymouth in one privileged helper run. All-or-nothing: any failure rolls the whole Mask back and shows the log.
- **GRUB:** install the theme into a versioned folder `/boot/grub/themes/ekpo-<slug>-<ver>`; write only `/etc/default/grub.d/zz-ekpo.cfg` (`GRUB_THEME`, `GRUB_GFXMODE=2560x1440,1920x1080,1280x720,auto`, timeout keys); run `update-grub`; verify `grub.cfg`. Warn when `zz-ekpo.cfg` overrides a value set in `/etc/default/grub`.
- **Plymouth:** install into a versioned folder; register and select the `default.plymouth` alternative; set `plymouthd.conf` `Theme=` to match; rebuild **all** initrds with the detected generator; run `update-grub` too (the alternative's `default.grub` slave ends up in `grub.cfg`); verify.
- **Plasma splash:** copy the package, set `ksplashrc` `[KSplash] Theme=`/`Engine=KSplashQML`.
- Re-applying an edited design installs a new versioned folder, so every history entry stays restorable; folders no kept entry uses are removed after the next successful apply.
- **After reboot:** the next time the app opens after a boot-stage apply, it asks "Did the new look work?" — *Keep* or *Revert to previous*.

### 4.10 Boot options

- GRUB menu timeout: one machine-wide setting on the Overview's Boot Menu card, applied through review and history. Ekpo manages `GRUB_TIMEOUT`, `GRUB_RECORDFAIL_TIMEOUT` and `GRUB_TIMEOUT_STYLE`. 0 s (instant boot, no way into the menu here) requires ticking "I can recover with a live USB".

### 4.11 History and restore

- Every apply and restore is an entry; keep the last 10 plus Original. Stored in the library under `/home`, so it survives a reinstall.
- Backups cover the config files Ekpo touches; initrds are rebuilt, not copied; installed theme folders stay while an entry references them. The helper re-validates every restore like an apply.
- A short crash journal on the system during an apply lets the helper finish or roll back after a crash or power loss.
- Each entry carries a plain-text `RESTORE.txt` with console commands for recovery without the app.

### 4.12 Changes made outside the app

- Checked when the app opens or gets focus: real system state vs last apply (e.g. System Settings' Boot Splash page, a Global Theme switch, kernel updates leaving initrds with an older splash). Shown on the Overview; the owner can adopt the current state or re-apply.

### 4.13 Library, export, settings

- Deleting moves items to the desktop trash with an inline Undo.
- Export a design or Mask as plain theme packages (GRUB/Plymouth `.tar.gz`, look-and-feel package). Export the whole library as one archive. Exports warn when an asset's licence forbids sharing.
- Generated packages carry author "nomoruyi" and licence MIT, editable per design; fonts and imported art keep their own licences.
- Library files carry a format version; newer versions migrate older ones.
- **Settings:** library location, history size, technical details expanded or collapsed, default preview resolution. Nothing else until a real need appears.

### 4.14 Command line

`ekpo status | apply <mask> | restore <entry|original>`, with the same review and a y/N prompt. Designing stays GUI-only.

## 5. Privileges and installation

- The GUI never runs as root. A small fixed-operation helper runs via `pkexec` with an installed polkit policy (clear prompt text, authorization kept for a few minutes). It accepts only validated requests, never reads the home directory directly, and logs every action.
- Installed as a locally built `.deb` (app, helper, polkit policy). Removing it leaves applied looks working; an in-app "Restore Original and remove Ekpo files" cleans the system first.
- No daemon, tray icon, autostart or apt hook.

## 6. Stack (decided in round 7)

C++ with Qt 6 Quick + Kirigami, CMake, QtTest; a small C++ root helper. Generators get golden-file tests (`theme.txt`, `.plymouth`/`.script`, `Splash.qml`, `metadata.json`). Apply logic is tested against fake system roots in the repo with stubbed system tools; a sandbox mode runs the whole app against such a root. The architecture is a separate proposal (gate 3).

## 7. Milestones

| Milestone | Content |
|---|---|
| **M1: manage** | Library, import with report, Masks, apply with review, history and restore (incl. Original), after-reboot check, outside-change detection, timeout setting, first start and intro dialog, CLI, `.deb` with helper and polkit policy. No designers. |
| **M2: splash designers** | Asset library (fonts, provenance), editor shell, Plymouth two-step designer (incl. video → frames, size meter, hide messages), Plasma splash designer (MP4 video, Run real splash), starters, linked shared elements. |
| **M3: GRUB designer** | Boot Menu designer incl. `.pf2` build, adaptive entry width, icon set, countdown. |
| **v1.0** | M1–M3 plus design/Mask/library export. |

Later, not scheduled: SDDM login screen; pixel-exact GRUB preview from a rescue ISO in QEMU; real Plymouth preview via `plymouth-x11`.

## 8. Non-goals

Editing GRUB menu entries or kernel parameters; editing third-party themes in the app; theme stores or any network use; scheduled or per-boot rotation; lock and logout screens; other user accounts; 4K (3840×2160) layouts; a VM-based preview; machines other than this one (exported packages may still work elsewhere).

## 9. Technical limits the design accepts

- GRUB entries can't wrap or be centred; one `theme.txt` serves every resolution, so font pixel sizes are the same at 1440p, 1080p and 720p (text looks larger at lower resolutions). Per-resolution fonts would need an `/etc/grub.d` script, which principle 2 rules out.
- Plymouth two-step: logo and animation are fixed pixel sizes; live text is white Ubuntu.
- With `/boot` on btrfs, GRUB has no failed-boot detection: a 0 s timeout leaves the menu unreachable.
- Each Plymouth apply leaves ~640 MB of old initrds in Timeshift snapshots until they rotate.
- Initrds of kernels installed later get the splash selected at that time; outside tools (System Settings) may rebuild only the newest kernel. The Overview shows outdated initrds by date.

## 10. Open questions

1. ~~**Final name**~~ Resolved 2026-11-06: Ekpo.
2. **Spikes before M2:** can two-step hide status messages (else `script`)? Does two-step play animations at a fixed frame rate? Real per-frame sizes for the 15 MB cap. MP4 start latency in KSplash. Does two-step's live text render under dracut?
3. Should a look-and-feel package that holds only a splash stay out of System Settings' Global Themes list, and how?
4. Round 8 Q1 promised per-resolution GRUB font sizes with an override; that isn't possible without an `/etc/grub.d` script (§9). Accept one font size set for all resolutions?
