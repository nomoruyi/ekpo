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
- The same building blocks on every stage, as far as its format allows (2026-11-07): background colour, a two-colour gradient at any angle (0–360°, issue #3) or an image; images and shared logos; text in any font; palette colours (five: background, gradient, accent, second accent, text; palettes made with three fill the new two from background and accent); a progress indicator (Plasma splash: Plasma's start; boot splash: boot progress; boot menu: the countdown); an animation from frames (both splashes). Only where the format has them: video (Plasma splash; the boot splash converts video to frames), fade-in (Plasma splash), hiding status messages (boot splash), the menu, countdown text and entry icons (boot menu). GRUB can't animate, so the boot menu has no spinner or animation.
- Drag on the canvas with snapping (centre, edges, other elements); number fields and arrow-key nudging. Positions and sizes are stored as % of the screen. Helpers (2026-11-08): align to the screen's edges or centre, duplicate (Ctrl+D), lock an element against canvas edits, copy and paste its style (Ctrl+Shift+C/V).
- Text effects (2026-11-08): shadow, outline and glow, each with its colour, sized from the font. The boot splash draws them into its background; a boot menu draws such text into `background.png` (GRUB's labels are plain); the Plasma splash uses copies of the text (plain QtQuick).
- Spinners (2026-11-08): dots, arc, pulse, bars, orbit, comet or a turning image, slow, normal or fast; both splashes draw them alike. Progress bars: flat, rounded, glow or segmented (both splashes; a shimmer on the Plasma splash), and on the boot menu flat, rounded or a ring of dots. Images can fade in, slide up, breathe, pulse or spin (both splashes).
- Explicit save (Ctrl+S); each save is a version (all kept); undo/redo per open design for the session; one crash-recovery draft.
- New designs start from a few starters tinted with the Plasma accent colour (and on every stage Minimal, Neon, Glass and Retro CRT), from blank, or by duplicating a Ekpo design.
- Versions… shows a thumbnail of each saved version; palettes can come from an image's main colours.
- Videos (both splashes) take a part of a longer recording: start and length on a range over the whole video, with a silent preview that loops the part (2026-11-07), so one recording gives one clip per look. The clip's asset name says which part it is.
- Validation at all three resolutions; problems that would break or clip the result warn, invalid output blocks the build.
- Fonts picked from the system are copied into the library with their licence file where one exists.

### 4.5 Boot Menu designer (GRUB)

- Elements: background, title or logo image, the entry list (font, colours, selected-entry style: flat colour, rounded box, glow, outline, underline or a bar on the left, baked into 9-slice images; GRUB stretches their middle, so pills and arrows can't be drawn), per-entry icons (small built-in monochrome set: Linux, Kubuntu-style, Windows, firmware settings, memtest; plus imports), countdown as text and/or progress bar, help line. No scrollbar.
- One high-resolution master per asset; Ekpo derives what each resolution needs at build time. The background and `image` components scale at boot; `.pf2` fonts don't.
- **Built for one resolution.** Each apply builds the theme for the boot resolution: by default the current mode of the primary screen (KScreen priority 1; here 2560×1440), changeable on the review page. Font sizes and the pixel values in `theme.txt` (item height, padding, spacing) are scaled from the design by screen height, with an optional per-resolution font size override in the designer; the entry width check runs at that resolution. Positions in % need no change.
- **Entry text is never cut off.** GRUB draws each entry on one line and clips it, so the menu width adapts: the build measures the longest expected entry (real `.pf2` glyph widths, icon and padding, headroom for longer kernel versions) and widens the menu box up to full width; if it still doesn't fit, the build warns and offers a smaller font. Expected entries come from the kernels in `/boot`, `GRUB_DISTRIBUTOR` and the menu scripts present (kernels, Advanced options, memtest, os-prober, UEFI Firmware Settings, fwupd); the helper re-checks against the real `grub.cfg` after `update-grub`.
- Terminal font stays GRUB's built-in Unifont.
- Preview: drawn by the app with the real `.pf2` text over that sample menu.

### 4.6 Boot Splash designer (Plymouth)

- Module: `two-step`; `script` when a design needs it, e.g. an animation loop longer than 2 s (two-step's throbber always loops in 2.0 s at 30 fps), a lower frame rate, a styled progress bar or a moving image.
- A notice on the Overview when a kernel was installed since Ekpo last looked: apt builds its initrd with the selected splash; if one lacks it, **Rebuild…** packs the splash into every initrd again (also offered on the Boot Splash card when initrds are older than the selection).
- Elements: background (scaled to the screen), logo, a centred animation (spinner, imported frames or frames converted from a video), optional progress bar, static text (rendered into images at build time, any font).
- Live text (status messages) is always white Ubuntu (initramfs-tools packs only that font). **"Hide status messages"** checkbox per design, off by default (two-step's `SuppressMessages`, per mode); password and emergency prompts always show; Esc still shows details.
- Shutdown and reboot use the boot look, with optional per-mode text such as "Restarting…".
- **Size:** a theme is capped at 15 MB (it is copied into every initrd and every Timeshift snapshot); the editor shows a size meter and warns before the cap. Video → frames uses lossy 8-bit palette PNGs (ffmpeg `palettegen`/`paletteuse`); the converter shows the maximum length for the chosen frame size and quality before it builds. Length is limited by frame size and content, not by a fixed number of seconds. A second meter shows decoded memory (width × height × 4 bytes per frame, all loaded before the splash shows), budget about 300 MB: full-screen 1440p animations stay short or slow, centred ones can run long (`notes/spikes-2026-11-06.md`).
- Preview: drawn by the app.

### 4.7 Plasma Splash designer (KSplash)

- Full-screen looping video: one silent MP4 played through QtMultimedia, scaled by the GPU (first frame ~30 ms after play; a first-frame still is optional). Plus image, logo and text layers, optional progress indicator, fade-in.
- `Splash.qml` imports only QtQuick (and QtMultimedia for video); no Kirigami or Plasma modules. Fonts are bundled.
- Background extras (2026-11-08): a slow pan and zoom over the background image, snow or stars, a vignette, film grain.
- Preview in the editor, plus **Run real splash** (`ksplashqml --test --window <package>`).

### 4.8 Masks

- One slot per stage: design, imported theme, or *Leave as is*.
- Missing references are shown; items used by a Mask, history entry or saved version can't be deleted.

### 4.9 Apply

- **Review screen** before every apply: per stage, from → to, what is written and run, time estimate (~8 s per rebuilt initrd), extra Timeshift cost; checks (files valid, free space, no apt/dpkg running, fallback kept); file diffs and commands collapsed by default (Settings can expand them).
- **Order:** Plasma splash first (no root), then GRUB and Plymouth in one privileged helper run. All-or-nothing: any failure rolls the whole Mask back and shows the log.
- **GRUB:** install the theme into a versioned folder `/boot/grub/themes/ekpo-<slug>-<ver>`; write only `/etc/default/grub.d/zz-ekpo.cfg` (`GRUB_THEME`, `GRUB_GFXMODE=<built resolution>,auto` for Ekpo designs and `2560x1440,1920x1080,1280x720,auto` for imported themes, timeout keys); run `update-grub`; verify `grub.cfg`. Warn when `zz-ekpo.cfg` overrides a value set in `/etc/default/grub`.
- **Plymouth:** install into a versioned folder; register and select the `default.plymouth` alternative; set `plymouthd.conf` `Theme=` to match; rebuild the initrds with the detected generator: only the newest kernel's or all, chosen per review with a switch (default from Settings, off = newest only; restores always all, so a restore never leaves an unwanted splash on older kernels; older kernels keep the previous splash as a fallback); run `update-grub` too (the alternative's `default.grub` slave ends up in `grub.cfg`); verify.
- **Plasma splash:** copy the package, set `ksplashrc` `[KSplash] Theme=`/`Engine=KSplashQML`.
- Re-applying an edited design installs a new versioned folder, so every history entry stays restorable; folders (and Plasma splash copies, to the trash) no kept entry uses are removed after the next successful apply. Settings → **Remove Old Theme Copies** removes leftovers without an apply (the helper's `prune` operation).
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
- **Monitor change:** when the applied Ekpo boot menu was built for another resolution than the primary screen now runs, the Boot Menu card says so and offers "Rebuild for <W×H>" (review, then apply). It also compares with the mode GRUB actually used on this boot (`/sys/kernel/boot_params`, world-readable: the framebuffer GRUB left, as `gfxpayload=keep`); if the firmware didn't offer the pinned mode, GRUB fell back to `auto` and the card warns.

### 4.13 Library, export, settings

- Deleting moves items to the desktop trash with an inline Undo.
- **Play the Boot** (2026-11-08): the boot menu, boot splash and Plasma splash previews one after another, about as long as a boot shows each, for the look applied now (Overview) or a Mask (its card). Ekpo can only play the boot splashes it makes.
- **New Mask from What's Applied** turns the current look into a Mask.
- **Assets page** (2026-11-07): images, videos and fonts in the library, shared palettes and logos, filtered by type; for each a preview (font sample, palette colours), size, date, source, licence (fonts) and what uses it: designs with their saved versions and drafts, shared logos, fonts (licence texts). Add images and fonts; rename; move to the trash only what nothing uses (a saved version would lose it), one by one or "Remove Unused". Videos are added in the editor, which converts them for their splash.
- Export a design or Mask as plain theme packages (GRUB/Plymouth `.tar.gz`, look-and-feel package). Export the whole library as one archive. Exports warn when an asset's licence forbids sharing.
- Generated packages carry author "nomoruyi" and licence MIT, editable per design; fonts and imported art keep their own licences.
- Library files carry a format version; newer versions migrate older ones.
- **Settings:** library location, history size, technical details expanded or collapsed, default preview resolution, check for updates at start. Nothing else until a real need appears.

### 4.14 Updates (approved 2026-11-07)

- At every start Ekpo asks GitHub for the newest release of `nomoruyi/ekpo`: one request to the releases API, without a login, sending nothing but Ekpo's version as the user agent. A Settings switch turns it off; "Check Now" checks on demand. Failures (offline, rate limit) show only in Settings.
- A newer release shows on the Overview ("Ekpo X.Y.Z is available") with its release notes and **Update…**: Ekpo downloads the `.deb` and `SHA256SUMS` (progress, cancel), checks the checksum and that the package is version X.Y.Z, installs it with `pkexec apt-get install` (one password prompt; apt resolves dependencies) and offers to restart. Not while an apply runs.
- The checksum catches broken downloads, not a compromised GitHub account; signed releases may come later.
- Dev and sandbox builds never contact GitHub on their own and never run `pkexec`: they check a URL given for tests and install through a stub.

### 4.15 Command line

`ekpo status | apply <mask> | restore <entry|original> | recover`, with the same review and a y/N prompt (`recover` rolls back an interrupted apply). Designing stays GUI-only.

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
| **M4: export** | Design/Mask/library export (ARCHITECTURE.md §9d), shipped as a minor release (decided 2026-11-08). |
| **Polish** | Feature extensions and quality-of-life additions the owner picks, plus the bugs found while testing. |
| **v1.0** | M1–M4 after polish and bug fixes; released only then. |

Later, not scheduled: SDDM login screen; pixel-exact GRUB preview from a rescue ISO in QEMU; real Plymouth preview via `plymouth-x11`.

## 8. Non-goals

Editing GRUB menu entries or kernel parameters; editing third-party themes in the app; theme stores or any network use except the update check (§4.14); scheduled or per-boot rotation; lock and logout screens; other user accounts; 4K (3840×2160) layouts; a VM-based preview; machines other than this one (exported packages may still work elsewhere).

## 9. Technical limits the design accepts

- GRUB entries can't wrap or be centred. `.pf2` fonts are fixed pixel sizes, so a boot menu built for one resolution has the wrong text size on another monitor until it is rebuilt (§4.12). Choosing fonts at boot time would need logic in `grub.cfg` that Ekpo can't test on these monitors.
- Plymouth two-step: logo and animation are fixed pixel sizes; live text is white Ubuntu.
- With `/boot` on btrfs, GRUB has no failed-boot detection: a 0 s timeout leaves the menu unreachable.
- Each Plymouth apply leaves ~640 MB of old initrds in Timeshift snapshots until they rotate.
- Initrds of kernels installed later get the splash selected at that time; outside tools (System Settings) may rebuild only the newest kernel. The Overview shows outdated initrds by date.

## 10. Open questions

1. ~~**Final name**~~ Resolved 2026-11-06: Ekpo.
2. ~~**Spikes before M2**~~ Resolved 2026-11-06 (`notes/spikes-2026-11-06.md`): two-step hides messages with `SuppressMessages`; it draws at 30 fps and its loop always lasts 2.0 s (script module for longer loops); frame sizes depend mostly on content, and decoded memory is the tighter limit for full-screen animations; MP4 starts in ~30 ms; live text under dracut should render (fontconfig falls back to `/usr/share/fonts`), still to be seen on a dracut boot.
3. ~~**Global Themes**~~ Resolved: Plasma lists only packages with `contents/defaults` or `contents/layouts`, so splash-only packages stay out by themselves.
4. ~~**GRUB font sizes per resolution**~~ Resolved 2026-11-06: each apply builds the boot menu for one resolution (the primary screen's) and pins `GRUB_GFXMODE` to it; a monitor change is detected and offers a rebuild (§4.5, §4.12).
