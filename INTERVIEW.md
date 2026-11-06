# Interview log

Input for `SPEC.md`. Answers are the owner's; anything marked *(Claude)* is a recommendation or finding the owner hasn't confirmed yet.

## Round 1: scope (answered 2026-11-05)

1. **Switching frequency:** rarely. Initramfs rebuild time is acceptable; no per-boot rotation design.
2. **Targets:** GRUB, Plymouth, KSplash. SDDM may come in a later version, but nothing is designed around it.
3. **GRUB menu:** shows on every boot today. Wants a timeout setting where 0 hides the menu. *(Claude: on a btrfs `/boot` the effective key is `GRUB_RECORDFAIL_TIMEOUT`, and 0 leaves no way into the menu; see round 2.)*
4. **Encryption:** none, so no Plymouth password prompt. **Reinstall:** undecided and not needed for this project *(Claude)*; the current install is the reference system.
5. **Initramfs generators:** support both initramfs-tools and dracut if feasible, otherwise dracut only. *(Claude: the reference system runs initramfs-tools, so dracut-only would not work here; see round 2.)*
6. **Existing themes:** import installed third-party themes read-only; changes happen on clones inside the app.
7. **Audience:** primarily personal, private GitHub repo. **License:** others may copy and modify; making money requires at least crediting the owner. Options are compared in round 2.

Findings *(Claude)*:
- Two of four "Get New" Plymouth installs are broken: `WindozeXP.Plymouth/` and `vortex-ubuntu/` hold the real theme one directory deeper, next to an install script, so there is no `<name>/<name>.plymouth` and no alternative was registered. The importer must locate the real theme root and never run bundled scripts.
- `fallout` and `mint-galaxy` are `script` themes; Overload and Penrose are QML splashes; hollow-grub ships only pre-rendered `.pf2` fonts (no TTF/OTF).

## Round 2: follow-ups (answered 2026-11-05)

Principle stated by the owner: **change as little on the host as possible.**

1. **GRUB timeout:** plain seconds; 0 = instant boot with no way into the menu, so the app warns hard. No extra `/etc/grub.d` script. *(Claude: the app writes both `GRUB_TIMEOUT` and `GRUB_RECORDFAIL_TIMEOUT`; the latter is what counts with `/boot` on btrfs.)*
2. **Initramfs generators:** support both initramfs-tools and dracut. The UI shows which toolchain was detected for each target.
3. **Editing third-party themes:** not an app feature. The owner copies theme files and edits them by hand outside the app; the app only imports read-only.
4. **Testing and preview:** no VM (too heavy). A rough in-editor preview is enough; pixel-perfect is not required.
5. **License:** MIT, copyright "nomoruyi" (`LICENSE`).

## Round 3: customization depth (answered 2026-11-05)

1. **Testing:** no VM. Apply logic is tested against fake system roots in the repo with stubbed system tools; a dry run shows every file change and command; backups and rollback are automatic; the owner runs the first real applies. Optional later: pixel-exact GRUB preview from a rescue ISO in QEMU (needs `mtools`).
2. **GRUB designer (v1):** background, logo/images, one font at several sizes, menu position/size, selection images or colors, countdown text, keymap label, countdown progress bar, per-entry icons. No scrollbar. The app derives lower-resolution assets from one high-resolution master. *(Claude: assets are rendered for the selected `GRUB_GFXMODE` at build time and re-rendered when it changes; masters stay untouched.)*
3. **Designed Plymouth splashes:** two-step for v1; script only when a design needs it.
4. **Splash text:** static text is rendered into images at build time (any font); live text (boot messages) uses the Ubuntu font. No font hook on the host.
5. **Animation:** Plymouth = static background plus a centered spinner or progress bar. KSplash = a full-screen looping video must be possible.
6. **Consistency:** at least background and spinner can be shared across targets; KSplash may differ or do more when designed that way.

Findings *(Claude)*:
- Overload (GPL, ideas only) is one animated WebP (1920×1080, 373 frames, 12.5 s loop at ~30 fps, 4.9 MB) shown full-screen by a QML `AnimatedImage` (crop fill, fade-in at stage 2). Its "Loading" animations are baked into the video. The system's ffmpeg has the `libwebp_anim` encoder, so video → animated WebP works with installed tools.

## Round 4: profiles, bundles, storage (answered 2026-11-05)

Correction to round 3 Q2: themes must work at **3840×2160, 2560×1440, 1920×1080 and 1280×720 without rebuilding** when the monitor changes.

1. **Shared elements:** asset library; images, videos and spinners are stored once and profiles reference them.
2. **Bundles:** one slot per target (designed profile, imported theme, or "leave as is"); profiles are referenced, not copied.
3. **History:** automatic snapshot on every save, restorable. (Undoing system changes is covered with the apply workflow.)
4. **Storage:** plain-file library in `~/.local/share/<app-id>/` (path configurable); imports are copied in, the source path is remembered, and "Re-import" pulls in hand edits.
5. **Export:** per-design/bundle archive plus the built standard packages; no store upload.
6. **Naming:** free display names, install IDs with a fixed app prefix. App name: open, brainstorming in the "Kamen" direction (anime, gaming or IT; the themes mask the loading screens).

Findings *(Claude)*:
- GRUB 2.14 scales the background (`desktop-image-scale-method`) and `image` components (to their box, which can be in %) at boot. `.pf2` fonts are fixed pixel sizes, so text size relative to the screen changes with the resolution. GRUB draws in software, so a 4K menu can feel sluggish.
- Plymouth 24 two-step loads `background.png` and has `ScaleBackgroundImage`, so one background can scale to any screen; logo and spinner stay at a fixed pixel size.
- KSplash QML scales freely; the generated splash can pick a pre-rendered video size per screen.
- Ubuntu archive: `kasumi` is a taken package name; kamen, omote, henshin, noren, kitsune, tatemae, transmog, cutscene, persona and facade are free.

## Round 5: name, resolutions, monitors (answered 2026-11-05)

1. **Name:** preference Persona > Omote > Transmog. "persona" alone is crowded on GitHub, so persona compounds are being explored; fallback is Omote.
2. **Resolutions:** 2560×1440, 1920×1080 and 1280×720 for every target; 4K is dropped (supersedes round 4). GRUB fonts are sized for 1440p and GRUB tries 1440p → 1080p → 720p.
3. **Monitors:** the HDMI monitor mirrors the main screen during GRUB. Plymouth and KSplash show the same splash on both screens.

Findings *(Claude)*:
- One initramfs build took ~8 s here (dpkg.log trigger runs on Oct 14 and Oct 23), so rebuilding all 5 kernels takes ~40 s.
- Persona compounds by GitHub repo-name count: Bootsona 0, PersonaStage 1, BootPersona 1, Personarium 5, Personae 153, Impersona 1232 (dropped).

## Round 6: apply, undo, privileges (answered 2026-11-05)

**App name: Personae.** *(Claude: IDs follow from it: library `~/.local/share/personae/`, installed GRUB/Plymouth folders `personae-<slug>`, Plasma splash ID `io.github.nomoruyi.personae.<slug>`, polkit action `io.github.nomoruyi.personae.apply`.)*

1. **Root access:** the GUI never runs as root; a fixed-operation helper runs via `pkexec` with an installed polkit policy file (clear prompt text, authorization kept for a few minutes).
2. **Initramfs scope:** rebuild all installed kernels (~40 s here).
3. **Undo:** system history; every apply is a restorable entry, plus a pinned "Original" entry (the state before the first apply). Backups cover only the config files the app touches; initrds are rebuilt, not copied; installed themes stay while any entry references them. Keep the last 10 entries plus Original.
4. **Bundle applies:** all-or-nothing; a failure rolls the whole bundle back automatically.
5. **Changes made outside the app:** detected from the real system state ("differs from last apply"), never overwritten silently; the owner can adopt the current state or re-apply.

Fixed by the ground rules: a dry run with file diffs and commands before every apply, validation of generated files, logging of every privileged action.

## Gap analysis (2026-11-05)

A read-only workflow reviewed what a full desktop app still needs from six angles (UX flows, editors and previews, settings and integration, robustness, stack and packaging, scope), merged the results and had them critiqued. Full output: `notes/gap-analysis-2026-11-05.md`. Its 14 draft questions became round 7 plus the defaults below; re-checked facts went into `CLAUDE.md`.

Findings *(Claude)*:
- No attached disk has a Windows partition, but the ESP still holds Windows Boot Manager files (Jan 2026) and the firmware still has a boot entry for it. os-prober is enabled, so the GRUB menu probably lists a dead Windows entry.
- System Settings' Boot Splash page sets both Plymouth inputs but rebuilds only the newest kernel. Here the 7.0.0-27 and -29 initrds (22 Sep) predate the switch to fallout (25 Sep), so those kernels likely still show the older splash.
- Under dracut the alternative also picks the theme (`CLAUDE.md` corrected). Whether two-step's live text renders under dracut is unverified.
- The splash's first ~3.5 s ran on the firmware framebuffer at GRUB's video mode, then re-laid out when amdgpu took over. Fixed by the early-KMS side task below; previews need only the native mode.
- Timeshift snapshots of `@` keep replaced initrds, ~470 MB per Plymouth apply, until they rotate.
- two-step supports a progress bar (`UseProgressBar` plus colour, size and alignment keys), so R3 Q5's "spinner or progress bar" both work.
- Limits: two-step's live text is always white Ubuntu; GRUB menu entries can't be centred.

## Round 7: stack, scope, history, navigation (answered 2026-11-05)

**Answer:** all of Claude's recommendations accepted: C++ / Qt 6 Quick + Kirigami with a C++ root helper; milestone 1 manages existing themes, then splash designers, then the GRUB designer; CLI in v1; history in the library under `/home`; sidebar per stage with an Overview; bundles are called "Personas" in the UI; Windows is a leftover. The defaults below stand.

1. **Stack:** will the owner read or edit the code, and in which language? *(Claude: C++ with Qt 6 Quick + Kirigami, CMake, QtTest, and a small C++ root helper. Most build tools are installed; the missing dev packages are only needed for optional parts: Kirigami Addons, QtMultimedia, KF6 I18n/Notifications/DBusAddons/Archive. Python + PySide6 is quicker to write but needs `python3-pyside6.qtquick` and related packages, and shows QML errors only at runtime.)*
2. **First usable version:** milestone 1 manages existing themes only (import, bundles, apply with review, history and undo, outside-change detection; no designers), then the Plymouth and Plasma splash designers, then the GRUB designer; v1.0 = all three. *(Claude: yes. The risky part can be proven with existing themes, and the splashes are what's broken today.)*
3. **Command line in v1:** `personae status | apply <bundle> | restore <entry|original>` with the same review and a y/N prompt; designing stays GUI-only. *(Claude: yes. Console recovery through the helper exists either way.)*
4. **System history location:** (a) in the library under `/home`: one history for all three stages, survives a reinstall, only a short-lived crash journal on the system during an apply; the helper re-validates every restore like an apply. (b) Root-owned in `/var/lib/personae`: rolls back with `/` under Timeshift, lost on a reinstall, and Plasma-splash-only entries still need a store in `/home`. *(Claude: a.)*
5. **Main window:** opens on an Overview (three stage cards in boot order with status and detected toolchain); sidebar Overview · Boot Menu · Boot Splash · Plasma Splash · Bundles · Assets · History, with Settings and About at the bottom; apply, restore and first run take over the window. Alternative: one combined library grid with a stage filter. *(Claude: sidebar per stage.)*
6. **Bundle name in the UI:** "Persona" or "Bundle"? *(Claude: Persona, it fits the app name.)*
7. **Windows:** gone for good, or on a disk that is sometimes connected? It changes the 0 s warning and the sample menu. *(Claude: treat it as a leftover.)*

Defaults unless the owner objects *(Claude)*:
- Install as a locally built `.deb` (app, root helper, polkit policy); an AppImage can't install root-owned files. Removal leaves applied looks working; an in-app "Restore Original and remove Personae files" cleans the system.
- GRUB keys live in `/etc/default/grub.d/zz-personae.cfg`. `/etc/default/grub` is never edited, and the app warns when its file overrides an edit made there.
- The menu timeout is one machine-wide setting on the Overview's Boot Menu card, applied through review and history. Personae manages `GRUB_TIMEOUT`, `GRUB_RECORDFAIL_TIMEOUT` and `GRUB_TIMEOUT_STYLE`; 0 s requires an "I can recover with a live USB" tick. Where GRUB can write to `/boot` (possible after a reinstall), 0 s keeps the menu after a failed boot.
- `GRUB_GFXMODE=2560x1440,1920x1080,1280x720,auto`.
- Installed, unchanged themes (fallout, mint-galaxy, Overload, hollow-grub, crossgrub) are selected in place; only broken, nested or edited ones get a cleaned-up copy under a Personae ID.
- Re-applying an edited design installs a new versioned folder, so every history entry stays restorable; versions no kept entry uses are removed after the next successful apply.
- Apply order: Plasma splash first (no root, undo without root), then GRUB and Plymouth in one helper run; any failure rolls the whole bundle back.
- Plymouth designs offer a progress bar besides the spinner. Shutdown and reboot use the boot-up look, with optional per-mode text such as "Restarting…" (white Ubuntu).
- English UI, text prepared for translation.
- No daemon, tray icon, autostart or apt hook; checks run when the app opens or gets focus.
- First run checks the kernel cmdline for `splash`, detects the toolchain and offers to import the installed themes. Original is saved per stage right before Personae first changes that stage.
- Imported Plasma splashes are QML code that runs at every login; Personae doesn't sandbox them, and the import report says so.
- Non-goals: editing GRUB entries or kernel parameters, theme stores or any network use, scheduled rotation, lock and logout screens, other user accounts.

## Side task: early KMS for amdgpu (outside the app)

The owner wants amdgpu in the initramfs now, for the current setup. Not a Personae feature; the owner runs the commands. *(Claude, checked read-only:)*
- Today the splash starts at 8.35 s on simpledrm; amdgpu loads from the real root (10.77 s) and takes over at 12.33 s.
- Adding `amdgpu` to `/etc/initramfs-tools/modules` packs the module (~6.7 MB) and all 692 firmware files it lists (~27 MB, already zstd-compressed), so each initrd grows from ~94 MB to ~125 MB; ×5 kernels plus Timeshift copies.
- `/etc/initramfs-tools/conf.d/calamares-safe-initramfs.conf` (root-only) contains `UMASK=0077` (owner confirmed), which explains the 0600 initrds.
- After the change, re-check the amdgpu bullet in `CLAUDE.md`: previews then need only the native mode, and Personae must keep amdgpu in mind when it rebuilds initrds (it doesn't remove it).
- **Done 2026-11-05 (7.0.0-42 only):** amdgpu now replaces simpledrm ~25 ms after it starts, before plymouth-start; initrd 128 MB. The owner then rebuilt all five (`-k all`, 21:52); all initrds are ~128 MB.

## Round 8: editors, assets, settings (answered 2026-11-05)

**Answers:** Q1–3, Q5, Q6 and the defaults as recommended. Q7 as recommended, and nothing else for now; settings get added when a real need shows up. Q4: keep the 15 MB cap, but allow animations longer than 4 s using lossy compression (see below).

Wireframes: `notes/wireframes-round8.md` (Overview, stage page, editor, apply review).

1. **Placing elements:** drag on the canvas with snapping (centre, edges, other elements), plus number fields and arrow-key nudging. Positions and sizes are stored as % of the screen, so one layout fits 1440p/1080p/720p; GRUB font sizes are derived per resolution (`.pf2` is fixed-size) with an optional per-resolution override. *(Claude: yes.)*
2. **Saving:** explicit Save (Ctrl+S); each save is a snapshot you can go back to under Versions; undo/redo per open design for the session; one crash-recovery draft. Alternative: autosave with no Save button. *(Claude: explicit Save.)*
3. **Plasma splash video:** (a) one silent MP4 played by QtMultimedia, GPU-scaled, with a still of the first frame until it plays; the module is guaranteed by plasma-workspace. (b) animated WebP like Overload: decoded on the CPU, Overload's peaked at ~1.5 GB RAM, and WebP support is only a recommended package. *(Claude: a.)*
4. **Boot splash animation size:** two-step loads every frame into RAM and every initrd carries the theme (×5 kernels, plus Timeshift copies). Cap a theme at ~15 MB, warning before the cap, and convert video to at most ~4 s of looping frames? *(Claude: yes.)*
5. **Shared elements** (R3): linked, so editing the shared logo or palette updates every design that uses it (applied looks change only when re-applied; the Overview shows "update available"), with per-design Unlink. Alternative: copied on use. *(Claude: linked.)*
6. **New designs** start from a few starters tinted with the Plasma accent colour, from blank, or by duplicating a Personae design. Imported third-party themes stay read-only. *(Claude: yes.)*
7. **Settings page:** library location, history size (10 + Original), technical details (file diffs and commands) expanded by default or not, default preview resolution. Nothing else. *(Claude: yes; technical details collapsed.)*

Defaults unless the owner objects *(Claude)*:
- Fonts chosen from installed system fonts are copied into the library on first use, with their licence file where one exists, so a reinstall can't break a design.
- GRUB terminal font stays GRUB's built-in Unifont; no extra `.pf2`.
- Deleting moves items to the desktop trash with an inline Undo; items used by a Persona, history entry or snapshot can't be deleted.
- The Plasma splash editor has a "Run real splash" button (`ksplashqml --test --window`); the two boot stages get app-drawn previews.

Q4 follow-up *(Claude, checked read-only)*:
- Length is limited by frame size, not by a fixed number of seconds. ffmpeg's `palettegen`/`paletteuse` can make 8-bit palette PNGs (lossy, dithered); pngquant and optipng aren't installed.
- Rough estimates, to be measured in a spike: a centred ~512×512 animation at ~50–100 KB per frame fits ~150–300 frames in 15 MB, i.e. ~5–10 s at two-step's frame rate. A full-screen 1440p video frame is ~1–2 MB even when quantized, so a full-screen Plymouth video doesn't fit.
- RAM: decoded frames take width × height × 4 bytes each (~1 MB per 512×512 frame, ~15 MB per 1440p frame); 30 GB is installed, so RAM isn't the limit, but loading time delays the first frame.
- The converter shows the maximum length for the chosen size and quality before it builds. A lower frame rate (each frame shown longer) needs the `script` module; two-step plays at a fixed rate (to verify).

## Licence note (2026-11-05)

If Personae ever needs code from a GPL project, the owner is fine with switching the project to the GPL (sole copyright holder, so a relicence is possible). Until then it stays MIT. Unlicensed projects stay ideas-only either way.

## Name check: "Ikhue" (2026-11-05)

The owner wants to rename the project to "Ikhue", from a machine translator (polytranslator, `bin_Latn`, the NLLB-200 language code) giving it as Bini for "mask". *(Claude: it isn't.)*
- Melzian, *A Concise Dictionary of the Bini Language* (1937), has **Ikhue** (written *Ixue*; Melzian writes `x` for today's `kh`) as the name of two Bini villages with shrines, and **ẹkhuẹ** (*exue*) as "shame" and "ashes of a farm fire".
- The likely source of the mistranslation: Benin hip masks are called **uhunmwu-ẹkuẹ** (Digital Benin; NOMA spells it *uhunmwaun-ekhue*). **Ẹkuẹ** means "waist/hip" (Melzian: "(1) waist"), so the phrase is roughly "head of the hip": a hip mask.
- No general Bini word for "mask" was found (Melzian has no entry; Glosbe has none). Masks are named by type or masquerade: *uhunmwu-ẹkuẹ* (pendant/hip mask), *Aro Ododua* (ritual masks of the Ododua masquerade; *aro* = eye/face), *Ekpo* and *Ovia* (masquerade societies).
- **Decision:** "Personae" stays as a placeholder. The owner will ask a Nigerian contact; a final name replaces it everywhere (IDs, paths, polkit action) before the first release.

## Boot splash animation: scenes instead of video (2026-11-05)

- The converter shows both costs before it builds: file size (counts against the 15 MB cap; copied into every initrd) and RAM (frames decoded at width × height × 4 bytes).
- Owner idea: Hollow Knight–style title screens (still artwork plus ambient motion). *(Claude: fits the `script` module: one background, a logo and small sprites (particles, light motes, fog strips) animated by code at 50 Hz. About 2–5 MB on disk and ~20–40 MB RAM at 1440p, with no length limit since it's generated each frame. Limits: no blur or shaders, CPU drawing only, so large full-screen moving layers must be checked for smoothness in a spike. Proposal: "scene" designs (layers + particle emitters + simple motion paths) as the main Plymouth animation type; frame sequences only for small centred animations.)*
- Full-screen video for comparison: 8 s at 30 fps at 1440p = 240 frames ≈ 3.5 GB RAM and ~250–500 MB on disk even with lossy palette PNGs (×5 initrds). At 15 fps it's still ~1.8 GB RAM. Not feasible in Plymouth; the Plasma splash can play it as MP4.
- **Decided (owner):** scenes are the main boot splash animation type; frame sequences stay for small centred animations only.

## Round 9: after apply, backup, sharing, GRUB and Plymouth details (answered 2026-11-05)

**Answers:** Q1–4 and Q7 as recommended. Q5 and Q6 changed, plus a note on GRUB entry text (below).

1. **Check after reboot:** after an apply that touched a boot stage, the next time the app opens it asks "Did the new look work?" with *Keep* / *Revert to previous*. Alternative: a one-shot login notification (needs a temporary autostart file, which round 7's "no autostart" excludes). *(Claude: ask on next app start.)*
2. **Library backup:** the library lives in `/home`, which Timeshift doesn't snapshot; Kup is installed but not set up. Options: (a) leave backups to you, (b) an "Export library" archive in v1.0, (c) both plus a reminder after N new designs. *(Claude: b; set up Kup separately if you want /home backed up.)*
3. **Exporting a design** as a plain theme package (`.tar.gz` for GRUB/Plymouth, `.zip`/`.tar.gz` look-and-feel for the Plasma splash) to share or use on another machine. *(Claude: yes in v1.0; it's the build output, zipped.)*
4. **GRUB menu elements:** background, title or logo image, the entry list (font, colours, selected-entry style: flat colour, rounded box or glow, all baked into images), per-entry icons, countdown (text, progress bar or both), help line at the bottom. Icons: a small built-in monochrome set (Linux, Kubuntu-style, Windows, firmware settings, memtest) plus your own imports. Anything to add or drop? *(Claude: as listed.)*
5. **Plymouth messages:** status text (fsck progress, "Press Ctrl+C to cancel" checks, errors) shown only when there is one, in a fixed spot under the logo; otherwise only the animation. *(Claude: yes.)*
6. **First real applies:** staged: Timeshift snapshot first, a live USB at hand, then GRUB only → reboot → check; Plymouth only → reboot → check; then a whole Persona. Do you have a bootable live USB? *(Claude: required before the first Plymouth apply.)*
7. **Generated theme metadata:** author "nomoruyi" and licence MIT on every generated package, editable per design; fonts and imported art keep their own licences, and exports warn when a licence forbids sharing. *(Claude: yes.)*

Facts *(Claude, read-only)*: GRUB menu scripts present: `10_linux`, `20_memtest86+`, `30_os-prober`, `30_uefi-firmware`, `35_fwupd`, `40_custom`, `41_custom`; so the sample menu shows kernels, Advanced options, memtest, the leftover Windows entry, UEFI Firmware Settings and possibly fwupd.

Changes from the owner:
- **Q5 Plymouth messages:** each design gets a "Hide status messages" checkbox (off by default). *(Claude: password and emergency prompts can't be hidden, since they need input; Esc still shows the boot details. On btrfs the boot-time fsck is a no-op, so hidden progress rarely matters. Whether two-step can suppress messages is unverified; if it can't, designs with hidden messages use the `script` module.)*
- **Q6 First run:** the owner makes a live USB before the first try. On first start the app shows an intro dialog: what each stage is, how to back up first (Timeshift snapshot, live USB, library export), and how to get back to a working system (in-app restore, `personae restore original`, an older kernel from GRUB's Advanced options, Timeshift from the live USB). It has a "Don't show again" checkbox, off by default, and stays reachable from Help.
- **GRUB entry text must never be cut off** (long entries under Advanced options, e.g. "Kubuntu, with Linux 7.0.0-42-generic (recovery mode)"). *(Claude: GRUB's `boot_menu` draws each entry on one line and clips it; it can't wrap, so multiline isn't possible. Instead, the menu width adapts: at build time Personae measures the longest expected entry with the real `.pf2` glyph widths at each resolution, adds icon width and padding plus headroom for longer future kernel versions, and widens the menu box up to full width. If it still doesn't fit, it warns and offers a smaller font for that resolution. Entry titles come from the kernels in `/boot`, `GRUB_DISTRIBUTOR` and the menu scripts, since `grub.cfg` is root-only; the helper re-checks against the real `grub.cfg` after `update-grub`.)*

## SPEC.md drafted (2026-11-05)

Correction *(Claude)*: round 8 Q1's per-resolution GRUB font sizes aren't possible. One `theme.txt` serves every video mode, and choosing fonts per mode would need an `/etc/grub.d` script, which round 2 ruled out. Listed as open question 4 in `SPEC.md`.

## Name decided (2026-11-06)

**Ekpo.** The owner's Nigerian colleague confirmed "Ekpo" is used for "mask". *(Claude: Melzian (1937) lists Ekpo as a Bini masquerade society; no Ubuntu package or command is named ekpo.)* Renamed everywhere except this log and `notes/`: app and binary `ekpo`, QML module `Ekpo`, library `~/.local/share/ekpo/`, IDs `io.github.nomoruyi.ekpo.*`, installed folders `ekpo-<slug>`, GRUB drop-in `zz-ekpo.cfg`, journal `/var/lib/ekpo/`, env vars `EKPO_*`. Bundles stay "Personas" in the UI (round 7).
