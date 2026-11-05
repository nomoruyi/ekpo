# Gap analysis, 2026-11-05

Raw output of the read-only gap-analysis workflow: six lens reviews, a synthesis, a critic and a model/mode research agent. This is Claude's analysis, not owner decisions. Where the synthesis and the critique disagree, the critique wins; owner answers in `INTERVIEW.md` win over both. Facts re-checked by hand are in `CLAUDE.md`.

Sections: 1. Critique · 2. Synthesis (gap map, proposed defaults, draft rounds, MVP boundary, UX sketch, merge notes) · 3. Lens reviews · 4. Model and mode research

## 1. Critique

### Corrections

1. **[high]** History storage contradicts itself. Q9.4 puts all system history and backups in root-owned /var/lib/personae. But a Plasma-splash-only apply skips polkit, and the Plasma splash part of Original is captured without root, so neither can be written there. The History page still shows one list with three stage icons per entry. Q9.4's context also overstates the trust gain: a restore is a plan of typed values that the helper validates exactly like any apply.
   - **Fix:** Pick one model before SPEC and reword Q9.4 around it. Either (a) all history lives in the user library: it survives a reinstall and is one store; the helper keeps only its crash journal and rollback backups root-owned, and a restore from a text console goes through the CLI. Or (b) root-owned entries for GRUB/Plymouth that also store the Plasma-splash values as data, plus user-side entries for Plasma-only applies, merged on one counter. In both cases, say where the Plasma splash part of Original lives.
2. **[medium]** The Plasma splash is applied last (step 10). If that step fails, the GUI needs a second privileged call to roll back the system part, and that only skips the password prompt while the kept polkit authorization is still valid.
   - **Fix:** Apply the Plasma splash first: it needs no root and can be undone without root. A helper failure is then rolled back by the helper itself, and the GUI just puts ksplashrc back. No privileged rollback is ever triggered by an unprivileged step.
3. **[medium]** zz-personae.cfg permanently overrides any later edit to the same keys in /etc/default/grub, whether by the owner or by Grub Customizer. Drift checks compare only the effective state, so they never see this. That breaks R6 Q5: 'never overwritten silently'.
   - **Fix:** Also compare the managed keys in /etc/default/grub with their values at the last apply, and report 'your GRUB_TIMEOUT edit in /etc/default/grub is overridden by Personae'. The Boot Menu card shows which file each effective value comes from.
4. **[medium]** Every imported theme is installed as a cleaned-up copy under a Personae ID, even one already installed and working (fallout, mint-galaxy, Overload). That adds a duplicate folder and alternative, puts two 'fallout' entries in System Settings, and rewrites ImageDir/ScriptFile, which is close to R2 Q3's ban on editing third-party themes. The stated reason is weak: fallout's extra files (install.sh, uninstall.sh, preview.sh, README, LICENSE, INSTALL) total about 38 KB and never run.
   - **Fix:** Default: select the existing folder while its hash still matches the import. Install a cleaned-up copy only for nested or broken themes, hand-edited copies, or folders that changed or disappeared. Or ask the owner.
5. **[medium]** R3 Q5 gives Plymouth designs 'a centered spinner or progress bar', but the synthesis covers only spinners for boot-up. two-step 24.004 has UseProgressBar and ProgressBarForegroundColor, ProgressBarBackgroundColor, ProgressBarHorizontalAlignment and ProgressBarVerticalAlignment (found in the strings of two-step.so).
   - **Fix:** Add a progress-bar option to the Plymouth designer, generator and preview. Note in SPEC that boot progress is estimated from earlier boots.
6. **[medium]** The dracut facts are wrong or missing. (a) CLAUDE.md says plymouthd.conf decides under dracut. Here, plymouth-populate-initrd takes the theme name from the default.plymouth alternative; Theme= wins only when ThemeDir= is also set (set_theme_dir). (b) dracut's 45plymouth/module-setup.sh runs it with 2> /dev/null and ignores its exit status, so there is no 'W: plymouth' signal. (c) It packs only the files named by Font=, TitleFont= and MonospaceFont=, plus the fc-match defaults (Noto Sans and DejaVu Sans Mono here). No dracut module packs /etc/fonts, and only label-pango is installed, so live text probably doesn't render under dracut.
   - **Fix:** Keep writing both inputs. Under dracut, treat the lsinitrd listing as the only failure signal. Write Font=Ubuntu N explicitly and check for the fontconfig files in the listing. Mark live text under dracut as unverified in SPEC (the reinstall will likely use dracut), and correct CLAUDE.md.
7. **[medium]** Dual boot is ignored. Windows Boot Manager is on the ESP (efibootmgr Boot0000), os-prober is installed and GRUB_DISABLE_OS_PROBER=false, so the menu lists Windows. The 0 s warning and the sample menu don't account for it.
   - **Fix:** The 0 s warning says Windows and the other entries are then reachable only through the firmware boot menu. The sample menu includes os-prober, memtest and UEFI firmware entries, or the real titles recorded by the root verify step. Per-entry icons cover their classes (windows, memtest, efi).
8. **[medium]** A limit the owner should see is missing. On EFI, 10_linux keeps GRUB's video mode for the kernel, and amdgpu isn't in the initramfs. So for the first ~3.5 s the splash runs on simpledrm at whatever mode GRUB set, on the firmware's mirrored output, then re-lays out per monitor when amdgpu takes over. GRUB_GFXMODE, including the new 'auto', therefore also sets the resolution of the splash's first seconds.
   - **Fix:** Add this to note 11's limits and to preview and validation: show a Plymouth design at the GRUB mode as well as at native resolution. Propose no host change.
9. **[medium]** Fourteen questions sent at once as rounds 7–9 breaks CLAUDE.md's 'rounds of at most 5–7 focused questions'. Several are already settled. Q9.1: R1 Q3 asks for 'a timeout setting'. Q9.2: R2 Q1 already decided what 0 s means; keeping the menu after a failed boot costs normal boots nothing and doesn't apply on btrfs. Q9.5: follows from R6 Q3 ('installed themes stay while any entry references them').
   - **Fix:** Send one round of 7 questions or fewer now. Turn Q9.1, Q9.2 and Q9.5 into stated defaults, and possibly Q7.2 and Q9.6 too, and list them as defaults the owner can veto.
10. **[low]** /etc/plymouth/plymouthd.conf is a conffile of the plymouth package (dpkg -S; a plymouthd.conf.dpkg-old from Aug 2025 exists). A package upgrade can prompt about it or replace it, which drops Theme=.
   - **Fix:** Note it in SPEC. Drift names 'plymouth upgrade replaced plymouthd.conf' as a likely cause. Edits change only the Theme= line and keep the rest.
11. **[low]** The default.plymouth alternative carries a GRUB slave link (default.plymouth.grub). For Kubuntu, 05_debian_theme pastes it into grub.cfg: kubuntu-logo.grub sets background_color and color_normal, plus a theme= line that applies only if /boot/grub/themes/kubuntu-logo exists. So changing the Plymouth theme also changes grub.cfg.
   - **Fix:** Always regenerate grub.cfg when the alternative changes, including Distro default and Original restores. Verification also checks that no later theme= line overrides Personae's.
12. **[low]** The failure checks are fragile. 'Found theme:' is printed through gettext_printf, so it can be translated, and the grub.cfg check already covers it. Any 'W: plymouth' line also fails the apply, even a warning about the text theme or a plugin, which could make a restore to Original fail.
   - **Fix:** Drop the stderr check and rely on the grub.cfg check. Count only 'W: plymouth' warnings about the theme being installed.
13. **[low]** 'The package has no maintainer scripts' contradicts Q9.4's default 'removed on purge'.
   - **Fix:** Add a postrm purge script, or drop 'on purge' and rely on the in-app 'Remove Personae files'.
14. **[low]** The fixed Ubuntu Mono terminal font is more than needed and breaks R3 Q2's 'one font'. 00_header already loads /boot/grub/fonts/unicode.pf2.
   - **Fix:** Default to terminal-font 'Unifont Regular 16', which needs no extra .pf2. Build Ubuntu Mono only if the owner wants a larger terminal font.
15. **[low]** 'GRUB 2.14 has no background-colour property' is imprecise: gfxmenu.mod has desktop-color.
   - **Fix:** Say that the menu and its selected entry have no background-colour or alignment property, and use desktop-color for plain-colour backgrounds.
16. **[low]** The GUI can't check whether GRUB can write to /boot 'the same way 00_header does': grub-probe exits 1 for a normal user (tested). CLAUDE.md's GRUB facts also leave out GRUB_RECORDFAIL_TIMEOUT=20, which is the value that sets today's timeout.
   - **Fix:** The GUI uses findmnt and lsblk for the review text, and the helper confirms with grub-probe. Add RECORDFAIL_TIMEOUT=20 and the 00_header EFI override to CLAUDE.md.
17. **[low]** Timeshift 25.12 runs in btrfs mode on @ (not @home), with hourly, daily, weekly, monthly and boot snapshots. Each Plymouth apply therefore leaves about 470 MB of old initrds (5 × ~94 MB) held in snapshots until they rotate.
   - **Fix:** Mention this in the free-space check and the size meter; '2 × largest initrd' covers only the space needed during the apply.
18. **[low]** Generated Plymouth themes ship preview.png, but the hook copies the whole theme folder into every initrd.
   - **Fix:** Keep preview.png small (about 640×360) and count it in the size meter.
19. **[low]** Q7.1's context 'every build tool is already installed' is true only if the C++ app avoids KF6 C++ APIs whose dev packages are missing: libkf6dbusaddons-dev, libkf6notifications-dev, libkf6archive-dev, libkf6i18n-dev, kirigami-addons-dev and qt6-multimedia-dev are not installed.
   - **Fix:** Name the missing packages in the question and in the repo's build-dependency list.
20. **[low]** The synthesis says the helper holds dpkg's lock-frontend and lock; dpkg uses fcntl locks, so taking them with flock won't keep apt out.
   - **Fix:** Take them with fcntl (F_SETLK or OFD locks), lock-frontend first, as apt does.
21. **[low]** Some items are speculative or more than needed: the Secure Boot font warning (Secure Boot is off, and the claim is unverified), a custom colour picker, the Compare view, the reload-or-overwrite prompt for library files, and rebindable shortcuts.
   - **Fix:** Cut them or move them to phase 3. Use the platform ColorDialog plus the swatch row, and fixed KStandardShortcut shortcuts in v1.
22. **[low]** Q8.1 asks an open 'does it fit?' about a long text sketch. Q8.4 leaves out the cheap middle option.
   - **Fix:** Show the sketch as a page or wireframe and ask the two choices that matter: the launch view, and one grid per stage versus one library. In Q8.4, offer per-mode Title/SubTitle text in two-step (white Ubuntu, e.g. 'Restarting…'), which needs no script module.

### Missing areas

- Owner procedure for the first real applies (R3 Q1): GRUB-only apply, reboot, Verify; then Plymouth-only; then a bundle. Take a Timeshift snapshot first (Timeshift is already set up in btrfs mode) and keep a live USB at hand. RESTORE.txt also needs a live-USB chroot recipe for this layout: btrfs subvol @ with /boot inside it, vfat ESP shared with Windows.
- Read-only kernel command line check at first run and after a reinstall: 'splash' (and 'quiet') in /proc/cmdline and GRUB_CMDLINE_LINUX_DEFAULT. Without it there is no graphical splash at all. Report and explain; never edit the command line. Present today.
- Dual-boot awareness for the GRUB stage: Windows and other os-prober entries, the firmware boot menu as the only way in at 0 s, icon classes, and the extra grub-mkconfig time os-prober adds.
- Plymouth's first ~3.5 s on simpledrm at the mode GRUB left, and the re-layout when amdgpu takes over: document it as a limit and add it to the preview.
- One history model that covers the Plasma splash: where Plasma-only entries and its part of Original live.
- Trust of imported Plasma splashes: they are QML code that runs at every login and in 'Run real preview'. The import report should say Personae doesn't sandbox them.
- Timeshift beyond rollback detection: space held by snapshots after each Plymouth apply, and the case where a Timeshift restore leaves the library on /home (never snapshotted) newer than the system.
- Later desktop-integration extras: a MIME type for Personae archives, Dolphin 'Open with Personae' for theme archives and folders, AppStream metainfo.
- A non-goals line on multiple users: the Plasma splash is per user, and another account's applies show up as outside changes.
- The owner asked whether Opus 5.5 max is enough or whether to switch to Fable or Ultracode. The synthesis doesn't cover it. The reply must answer it, using the claude-api skill reference rather than memory.

### Verdict

The synthesis is thorough and mostly faithful to the six rounds in /extra/data/Coding/linux-theming/INTERVIEW.md: no 4K, no VM, no extra /etc/grub.d script, two-step for v1, no Plymouth font hook. Most of its technical claims hold on this machine, checked read-only: on EFI with /boot on btrfs, 00_header's recordfail override applies (GRUB_RECORDFAIL_TIMEOUT=20 sets today's timeout); grub-mkconfig rewrites grub.cfg in place with cat; update-initramfs stops at the first failure; the hook copies whole theme folders; two-step has background.png and ScaleBackgroundImage and no text-colour key; kubuntu-logo's priority of 150 makes it the auto choice; plasma-workspace pulls in QtMultimedia with its FFmpeg backend; Timeshift runs in btrfs mode. Before SPEC, fix the one structural contradiction: a root-owned history can't hold Plasma-splash-only entries that run without root, and Q9.4 needs rewording to match. Apply the Plasma splash first. Close the hole where the drop-in silently overrides later edits, and stop duplicating imported themes that are already installed. Add the progress-bar option R3 Q5 allows. Correct the dracut facts, which CLAUDE.md also gets wrong and which matter after the likely dracut reinstall. Add the dual-boot and simpledrm limits. Then send one round of at most 7 questions, with the rest as stated defaults, instead of 14 at once.

## 2. Synthesis

### Gap map

**Tech stack and development workflow**

- Language, UI toolkit, test framework [Q7.1]
- Dev builds sandboxed by construction: HOME/XDG/TMPDIR, fake system root, stub tools [default]
- Headless screenshot mode for AI-assisted UI work [default]
- Golden, QML smoke and helper-rollback tests [default]
- Build-dependency list that survives a reinstall [default]
- Image/video toolchain without ImageMagick [default]
- Build, test, lint and single-test commands for CLAUDE.md [after Q7.1]

**Packaging, helper and removal**

- Install, update and removal method [Q7.2]
- Helper path, number of polkit actions, prompt text [default]
- GUI/helper version handshake; read-only mode without a matching helper [default]
- Removing everything Personae put on the host [default]

**MVP boundary and scope**

- First usable build and milestone order [Q7.4]
- Command line in v1 or later [Q7.3]
- Non-goals for SPEC.md [default]

**Host footprint and system writes**

- Where system history and backups live [Q9.4]
- Installed folders: one per design, or immutable versions [Q9.5]
- GRUB keys in their own drop-in or in /etc/default/grub [Q9.3]
- Timeout keys for the detected /boot, including GRUB_TIMEOUT_STYLE [default]
- 0 s on a GRUB-writable /boot: keep the failed-boot menu? [Q9.2]
- plymouthd.conf edit; alternative priority, mode and restore order [default]
- Restores scoped to Personae's keys, not whole files [default]
- Last-resort 'auto' in GRUB_GFXMODE [default]
- Locale grub-mkconfig runs with [default]

**Apply transaction and safety**

- What the helper accepts and validates [default]
- Step order, transaction journal, recovery after a crash or power loss [default]
- Safe grub.cfg write, syncfs, one kernel at a time [default]
- Silent theme drops treated as failures [default]
- Locking against apt, Discover, kernel hooks and second instances [default]
- Free-space preflight and theme size limits [default]
- Verifying grub.cfg and every initrd after apply [default]
- Skipping the rebuild when Plymouth is unchanged [default]

**System history, Original and restore**

- When Original is captured (per stage) [default]
- What 'restore entry N' means; restoring single stages [default]
- Showing what gets pruned; rolled-back attempts [default]
- Theme folders missing at restore time [default]
- Recovery without the GUI: console commands, RESTORE.txt [default]
- After a reinstall: new Original, re-apply the last bundle [default]

**Drift and health checks**

- When checks run; no daemon, tray, autostart or apt hook [default]
- What can be checked without root [default]
- Naming the likely outside writer: Boot Splash page, Global Theme, Timeshift [default]
- What Adopt does, including a mixed Plymouth state [default]
- Checks after kernel, package or generator changes; Secure Boot [default]

**Screens and flows**

- Main window, navigation and launch view [Q8.1]
- Apply/restore pipeline screens [default]
- How failures, rollbacks and unfinished applies are shown [default]
- First-run and welcome-back page [default]
- Empty views and progress for background work [default]
- 'Bundle' or 'Persona' in the UI [Q8.2]
- Keep-or-revert prompt after the first reboot [phase 3]

**Design editors (all stages)**

- Canvas interaction: drag, or fields only [Q8.3]
- Save, snapshots, undo/redo, crash draft [default]
- Preview source and fidelity per stage [default]
- Validation at 1440p/1080p/720p; what warns and what blocks [default]
- Colour picking per stage [default]

**GRUB designer**

- Colours, shadows and rounded selection compiled into 9-slice images [default]
- .pf2 naming, glyph subset, terminal font [default]
- Sample menu without reading the root-only grub.cfg [default]
- Guard against unknown global theme keys [default]

**Plymouth designer**

- Mandatory prompt images; UseEndAnimation in every mode [default]
- Shutdown, reboot and update screens [Q8.4]
- Live text fixed to white Ubuntu [default]
- Fixed 2.0 s spinner loop; spinner import and generator [default]
- Free layers flattened into the background or the watermark [default]
- Initramfs size meter [default]

**Plasma splash designer**

- Video format: MP4 via QtMultimedia or animated WebP (spike) [default]
- Splash.qml rules that survive Qt and Plasma upgrades [default]
- In-app preview for designs, external ksplashqml for imports [default]
- Bundled fonts, preview image, staying out of Global Themes [default]

**Asset library and imports**

- Content-addressed masters, per-use edits, derived-file cache [default]
- Fonts as assets; provenance and licences [default]
- Import sources (archives, Personae exports) and their guards [default]
- Import report, verdict, duplicates, Re-import diff [default]
- Normalized installed copy of imports; library copy kept verbatim [default]
- Starter designs and MIT-safe spinner art [default]

**Bundles**

- Slot values beyond 'Leave as is': Distro default, None [default]
- Single design or import applied as a one-slot bundle [default]
- Broken references; deleting items in use [default]

**Settings**

- App preferences vs machine boot options [default]
- GRUB timeout machine-wide or per bundle [Q9.1]
- Library location: move it, or use the library found there [default]
- Author and licence in generated metadata [default]

**Desktop integration and polish**

- Native look in System Settings: preview images, metadata [default]
- Coexistence with the Boot Splash, Splash Screen and Global Theme pages [default]
- Notifications; closing the window mid-apply; shutdown inhibitor [default]
- Shortcuts; which undo Ctrl+Z means [default]
- Accessibility baseline [default]
- UI language [Q9.6]
- Single instance [default]
- In-app help, About, system report [default]

**Library lifecycle**

- Format versions and migrations [default]
- Snapshot retention and cleanup [default]
- Undoing deletions via the trash [default]
- Exporting and backing up the whole library [phase 3]

### Proposed defaults

1. **Original**

   Captured per stage, right before Personae first changes that stage. It records the managed values, including keys that were absent and whether the user's ksplashrc key existed, plus a hash manifest of the foreign theme folders it points at. Restore Original restores only the stages that were captured; Adopt never changes it. On first run Personae offers, on by default, to copy the three active themes into the library.

   *Why:* Refines round 6's 'state before the first apply': a first apply that changes only the Plasma splash runs without the helper, and hollow-grub, fallout and Overload belong to no package.

2. **History entries and restore**

   Entries are numbered by a counter and record the full managed state after the apply. Restore returns to that state through the normal review flow, with a checkbox per stage, all ticked. It adds a 'Restored …' entry and never deletes newer ones. Restores write only Personae's keys (GRUB keys, Theme=, the alternative's mode and value, ksplashrc Engine/Theme); whole-file backups are used only to roll back a failed apply. Rolled-back attempts are shown greyed and don't count toward the 10. The review names any entry and folder about to be pruned. A missing theme folder is reinstalled from its library copy through the normal validated path; if there is no copy, that stage is blocked and the reason shown.

   *Why:* Delivers R6 Q3/Q4 without ever reverting unrelated later edits such as the kernel command line. A counter is not thrown off when dual-booting shifts the clock.

3. **After a reinstall**

   A new /etc/machine-id means a fresh system. A new Original is captured at the first change and earlier entries can't be restored. The library remembers the last applied bundle per machine, so the welcome-back page can offer 'Apply it again'.

   *Why:* /home survives a reinstall and / doesn't, so old entries point at kernels, paths and a boot-image tool that no longer exist.

4. **GRUB timeout keys**

   Detect whether GRUB can write to /boot the same way 00_header does (filesystem type, LVM/RAID, EFI).
   - N>0: GRUB_TIMEOUT=N, GRUB_RECORDFAIL_TIMEOUT=N, GRUB_TIMEOUT_STYLE=menu.
   - N=0: GRUB_TIMEOUT=0, GRUB_TIMEOUT_STYLE=hidden, GRUB_RECORDFAIL_TIMEOUT=0 (for a writable /boot see Q9.2).
   Show the effective result in one sentence with the raw keys below. At 0, show a hard warning and require an 'I can recover with a live USB' tick before Apply.

   *Why:* Verified in /etc/grub.d/00_header: on EFI with /boot on btrfs, RECORDFAIL and timeout_style=menu override the other keys, so today the menu shows for 20 s despite TIMEOUT=10 and STYLE=hidden. On a /boot GRUB can write to, the existing STYLE=hidden would hide the menu for any N.

5. **Plymouth inputs**

   Change only [Daemon] Theme= in plymouthd.conf, and always together with the default.plymouth alternative. Register Personae themes at priority 10 and select them with --set. A restore first puts back the recorded mode and value, then removes Personae's entries.

   *Why:* Both inputs must agree (CLAUDE.md). Priority 10 never wins in auto mode. Removing the selected entry first would switch the group to auto, which means kubuntu-logo.

6. **Imported themes on the host**

   The library copy stays exactly as imported. Applying an imported theme installs a cleaned-up copy under a Personae ID: scripts and docs dropped, symlinks resolved, images checked, ImageDir/ScriptFile rewritten. History entries never point at folders Personae didn't install, except Original and adopted states.

   *Why:* Keeps round 2 Q3's read-only rule. The initramfs hook copies the whole folder, so fallout's install.sh would otherwise end up in every initrd.

7. **Helper and polkit**

   One action, io.github.nomoruyi.personae.apply: auth_admin_keep for active sessions, no for any/inactive. exec.path is a single system path compiled into both the policy and the app (e.g. /usr/libexec/personae/personae-helper, root:root 0755). The GUI runs pkexec directly, never through a shell. Verbs: --version (unprivileged), apply, restore <entry|original>, recover, verify. They also work with sudo from a text console.

   *Why:* R6 Q1. polkit keeps an authorization per action and per caller, so with one action a failed Plasma-splash step can trigger the rollback without a second password prompt.

8. **Helper trust boundary**

   The helper never opens a path supplied by the GUI and never touches $HOME. Each request arrives on stdin as a JSON plan: the built files inline, plus hashes of the live inputs the review saw; on a mismatch it aborts with 'system changed since the review'. It validates its own root-owned copy:
   - regular files only, safe names, at most 2 directory levels
   - size caps and allowed extensions
   - PNG/JPEG/pf2 header checks
   - theme.txt properties against an allowlist
   - an exact 'ModuleName=' line
   It writes config files itself from typed values and checks the GRUB file with sh -n.

   *Why:* Whatever it installs lands in /boot and every initrd, and the GRUB config is sourced as root by every update-grub run, including apt's.

9. **Apply transaction**

   Write a journal before the first change and fsync after each step. Order:
   1. Locks and a logind shutdown/sleep inhibitor
   2. Preflight checks
   3. Backups
   4. Stage and validate
   5. Rename new folders into place
   6. Configs (temp file, fsync, rename) and the alternative
   7. Boot images, one kernel at a time
   8. grub-mkconfig to a temp file, then fsync and rename
   9. syncfs, verify, mark the system part applied
   10. Plasma splash, done by the GUI with its own small journal. If it fails, the GUI calls the helper's restore without a new prompt.
   11. Remove Personae folders nothing references any more
   No Cancel after the first write; a 5-minute watchdog on every command.

   *Why:* The system boots at every step in between. update-initramfs -k all stops at the first kernel that fails, and grub-mkconfig rewrites grub.cfg in place without fsync.

10. **Failure detection and fast paths**

   Treat these as failures: a non-zero exit, no 'Found theme:' line from grub-mkconfig, or any 'W: plymouth' line from the boot-image tool. Skip rebuilding boot images when the Plymouth slot and both inputs are unchanged and already verified.

   *Why:* 00_header and the initramfs hook drop a theme silently and still exit 0. GRUB-only and Plasma-splash-only applies stay fast; round 6 Q2 (all kernels) applies whenever Plymouth changes.

11. **Locks**

   The helper takes a non-blocking flock on its state folder and holds dpkg's lock-frontend and lock for the whole apply. If dpkg is busy it shows 'package manager busy' and waits; it never breaks a lock. The app holds a lock file on the library, and a second process opens the library read-only. Raising an already open window (KDBusService) comes later.

   *Why:* Neither update-initramfs nor grub-mkconfig takes a lock, and apt, Discover and kernel hooks run them too.

12. **Helper keeps running without the GUI**

   The helper ignores SIGPIPE, SIGHUP and SIGINT and finishes or rolls back on its own. Closing the window during privileged steps asks first and waits. The next start reports an unfinished apply and offers Roll back (default) or Finish.

   *Why:* Round 6 Q4. KillUserProcesses=no on this machine (verified), so no systemd-run wrapper is needed.

13. **Verification**

   Runs after every apply or restore and on 'Verify now', inside the same authorization.
   - grub.cfg must contain the expected set theme= line (with the @ subvolume prefix) and the loadfont, insmod and gfxmode lines.
   - Every initrd listing (lsinitramfs or lsinitrd, matching the detected tool) must contain the theme files, the plugin .so, the default.plymouth link and plymouthd.conf.
   Results and initrd fingerprints are stored world-readable.

   *Why:* grub.cfg and the initrds are mode 0600, and a successful exit code proves nothing.

14. **Drift**

   Checked at start, when the window gets focus and after each apply, using only world-readable files: GRUB config, plymouthd.conf, the alternative, theme folder hashes, ksplashrc (read the way KConfig does, including kdedefaults), initrd fingerprints and the kernel list. The live state is compared with every kept entry.
   - A changed initrd fingerprint alone means 'unverified'.
   - Config newer than some initrds adds the hint 'older kernels still show the previous splash'.
   - A history counter that went backwards means a rollback, e.g. by Timeshift.
   The banner names the likely cause. Adopt records the live state without root; for a mixed Plymouth state it also offers a reviewed 'rebuild all boot images'. No daemon, tray icon, autostart or apt hook.

   *Why:* Round 6 Q5 and 'change as little on the host'. On this machine the System Settings Boot Splash page sets both inputs but rebuilds only the newest kernel, and the 7.0.0-27 and -29 initrds predate the splash change of 25 Sep (verified).

15. **Disk space and size limits**

   Before changing anything, require free space on the filesystem holding /boot of at least max(1 GiB, 2 × largest initrd + new theme bytes); otherwise refuse.
   - Plymouth theme: warn at about 4 MB (shown multiplied by the kernel count), ask for confirmation above 16 MB, hard cap of 30 MB in the helper.
   - GRUB: warn above 20 MB in total or for a background above 8 MB.
   The limits are constants in the helper, not settings.

   *Why:* Each rebuild writes a ~94 MB .new file, and the theme is copied into every initrd. A setting would be one more input the helper has to trust.

16. **Recovery and logs**

   The helper's restore original|<entry> and recover verbs work with sudo from a text console. Each entry stores a plain RESTORE.txt with the exact values and commands for the detected boot-image tool, generated from the same plan as the review; Help › Recovery shows it offline. The helper logs every step to journald (identifier personae-helper) and the full output into the entry. About has 'Copy system report' with versions and the detected toolchain.

   *Why:* A broken Plasma or a Qt upgrade must not block the way back. Ground rule: log every privileged action.

17. **Host footprint and removal**

   App ID io.github.nomoruyi.personae for the .desktop file, the polkit policy and the Wayland app_id. The package has no maintainer scripts, and removing it leaves the applied looks working. An in-app 'Restore Original and remove Personae files' removes Personae's GRUB file, alternative entries and theme folders, rebuilds, and then tells the owner to purge the package. The library is never deleted automatically.

   *Why:* CLAUDE.md requires generated output to keep working after uninstall, and a clean way out fits 'change as little on the host'.

18. **Helper missing or outdated**

   At startup, check the policy file and the helper's --version protocol. If they don't match the app:
   - show a banner with the exact install command
   - disable apply and restore for Boot Menu and Boot Splash, with the reason
   - keep designing, previews, export and Plasma-splash applies working

   *Why:* An app built from source drifts away from the installed helper; failing deep inside pkexec would be worse.

19. **Failure presentation**

   - Problems found before apply disable Apply and link to the affected fields.
   - Cancelled authentication is an info message, not a failure.
   - Failed and rolled back: a warning page with the cause in plain words (naming failures in other packages' hooks as such), the log and a Copy button.
   - Rollback failed: an error page plus a persistent banner with the exact commands and where the backup is.
   Errors are never shown only as passive notifications.

   *Why:* The automatic rollback from round 6 Q4 must be visible and understandable.

20. **Dev sandbox and tests**

   Dev builds:
   - point HOME, XDG_* and TMPDIR at a gitignored folder in the repo
   - run against a fake system root whose stub tools log their calls
   - have one --sandbox DIR flag, with a permanent banner
   The helper refuses a fake root when running as root. Injected stub failures exercise every rollback path. Release builds refuse apply/restore while CLAUDECODE is set. --screenshot renders a page offscreen to a PNG.
   Generators are deterministic, tested with byte-exact text golden files, image comparisons with a tolerance, and an offscreen Splash.qml smoke test that steps through stages 1 to 6. The build-dependency list lives in the repo.

   *Why:* CLAUDE.md's hard rule and golden-file rule. Claude can't see a Wayland window. The build dependencies exist today only because plasma-workspace-dev was installed by hand.

21. **Image and video tools**

   Qt's image classes for raster work; the installed plugins read WebP, AVIF, JXL, PSD, XCF and KRA. ffmpeg and grub-mkfont run as cancelable subprocesses with absolute paths, off the UI thread. No ImageMagick.

   *Why:* One runtime dependency fewer, and no surprises from ImageMagick's resource policy.

22. **Editor layout**

   Element list, live canvas, and an inspector with number fields for every canvas action. Arrow keys nudge 1 px (Shift: 10 px). Positions are stored in each stage's own units: GRUB whole % plus px, two-step alignment fractions, Plasma splash % of screen. A resolution switch for 1440p/1080p/720p, plus a side-by-side Compare view.

   *Why:* One generic % model would make preview and boot disagree; round 5 fixed three resolutions.

23. **Save model**

   Saving is explicit (Ctrl+S), and each save makes one snapshot. All snapshots are kept; each is a few KB. Undo/redo works per open design for the session, separately from snapshots. One crash-recovery draft is kept, and it is not a snapshot. Restoring a snapshot saves a new one. Build and apply use only saved snapshots, and history records their IDs. Ctrl+Z never touches the library or the system.

   *Why:* Delivers round 4 Q3 without flooding the snapshot list, and keeps the three undo layers from colliding.

24. **Previews**

   Previews render the real build output, built incrementally into the cache, so designs and imported themes share one renderer per stage.
   - GRUB: 1-bit .pf2 text over a sample menu built from GRUB_DISTRIBUTOR and the kernels in /boot (editable).
   - Two-step: the plugin's layout formulas and its 2 s spinner timing.
   - Script themes: a static image marked 'not simulated'.
   - Plasma splash: designs load in-process with a stage scrubber; imported splashes run only through ksplashqml --test --window.

   *Why:* Round 2 Q4 accepts rough previews, but antialiased text would make GRUB fonts look better than they are. An imported splash is code.

25. **Validation**

   Recomputed at 1440p, 1080p and 720p on every change.
   - Red, blocks the build, only for broken or missing output: menu off-screen, mandatory files missing, unknown GRUB global key.
   - Yellow for everything else: overflow, clipped titles, fixed-size parts too large at 720p, contrast below 4.5:1 for text or 3:1 for large text and spinners, Plymouth size.

   *Why:* Fixed-pixel fonts and images overflow at lower resolutions; warnings must not get in the way of experimenting.

26. **GRUB generator**

   - Selection and menu colours, borders and rounded corners are rendered into 9-slice PNGs.
   - A text shadow is a duplicate component, slightly offset.
   - Decorative static text is baked into image components; there is no menu text alignment.
   - Fonts are built with grub-mkfont -n personae-<slug> covering Latin-1, Latin Extended-A, punctuation and arrows; the stored name is read back into theme.txt.
   - The terminal box is styled, with a fixed Ubuntu Mono terminal font.
   - A PNG sits at the theme root so insmod png is emitted.
   - No unknown global keys are ever written.

   *Why:* GRUB 2.14 has no background-colour or alignment property, a font-name mismatch silently swaps the font, and an unknown global key drops GRUB to the text menu.

27. **Plymouth generator**

   - Always ship the prompt images; entry.png and lock.png are mandatory.
   - Write UseEndAnimation in every mode section: false unless there is an end animation.
   - Write all seven mode sections. Update, upgrade, firmware and reset keep Ubuntu's stock text and progress bar on the design's background.
   - Live text offers size only; it is always white Ubuntu.
   - Spinners are resampled to the fixed 2.0 s loop (60 frames), and the speed change is shown.
   - Free layers are flattened into background.png (scales, cropped to 16:9) and watermark.png (fixed pixel size).
   - Ship preview.png plus Name/Description for System Settings.
   - Show a live size meter.

   *Why:* These are the plugin's rules as found in the two-step source. Discover's offline updates boot in the [updates] mode.

28. **Plasma splash generator**

   Splash.qml imports only QtQuick, plus QtMultimedia when it plays video; never Kirigami or Plasma modules. It is visible without waiting for any stage, and it bundles its fonts. The package ships contents/previews/splash.png and metadata.json, never contents/defaults or contents/layouts. The splash is checked by loading it offscreen at build time, at apply time and after Qt or Plasma upgrades; if it fails, a one-click revert that needs no root is offered.

   *Why:* A QML error shows a blank splash. Packages without defaults or layouts stay out of Global Themes.

29. **Plasma splash video**

   Decide with a short spike before writing the generator. Compare:
   - one silent H.264 MP4 (2560×1440, at most 30 fps, over a still of the first frame) in QtMultimedia's Video
   - animated WebP in AnimatedImage with caching off
   Measure peak memory, delay to the first frame and the loop seam on both monitors; use MP4 if it holds up. Encode when the video is added, in the background with progress and Cancel, cached by source and settings. The trim tool marks the roughly 4 s that are actually visible.

   *Why:* Overload's WebP peaked at 1.5 GB. libqwebp is only Recommended by dolphin, gwenview and kubuntu-desktop, while plasma-workspace hard-depends on QtMultimedia QML through the virtual keyboard (verified). The splash lasts about 3.8 s here.

30. **Asset library**

   Originals are never modified and are stored by content hash. Per-use edits (crop, trim, size, frames, quality) live on the design element. Derived files go to ~/.cache/personae, keyed by input hash. Fonts are assets too, copied in on first use together with any licence file. Every asset and import records its source, author and licence. The picker filters by type and shows where each asset is used; 'Replace everywhere' is a single undo step.

   *Why:* Round 4 Q1. System fonts disappear on a reinstall while /home survives, and exports need the licences.

31. **Import**

   Sources: installed themes, folders, archives (.zip, .tar.*) and Personae export archives, by button or drag-and-drop. Archives are unpacked into the cache with checks against path traversal, symlinks, device files and oversize content. The stage is detected from marker files, nested theme roots are found, archives with several themes get a pick list, and bundled scripts are never run.
   The report shows:
   - the theme root found and the module
   - fonts that will fall back
   - scripts that are ignored
   - a verdict: OK, Works with limits, or Won't work here (importable, Apply blocked)
   Identical content shows 'already in your library'; Re-import lists the changed files.

   *Why:* Round 1 found nested theme roots and install scripts, and today's themes arrived as downloads. Browsing a theme store stays out.

32. **Bundles**

   Slots: design, imported theme, Leave as is, Distro default, and None for the Plasma splash. Distro default is resolved at apply time:
   - GRUB: no theme
   - Plymouth: the alternative back to auto, with Theme= matching
   - Plasma splash: the user's key removed
   A single design or import applies as a one-slot bundle. A slot with problems or a missing item blocks Apply.

   *Why:* Extends round 4 Q2 with a fallback that needs no Personae files (CLAUDE.md: keep a fallback theme).

33. **Deletion**

   Deleting from the library moves the item to the desktop trash and shows an inline Undo. Items used by a bundle, history entry or snapshot can't be deleted; the app shows where they are used and offers Duplicate. Confirmation dialogs appear only for privileged actions (the review page) and for unsaved edits.

   *Why:* The owner wants undo, and dialogs on everything just get clicked through.

34. **Starters and spinners**

   One starter per stage, built from assets the app renders itself, with the accent colour defaulting to Plasma's AccentColor. A built-in spinner generator (dot ring, arc, pulse, rotating a still image), plus import from GIF, APNG, WebP, video or a folder of frames.

   *Why:* Round 2 Q3 rules out editing imported themes, so new designs need a starting point. The MIT-licensed repo can't ship Plymouth's GPL spinner frames.

35. **Colour picking**

   One picker (hex, HSV, recent colours) that hides alpha where the stage ignores it. An eyedropper on the app's own canvas and assets. A swatch row with every colour used in the bundle. Palette extraction comes later.

   *Why:* Two-step colours have no alpha and its live text is always white; picking colours anywhere on screen under Wayland needs a portal.

36. **App settings**

   One preference: the library location. Changing it offers 'move the library there' or 'use the library found there'; local filesystems only; a missing path shows 'library not found' and never creates an empty library. UI state is stored separately.
   Shown read-only, not settings: all kernels are rebuilt, 10 entries plus Original, the detected toolchain, the resolution order, the dry run.
   Boot options live on the Overview's Boot Menu card and go through review, polkit and history.

   *Why:* Anything that changes boot behaviour must be reviewed and undoable; rounds 4 to 6 fix the rest.

37. **GRUB resolution list**

   GRUB_GFXMODE=2560x1440,1920x1080,1280x720,auto

   *Why:* Keeps round 5's order and adds a last resort, so an unknown monitor or GPU still gets the scaled theme instead of the text console.

38. **Locale**

   The helper runs grub-mkconfig with LANG, LANGUAGE and LC_MESSAGES from /etc/default/locale, and previews use the same strings. File names use ISO 8601 UTC; the UI uses the session locale.

   *Why:* The menu then matches what update-grub produces when apt runs it.

39. **Generated metadata**

   Authors = nomoruyi (a setting). Licence is set per design and empty by default. Exports include a README with manual install and uninstall commands, plus the licences of any imported material.

   *Why:* Designs may contain artwork the owner doesn't own, so MIT can't be assumed for generated themes.

40. **Notifications, shortcuts, accessibility**

   - Progress shows in the window; a desktop notification on finish or failure appears only when the window is not focused.
   - Rebindable KDE-standard shortcuts: Ctrl+S, Ctrl+N, Ctrl+I, Ctrl+E, F5 to re-check, Ctrl+Return to open the review. No shortcut runs a privileged step.
   - Everything can be edited by keyboard alone, and every control has an accessible name.
   - Diffs carry +/- markers, not just colour.
   - Animated previews pause when Plasma animations are off.
   - The app follows the Plasma colour scheme, while previews show the true boot colours.

   *Why:* KDE HIG; the review diff is the main safety screen.

41. **Library format**

   Every file carries a formatVersion from day one. Older formats are migrated one version at a time, after a backup of the JSON files. Newer formats open read-only. Writes are atomic. If a file changed on disk, the app asks whether to reload or overwrite. A file that can't be parsed marks only that item as broken.

   *Why:* The plain-file library in /home outlives app versions and reinstalls.

42. **Secure Boot (later)**

   Read the SecureBoot EFI variable at start; if it is on, warn that the signed GRUB may refuse theme fonts.

   *Why:* Off today, but Windows on the ESP makes turning it on later plausible.

### Draft rounds (superseded by round 7 in INTERVIEW.md)

#### Round 7: Stack, packaging and first version

1. **C++ with Qt/Kirigami, or Python with PySide6? Will you read or edit the code yourself, and in which language?**
   - Context: C++: every build tool is already installed and QML is checked at build time. Python: faster to write, but needs 3 more apt packages, has no KDE Frameworks bindings, and QML errors only show up at runtime.
   - Default: C++20 with Qt 6.10 Quick and Kirigami 6.24, CMake and QtTest; the helper in C++ using only QtCore.
2. **Install Personae as a .deb you build locally and install with apt?**
   - Context: The root helper and polkit policy must be root-owned files at fixed paths. dpkg tracks them, so removal is clean, but you reinstall the .deb whenever the helper changes.
   - Default: Yes, a local .deb only, no public releases; applied looks keep working after removal.
3. **Should v1 include a small command line next to the GUI?**
   - Context: It shares the GUI's core, works from a text console when Plasma is broken, and lets tests drive the whole flow without a screen. Designing stays GUI-only.
   - Default: Yes: `personae status | build | apply | restore` (same dry run, asks y/N), plus the helper's `restore original` via sudo for emergencies.
4. **First usable version: import, apply, switch and undo existing themes, with no designers. Then the Plymouth and Plasma splash designers, then the GRUB designer. OK?**
   - Context: The risky part (root helper, rebuilding 5 boot images, history, rollback) needs no designer and can be proven with imported themes. Your splashes are what's broken today; your GRUB theme works.
   - Default: Yes, in that order; v1.0 = all three (the round 3 scope).

#### Round 8: Screens and flows

1. **Does the screen and flow sketch fit how you want to work? Is anything missing or in the wrong place?**
   - Context: It fixes the screen list for SPEC.md; changing the navigation later means rework.
   - Default: Build it as sketched.
2. **Call a bundle a 'Persona' in the app (one mask across all three stages), or keep 'Bundle'?**
   - Context: Only labels and docs change. 'Persona' fits the app name; 'Bundle' is plainer.
   - Default: Keep 'Bundle'.
3. **Drag-and-drop placement on the editor canvas in v1, or are click-to-select plus number fields enough?**
   - Context: Dragging with snapping is a big share of the editor work; number fields and arrow-key nudging are needed anyway for precision and keyboard use.
   - Default: Dragging with snapping, plus number fields.
4. **Should shutdown and reboot look different from boot-up in v1?**
   - Context: Two-step uses the same images in every mode except an optional end animation; different text per mode in your own font needs the script module.
   - Default: Same look. Update and upgrade screens keep Ubuntu's progress bar and 'do not turn off' text on your background.

#### Round 9: Settings, host footprint and integration

1. **Should the GRUB menu timeout be one machine-wide setting, or travel with bundles (e.g. 0 s for a gaming look, 10 s for maintenance)?**
   - Context: If bundles carry it, switching looks also changes whether you can reach the menu; machine-wide keeps boot behaviour separate from looks.
   - Default: Machine-wide, set on the Overview's Boot Menu card through its own review and history entry.
2. **With a 0 s timeout, should GRUB still show the menu after a failed boot, where your /boot allows it?**
   - Context: Today /boot is on btrfs, so GRUB can't record failed boots and 0 s means no way into the menu, as decided. If GRUB can write to /boot (possible after a reinstall), leaving one key alone keeps the menu after failed boots without slowing normal ones.
   - Default: Yes, keep it where supported.
3. **Put Personae's GRUB settings in their own file, /etc/default/grub.d/zz-personae.cfg, and never edit /etc/default/grub?**
   - Context: That file overrides /etc/default/grub and never touches the kernel command line; deleting it brings your old settings back. Your old GRUB_THEME line stays, just overridden.
   - Default: Yes.
4. **Keep system history and backups in a root-owned folder, /var/lib/personae?**
   - Context: The root helper can trust it, and Timeshift rolls it back together with /boot and /etc. But it's one more folder on the system and is lost on a reinstall. Your home folder would survive reinstalls, but then every restore is untrusted input.
   - Default: Yes: created at the first apply, removed on purge or by 'Remove Personae files'. After a reinstall, Personae offers to re-apply your last bundle.
5. **When you re-apply an edited design, may Personae install it as a new folder next to the old one?**
   - Context: Never rewriting a folder that grub.cfg and the boot images point at keeps every history entry restorable. System Settings' Boot Splash and Splash Screen pages may then list a few dated versions.
   - Default: Yes. Names show the design and date; versions that no kept entry uses are removed after each successful apply.
6. **English-only interface, or German too?**
   - Context: Text is prepared for translation either way; German adds KDE's translation framework and ongoing upkeep.
   - Default: English only.

### MVP boundary

MVP (milestone 1, Q7.4)
Replaces today's manual workflow (download, hand-install, switch and revert themes, hand-edit configs, rebuild boot images) for existing themes. No designers yet.
- Package: app, fixed-operation helper, polkit policy, .desktop file, icon (Q7.2).
- Library: plain files with format versions.
- First run: system check, current look, Original saved per stage, optional import of installed themes. A welcome-back page on a new machine-id.
- Import of installed themes, folders and archives: nested folders repaired, report with verdict, Re-import. Thumbnails come from the preview files themes ship. Plasma splashes get Run real preview.
- Overview: active look per stage, detected toolchain, outside changes with Adopt / Re-apply, check of every kernel's boot image.
- Bundles and applies of a single theme. Slots: imported theme, Leave as is, Distro default, None (Plasma splash only).
- Full apply pipeline:
  - cleaned-up install copy and validation
  - review with diffs and commands
  - pkexec, then a rebuild of each kernel's boot image
  - verification and automatic rollback
  - recovery of an unfinished apply, logging
- System history (10 entries plus Original): restores that change only Personae's settings, a RESTORE.txt per entry, and recovery commands for a text console.
- GRUB menu timeout and the fixed resolution order.
- Read-only mode when no matching helper is installed.
- Sandbox and screenshot modes, golden-file tests and rollback tests for the helper.
- The small command line, if Q7.3 is yes.

PHASE 2 = v1.0: the round 3 designer scope, in two steps
2a:
- Asset library: images, videos, spinners, fonts, with source and licence.
- Editor layout: canvas, inspector, save, snapshots, undo, crash draft.
- Validation at 1440p, 1080p and 720p.
- Plymouth two-step designer and preview: all seven modes, size meter.
- Plasma splash designer: full-screen looping video (format chosen by the spike), spinner, fade, in-app preview.
- Spinner generator and spinner import.
- Starters that use the Plasma accent colour.
- Previews of imported two-step themes.
- Personae export/import archives (R4 Q5).
2b:
- GRUB designer per R3 Q2: background, images, one font at several sizes, menu box, 9-slice selection, countdown, keymap label, progress bar, per-entry icons, terminal box.
- GRUB preview with real .pf2 text and a sample menu.
- Previews of imported GRUB themes, with badges for ignored or rejected properties.

PHASE 3 (after v1.0, roughly in order)
- A keep-or-revert prompt after the first reboot into a new look, and a 'Play boot sequence' preview of a bundle.
- 'Use desktop wallpaper': image or video per screen and activity; a video wallpaper becomes the splash loop.
- Firmware-logo (BGRT) option for Plymouth, with the Plasma splash placing the logo in the same spot.
- Text per Plymouth mode, separate startup and shutdown end animations, designs using the script module.
- Library extras: export/import of the whole library, cleanup of unused assets and old snapshots, licence warnings on export.
- Desktop extras: raising an already open window, crash reporting, translations if wanted, man page and shell completion, CI in an ubuntu:26.04 container. The command line lands here if Q7.3 is no.
- Maybe: SDDM as a fourth slot (R1 Q2), pixel-exact GRUB preview in QEMU (R3 Q1), a real Plymouth preview if plymouth-x11 gets installed, palette extraction.

NON-GOALS
- Editing GRUB entries, the default entry, SAVEDEFAULT, kernel parameters or the boot tune. That is Grub Customizer territory, and the kernel command line is off limits.
- Store browsing or upload, update checks, telemetry, any network use. Download in the browser, then Import.
- Scheduled rotation or following wallpaper changes, since each switch costs a root prompt and a rebuild.
- Lock and logout screens.
- A daemon, tray icon, autostart entry or apt hook.
- Git or sync inside the app.

### UX flow sketch

WINDOW
Kirigami, desktop only (minimum about 1100×700).
- Sidebar: Overview · Boot Menu · Boot Splash · Plasma Splash · Bundles · Assets · History. Footer: Settings · About.
- Editors open full width; details open as a second column.
- First run, apply and restore take over the whole window.

SCREENS
- Overview (launch view): three stage cards in boot order. Each card shows:
  - thumbnail and name
  - origin: design, imported theme, or not in library
  - state: In sync · Takes effect at next boot / login · Changed outside Personae · Unverified · Inconsistent
  - detected toolchain ('initramfs-tools, 5 kernels')
  - Change…
  The Boot Menu card also has Menu options…: the timeout, plus its effective result in one sentence.
  Header: 'Matches bundle X', Apply bundle…. Overflow menu: Restore Original…, Verify now.
  Banners: helper missing or outdated (read-only mode), outside changes, unfinished apply, failed rollback.
- Boot Menu / Boot Splash / Plasma Splash: one grid of designs and imported themes.
  - Filter: All / Designs / Imported.
  - Badges: Design, Imported, Active, Edited since applied, Won't work here.
  - Toolbar: New design, Import….
  - Card menu: Apply…, Duplicate, Export…, Rename, Delete (to the trash, with Undo).
- Imported theme (second column): preview, import report and verdict, source path, Re-import, Apply…, Run real preview (Plasma splash).
- Design editor (phase 2):
  - element list | canvas | inspector
  - 1440p / 1080p / 720p switch, Compare, Play
  - warnings panel
  - Save (makes a snapshot), Versions…, Apply…; the Plasma splash adds Run real splash
- Bundles: cards with three thumbnails in boot order. The editor has three slot pickers: design, imported theme, Leave as is, Distro default, None (Plasma splash only).
- Assets (phase 2): a grid by type (image, video, spinner, font) showing where each is used. Add or drop files, encoding progress, Replace everywhere.
- History:
  - Original pinned on top, showing per stage 'saved <date>' or 'not changed yet'.
  - Entries newest first: title, three stage icons, a Current chip; rolled-back attempts greyed.
  - Entry detail: values per stage, backed-up files, log, duration, Restore….
- Settings: library location, shortcuts.
- About: versions, toolchain, paths, Copy system report, Help › Recovery.
- Empty views: one sentence and one action, e.g. 'No boot splashes yet' with New design / Import….

FLOWS
1. First run, on a full-window welcome page:
   a. System check: GRUB, boot-image tool and kernels, Plymouth plugins, Plasma, ffmpeg, helper. A missing item disables only what needs it and shows the install command.
   b. Your current look: three cards, with 'keep a copy in your library' ticked. Original is saved per stage right before Personae first changes it.
   c. Other installed themes: unticked; nested ones marked 'repaired on import'.
   d. Then the Overview.
   A new machine-id with an existing library shows the same page as 'Welcome back', plus 'Apply <last bundle> again'.
2. Design (phase 2): New design (name, stage, start from a starter, a duplicate, or another design's assets) → edit with live preview and warnings → Ctrl+S → Apply…, or use it in a bundle.
3. Import: Import…, or drop a folder or archive → report and verdict → imported theme (exact copy). Later, Re-import shows the changed files.
4. Bundle: New bundle → fill the three slots → Apply…. Blocked while a slot has problems or is missing.
5. Apply. The same pipeline serves restore, re-apply, the rebuild offered by Adopt, and Menu options.
   a. Preparing: build and validate, no root.
   b. Review, always shown:
      - per stage: before/after, and 'next boot' or 'next login'
      - warnings: the 0 s gate, font fallback, outside changes that will be replaced, the entry that will be pruned
      - files with diffs, commands, time estimate
   c. polkit prompt; skipped when only the Plasma splash changes.
   d. Progress: Back up · Install · Boot images 3/5 (7.0.0-38) · GRUB config · Verify · Plasma splash. Live log; no Cancel after the first write.
   e. Result, one of:
      - Done, with Restart… (Plasma's own reboot prompt)
      - 'Couldn't apply X; your previous setup is back', with the cause and the log
      - 'Rollback failed', with the exact commands and a persistent banner
6. Restore: History → entry → Restore… → review with a checkbox per stage, all ticked → same pipeline → a new 'Restored …' entry. Restore Original from the Overview's overflow menu or the pinned row.
7. Outside changes, checked at start and when the window gets focus:
   - The stage card says 'Changed outside Personae'.
   - A banner says what changed, the likely cause and the effect, e.g. 'Boot Splash is now Breeze, probably via System Settings; only the newest kernel was rebuilt'.
   - Actions:
     - Adopt: records the live state, no root. It offers to import an unknown theme, and for a mixed splash state a reviewed 'rebuild all boot images'.
     - Re-apply <last>
     - Details: expected vs actual
     - Dismiss until it changes again
8. Unfinished apply (crash, power loss): at the next start, 'The apply on <date> didn't finish; your system boots either way' → Roll back (default) or Finish, through the same pipeline.

### Merge notes

1. Checked read-only while merging the lens reviews:
(a) System Settings' Boot Splash page uses the helper /usr/lib/kf6/kauth/libexec/plymouthhelper. Its strings reference:
- /etc/plymouth/plymouthd.conf [Daemon] Theme
- update-alternatives --set
- update-initramfs -u and mkinitcpio -P
There is nothing for dracut. So the page sets both Plymouth inputs but rebuilds only the newest kernel.
The robustness lens claims it changes only the alternative, leaving the two inputs in disagreement. That is wrong. On this machine both inputs say fallout, changed together on 2026-09-25 at 06:09. The 7.0.0-27 and -29 initrds date from 2026-09-22, so they likely still hold the older splash.
(b) /etc/grub.d/00_header confirms the settings lens. On EFI with /boot on btrfs, GRUB_RECORDFAIL_TIMEOUT and timeout_style=menu override GRUB_TIMEOUT and GRUB_TIMEOUT_STYLE, so the menu shows for 20 s today.
(c) libqwebp (in qt6-image-formats-plugins) is only Recommended by dolphin, gwenview and kubuntu-desktop. plasma-workspace hard-depends on qml6-module-qtquick-virtualkeyboard, which hard-depends on qml6-module-qtmultimedia.
(d) logind's KillUserProcesses is no, so the tech lens's systemd-run --scope re-exec isn't needed.

2. Installed folders. The UX lens keeps one folder per design (personae-<slug>) and rebuilds old snapshots into it on restore. The robustness lens wants immutable folders named personae-<slug>-<hash8>.
- R6's personae-<slug> was Claude's suggestion, not an owner decision.
- R6 Q3 ('installed themes stay while any entry references them') favours versions.
Merged: versioned folders, asked as Q9.5. The alternative is an atomic rename swap plus a root-side archive of old builds.

3. Plasma splash video. The editors lens wants one MP4 played through QtMultimedia and scaled by the GPU. The robustness, tech and UX lenses assume animated WebP in AnimatedImage with one file per resolution, following Claude's R4 finding.
The facts in 1(c) favour MP4. Merged: run a spike first. Don't put R4's 'pre-rendered video size per screen' into SPEC as a decision.

4. R2 Q1's Claude note 'write both keys' is incomplete. Personae must also manage GRUB_TIMEOUT_STYLE: where GRUB can write to /boot (possible after a reinstall), the existing hidden style would hide the menu for any N. Whether 0 s keeps the menu after a failed boot there is asked as Q9.2 rather than assumed, because R2 says 0 means no way into the menu.

5. R6 Q3 and restores. The product lens warns that restoring the whole of /etc/default/grub could undo later edits to the kernel command line. This is solved without changing R6 Q3:
- Personae's keys live in their own file (Q9.3).
- Whole-file backups only roll back a failed apply.
- Restores write only Personae's keys.

6. When Original is captured. The UX lens captures each stage at Personae's first change to that stage, a refinement of R6's 'state before the first apply'. The robustness lens captures everything at once. Merged: per stage. SPEC should say so.

7. Where history lives. The UX and settings lenses want the old installation's history visible after a reinstall, which means storing it in /home. The robustness, tech and product lenses put it in root-owned /var/lib/personae, which is lost with /. Asked as Q9.4; 'apply the last bundle again' works either way.

8. polkit. The tech lens proposes two actions, apply and restore, split via exec.argv1. The robustness lens wants one: the kept authorization is per action, and a failed Plasma-splash step must roll back without a second prompt. Merged: one action, under R6's name.
Helper path: /usr/libexec/personae (settings and robustness lenses) or ECM's multiarch libexec (tech lens). Either works, as long as it's one compiled-in constant.

9. Command line. The tech lens wants an operational CLI in v1, the product lens in phase 2, the settings lens only --sandbox and --version. All agree on console recovery through the helper. Asked as Q7.3.

10. Smaller conflicts, and how they were merged:
- Deletion: desktop trash, with items in use blocked. The UX lens wanted an in-app trash purged after 30 days, and allowed deleting used items, which would leave 'Missing' slots.
- Snapshots: keep all. The robustness lens wanted the last 50 plus referenced ones.
- Closing during an apply: ask and wait. The settings lens refused the close; the product lens let the helper finish in the background.
- KDBusService: deferred as the tech lens proposed, though three lenses wanted it.
- Helper input: JSON with inline base64 files (tech lens), not a tar stream (robustness lens), because QtCore has no tar reader.
- dpkg lock: taken and held (robustness lens), not just refused while busy (product lens).
- Thresholds: size warning at about 4 MB, confirmation above 16 MB, hard cap 30 MB. Contrast 4.5:1 for text and 3:1 for large text; the editors lens used 3:1 for live text.
- Resolution order: read-only (UX lens), not editable under Advanced (settings lens), with 'auto' appended, which extends R5.
- Previews: render the build output (editors lens), not the design model (tech lens).
- AppStream metainfo: now (tech and product lenses) or later (settings lens); trivial either way.

11. Limits the owner should see in SPEC:
- Two-step live text is always white and in the Ubuntu font. R3 Q4 only named the font; any other colour needs the script module.
- The GRUB terminal (the e/c editor) gets a fixed Ubuntu Mono font, the one exception to R3 Q2's 'one font'.
- Spinners always loop in exactly 2 s.
- GRUB menu entries can't be centred.

12. Wording in INTERVIEW.md to fix in SPEC:
- R1 Q6's 'changes happen on clones inside the app' was replaced by R2 Q3.
- CLAUDE.md calls the reinstall planned, while R1 Q4 says undecided. Nothing built depends on it beyond detecting the boot-image tool (R2 Q2).
- R4 Q2's slots gain 'Distro default' (and None for the Plasma splash). This extends the decision rather than changing it.
- Imported themes stay exactly as imported in the library; only the installed copy is cleaned up (scripts dropped, paths rewritten). That keeps R2 Q3's read-only rule.

## 3. Lens reviews

### Product scope and feature completeness

INTERVIEW.md already settles the core loop: import, profiles and bundles, apply, history and undo. A full app still needs eight more things:
- slot states that are not theme packages ("Distro default", "None");
- a way to bring in downloaded themes (archives, plus re-importing Personae's own exports);
- something to start a design from (starters, spinner art the MIT repo may ship);
- the Plymouth screens nobody designs but everyone sees: Discover's offline updates are on here, so update reboots show the [updates] mode;
- a check of what is actually inside each root-only initrd and grub.cfg;
- undo and redo while editing;
- readability and fit checks at all three resolutions;
- a clean split between the portable library in /home and machine state that should die with / on the reinstall.

One decision needs fixing. As worded, round 6 Q3 backs up whole config files. /etc/default/grub also holds both GRUB_CMDLINE lines (/etc/default/grub.d is empty), so a whole-file restore could roll back a later kernel command line edit. Restores must be key-scoped (gap 2).

PROPOSED MVP (M1). It replaces today's manual workflow: a hand-installed GitHub GRUB theme, "Get New" splashes, and hand edits plus rebuilds. Scope:
- an installable .deb with the app, the fixed-operation helper, the polkit policy, the .desktop file, an icon and AppStream metadata;
- the library;
- import of installed themes, folders and archives (finds the real theme root in nested downloads, never runs bundled scripts);
- a status page: active look per target, detected toolchain, drift, and per-kernel initrd and grub.cfg checks;
- apply and switch per target and per bundle, including "Distro default" and "None" slots;
- dry run, validation and logs;
- system history (last 10 plus Original) with key-scoped restores and a plain-text restore recipe per entry;
- GRUB timeout and resolution settings;
- real KSplash preview via ksplashqml --test --window;
- a desktop notification when an apply ends;
- English UI with i18n-wrapped strings.

v1.0 = M1 + M2 + M3. This keeps the owner's round 3 v1 scope and only puts it in order.
- M2: asset library (fonts and provenance included), spinner generator, starters seeded with the Plasma accent color, Plymouth two-step designer that writes all seven mode sections, KSplash designer with full-screen video, background and spinner shared between the two, undo/redo with explicit save and snapshots, readability and fit checks at 1440p/1080p/720p, Personae archive export and import, golden-file tests.
- M3: GRUB designer per round 3 Q2, including the .pf2 font build.

PHASE 2:
- "Use desktop wallpaper" (image or video wallpaper per screen and activity; a video wallpaper becomes the KSplash loop);
- firmware logo (BGRT) option;
- per-mode Plymouth text and separate startup/shutdown end animations;
- small CLI (status, history, restore, apply bundle);
- export license warnings;
- "Remove all Personae changes" before uninstall;
- library extras: thumbnails, in-use badges, cleanup of unused assets.

PHASE 3:
- SDDM as a fourth bundle slot (SDDM 0.21 with the kubuntu theme is the display manager here);
- pixel-exact GRUB preview from a rescue ISO in QEMU;
- real Plymouth preview if the owner installs plymouth-x11;
- Plymouth designs on the script module (e.g. different images per mode);
- own palette extraction;
- boot storyboard preview (firmware, GRUB, Plymouth, SDDM, KSplash in sequence);
- per-machine bundle mapping for a shared library.

NON-GOALS: see the last gap. In short: GRUB entry editing and SAVEDEFAULT, GRUB_INIT_TUNE, store browsing, telemetry and any network use, rotation and following the wallpaper automatically, lock and logout screens, any daemon or tray icon, in-app git.

Everything was checked read-only; no files were changed. Facts checked:
- /usr/share/plymouth/themes/{bgrt,spinner}/*.plymouth: seven mode sections.
- two-step.so: understands startup-animation- and shutdown-animation- frame prefixes, UseFirmwareBackground and bgrt-fallback.png.
- discoverrc: UseOfflineUpdates=true.
- /sys/firmware/acpi/bgrt: a 409x307 logo, world-readable.
- default.plymouth: the alternative's best entry is kubuntu-logo (priority 150), manually overridden to fallout.
- The Plymouth KCM helper runs update-alternatives --set and update-initramfs -u.
- Plymouth's stock theme files are GPL-2+; Overload is GPLv3.
- The primary screen shows a video wallpaper; the Plasma accent color is taken from the wallpaper.

#### MVP cut line and milestone order

- Priority: mvp-blocker; owner decision needed: yes
- Why: Round 3 sets the v1 designer scope, but nothing says what ships first, and SPEC.md needs that line. The riskiest part (the privileged apply, initramfs rebuild for 5 kernels, history, undo) does not depend on the designers. It can be proven with imported themes before any generator exists, and that slice alone already replaces the owner's manual workflow.
- Default: M1 (first usable build): .deb (app, helper, polkit policy, .desktop, icon, metainfo), library, import (installed, folder, archive), status page with per-kernel checks, apply and switch per target and bundle, Distro default and None slots, dry run, history (10 plus Original) with restore recipes, GRUB timeout and resolution, real KSplash preview. M2: asset library, spinner generator, starters, Plymouth two-step designer and KSplash designer, built together because they share background and spinner (round 3 Q6). M3: GRUB designer, the most constrained (.pf2 fonts, fixed pixel sizes at three resolutions, per-entry icons). v1.0 = M1+M2+M3.
- Question: First usable build = M1 (import, apply, undo; no designers), then Plymouth+KSplash designers, then the GRUB designer: OK?
- Notes: Package as a .deb: the helper and the polkit policy must live in system paths, and dpkg tracks and removes them cleanly. Snap and Flatpak cannot ship a host polkit helper, and the reinstall will be snap-free.

#### Machine state vs. portable library, and restore granularity

- Priority: mvp-blocker; owner decision needed: no
- Why: /home (the library) survives the planned reinstall; / does not. History entries, Original and backups describe this installation only (Original = hollow-grub + fallout + Overload + the current GRUB keys), and restoring them onto a fresh system would bring back a dead state. As worded, round 6 Q3 backs up whole config files. /etc/default/grub also holds both GRUB_CMDLINE lines (/etc/default/grub.d is empty), so a whole-file restore can silently revert a later command line edit and break the never-touch-the-cmdline rule.
- Default: Three stores. (1) Library: designs, bundles, assets in ~/.local/share/personae; source files only, portable, git-friendly. (2) Per-user derived data: snapshots in ~/.local/state/personae, builds and renders in ~/.cache/personae. (3) Machine state: history, Original, logs, last-apply baseline in a helper-owned /var/lib/personae. It is wiped with / on reinstall, and the helper never takes file contents from storage the user can write. Restores are key-scoped: only app-owned values are recorded and written (GRUB_THEME, GRUB_GFXMODE, GRUB_TIMEOUT, GRUB_RECORDFAIL_TIMEOUT, plymouthd.conf Theme=, the default.plymouth alternative, ksplashrc Engine/Theme), never whole files.
- Notes: /var/lib/personae is one new host directory. If the owner wants none, machine state can live in ~/.local/state stamped with /etc/machine-id, but then the helper must treat every restored value as untrusted input and validate it.

#### Plymouth mode coverage: updates, upgrades, reset, shutdown, reboot

- Priority: mvp; owner decision needed: no
- Why: Plymouth 24 two-step themes carry seven mode sections: boot-up, shutdown, reboot, updates, system-upgrade, firmware-upgrade, system-reset (see the installed bgrt and spinner .plymouth files). Discover has UseOfflineUpdates=true here, so every update reboot runs under [updates]. A generated theme without these sections shows the normal boot look with no progress bar and no 'Do not turn off your computer' text. Rounds 3 to 5 only describe the boot look.
- Default: The generator always writes all seven sections. boot-up, shutdown and reboot use the design. The four maintenance modes copy Ubuntu's stock keys (SuppressMessages, UseProgressBar, ProgressBarShowPercentComplete, Title/SubTitle as live Ubuntu-font text), drawn on the design's background and colors. The preview gets a boot/updates/shutdown switch. Golden files cover all sections. Phase 2: per-mode text editing and separate end animations (two-step 24 looks for startup-animation- and shutdown-animation- frames before animation- and throbber-). Different background images per mode need the script module: later.
- Notes: Shutdown and reboot read the theme from /, so they show a new look before the initramfs rebuild finishes. The apply should refuse while the dpkg lock is held, because package installs run initramfs rebuilds of their own.

#### Import sources: downloaded archives, folders, Personae archives

- Priority: mvp; owner decision needed: no
- Why: Round 1 covers installed themes and round 4 covers re-import from a remembered path. But the current themes arrived as GitHub or KDE Store downloads (hollow-grub, crossgrub, Overload, Penrose), and broken 'Get New' installs started this project. Without archive import the owner still has to install by hand first. Round 4 Q5 defines an export archive but no import for it.
- Default: Import takes folders and .zip/.tar.gz/.tar.xz/.tar.zst via the file dialog or drag-and-drop. Archives are extracted into ~/.cache/personae with guards: no absolute paths, no '..', no symlinks or device files, caps on size and file count. The target is detected by markers: theme.txt for GRUB; *.plymouth with ModuleName for Plymouth; metadata.json with Plasma/LookAndFeel plus contents/splash/Splash.qml for KSplash. The real theme root is found in nested layouts. Archives with several themes or resolution variants (common for GitHub GRUB themes) show a pick list. Install scripts are listed but never run. Re-import re-extracts the remembered archive. Personae's own export archives (zip with a format version) import as designs or bundles; identical assets are stored once (matched by content hash) and clashing names get a suffix.
- Notes: Browsing the KDE Store or KNewStuff inside the app stays a non-goal: download in the browser, then import.

#### Bundle slot values for 'Distro default' and 'None'

- Priority: mvp; owner decision needed: no
- Why: Round 4 Q2 offers three slot values: designed profile, imported theme, leave as is. Some valid states are not theme packages: GRUB with no theme; Plymouth's distro default (the alternative's best entry is kubuntu-logo, priority 150, currently overridden manually to fallout); KSplash disabled or following the Global Theme (both Global Themes here set [KSplash] Theme=org.kde.Breeze). These are the always-available fallback CLAUDE.md asks for and need no files installed by Personae. 'Original' is not stock on this machine.
- Default: Every slot offers 'Distro default', worked out at apply time and never hardcoded, so it stays right after the reinstall. GRUB: GRUB_THEME empty. Plymouth: the alternative back to auto mode, with plymouthd.conf Theme= set to match. KSplash: the Theme key removed, so the active Global Theme's splash shows. KSplash also offers 'None' (Engine=none). Plymouth gets no 'None', because that would mean removing 'splash' from the command line; the stock text and details themes can be imported instead.

#### Starter designs and built-in spinner art

- Priority: mvp; owner decision needed: no
- Why: Editing imported themes is not an app feature (round 2 Q3), so without starters every new design begins empty. Spinners are a library asset (round 4 Q1), but nothing says where spinner frames come from, and the MIT repo cannot ship third-party art: Plymouth's stock throbber frames are GPL-2+ (/usr/share/doc/plymouth/copyright) and Overload is GPLv3.
- Default: One starter per target, using only assets the app renders itself. GRUB: centered menu on a background. Plymouth: background + spinner (two-step). KSplash: background + spinner, and full-screen video. Creating a starter asks for a background (image, gradient or solid) and an accent color. The color defaults to Plasma's AccentColor from kdeglobals (here 146,85,183, derived from the wallpaper). A built-in spinner generator (styles: dot ring, arc, pulse; options: color, diameter, frame count, fps) renders the two-step throbber-NNNN.png frames at build time, plus the matching KSplash animation. Spinners can also be imported from GIF, APNG, animated WebP, video or a folder of frames. The starters double as the golden-file test inputs.

#### Verify what is actually inside each initrd and grub.cfg

- Priority: mvp; owner decision needed: no
- Why: What actually boots lives in five root-only (0600) initrds and a 0600 grub.cfg, not in the config files that drift detection reads. The installed Plymouth KCM (kde-config-plymouth) is a second writer: its helper runs update-alternatives --set and then update-initramfs -u, which rebuilds only the newest kernel, so after a change outside Personae the kernels can disagree. dracut and initramfs-tools also pack themes differently.
- Default: Read-only helper operations, run at the end of every apply (inside the same polkit authorization) and on demand. For each kernel: list the initrd with lsinitramfs or lsinitrd (whichever matches the detected generator; both are installed) and check for the theme directory, plymouthd.conf Theme=, the module .so (two-step or script) and label-pango. For GRUB: check grub.cfg for the expected 'set theme=', the loadfont lines, insmod png/jpeg and the gfxmode. Show results per kernel on the status page and in the history entry. A mismatch counts as drift and offers a 'Rebuild all initrds' fix.

#### Undo/redo and save behavior in the designer

- Priority: mvp; owner decision needed: no
- Why: The owner strongly wants undo. Round 4 Q3 gives a snapshot on every save, but does not settle undo/redo while editing, explicit save vs. autosave (autosave plus a snapshot per save would flood the history), crash recovery, or how snapshots keep their assets alive.
- Default: Explicit save (Ctrl+S) creates one snapshot. Each open design has unlimited undo/redo for the session (Ctrl+Z / Ctrl+Shift+Z, asset swaps included), cleared when the design is closed. A crash-recovery draft is written every 30 s to a separate file (not a snapshot) and offered at the next start. Restoring a snapshot counts as a save, so it can be undone. An asset is deleted only when no design, bundle or snapshot references it. Deleting from the library moves items to the desktop trash, and assets still in use cannot be deleted.

#### Readability and fit checks for generated themes

- Priority: mvp; owner decision needed: no
- Why: The owner's wallpapers are busy cyber-city and anime images. GRUB .pf2 fonts have a fixed pixel size chosen for 1440p, so at the 720p fallback text takes twice as much of the screen. two-step's logo and spinner are fixed-size too. GRUB_DISABLE_OS_PROBER=false can add menu entries. Plymouth assets are copied into all five ~94 MB initrds. Nothing decided so far catches an unreadable or overflowing design before a reboot shows it.
- Default: Warnings, never blockers, shown in the editor and in the dry run, checked at 2560x1440, 1920x1080 and 1280x720: text contrast at least 4.5:1 and progress bar or spinner at least 3:1 against the background area behind them; elements off-screen or overlapping; GRUB menu box tall enough for the real number of entries (counted from grub.cfg during verification) at 720p; Plymouth theme over ~5 MB (the warning shows the total across installed kernels); oversized GRUB background file. Where possible a warning offers a one-click fix: dim or blur that part of the background, add a menu panel (GRUB menu_pixmap_style 9-slice), or pick a readable text color automatically.

#### Fonts as library assets; provenance and metadata in generated packages

- Priority: mvp; owner decision needed: no
- Why: Round 4 Q1 lists images, videos and spinners as shared assets, but not fonts. System fonts in /usr/share/fonts disappear with the reinstall while /home survives, so a design that points at a system font breaks. Imported themes carry licenses (Overload: GPLv3) that exports (round 4 Q5) would pass on. The KSplash generator needs Authors, License and Version, and System Settings shows contents/previews/splash.png (Overload ships one).
- Default: Using a font copies its file into the asset library, plus any license file found next to it. .pf2 files are build outputs, made from that copy with grub-mkfont at the needed pixel sizes. Every asset and import records its source path or URL, author and license, prefilled from metadata.json or LICENSE files. Settings hold the author name and default license (MIT, nomoruyi) for generated metadata.json. Generated KSplash packages include contents/previews/splash.png. GRUB and Plymouth exports include a README with the manual install and uninstall commands, plus the original licenses. Phase 2: export warns about assets whose license is unknown or does not allow redistribution.

#### Recovery that does not need the app

- Priority: mvp; owner decision needed: no
- Why: The owner is fluent on the command line and wants undo, but the GUI can be unavailable: a broken Plasma session, working over SSH, the app uninstalled, or a library format change. Generated themes keep working after uninstall, yet the history can only be used through the app.
- Default: Every history entry, including Original, stores a world-readable RESTORE.txt next to its log. It lists the exact key values and the commands in order: update-alternatives --set default.plymouth <path>; the Theme= edit; update-initramfs -u -k all or dracut --regenerate-all --force; update-grub; the ksplashrc edit. It is generated from the same plan as the dry run, so it costs almost no extra code. Phase 2: a small CLI (personae status | history | restore <id|original>) that uses the same helper. pkttyagent is installed, so pkexec works on a text console.

#### Background behavior, notifications, and closing the app during an apply

- Priority: mvp; owner decision needed: no
- Why: An apply takes 40 s or more (5 initrds plus grub-mkconfig). Nothing says whether Personae runs in the background (tray icon, autostart, drift watcher), how it reports that an apply finished, or what happens if the window closes or the session ends mid-rebuild. That could leave some initrds stale or a bundle half-applied, despite the all-or-nothing rule.
- Default: No daemon, tray icon or autostart, to keep host changes minimal; drift checks and verification run when the app opens. Progress shows in the window, plus a desktop notification on success or failure when the window is unfocused. The helper ignores SIGHUP and SIGPIPE, logs to the machine-state directory, and finishes or rolls back on its own. Quitting during an apply asks for confirmation and says the apply will finish in the background; the next start reads the outcome from the history.

#### Match the desktop: wallpaper, video wallpaper, accent color

- Priority: later; owner decision needed: no
- Why: Plasma already takes AccentColor from the wallpaper here (accentColorFromWallpaper=true), so most of the palette work is already done. The primary screen of the main activity runs a video wallpaper (luisbocanegra smart video wallpaper reborn, hacker-room.1920x1080.mp4), and the other activities use 3840x2160 images. So 'current wallpaper' means one per screen and per activity, and may be a video.
- Default: Phase 2: a 'Use desktop wallpaper' picker lists the image and video wallpapers per screen and activity from plasma-org.kde.plasma.desktop-appletsrc and copies the chosen one into the asset library. A video wallpaper becomes the KSplash loop, so the splash flows into the same video on the desktop. The accent-color default ships in M2 with the starters. Phase 3: own palette extraction from images. Non-goal: following wallpaper changes automatically, since every switch means a root prompt and a 40 s rebuild.

#### Firmware logo (BGRT) as Plymouth background

- Priority: later; owner decision needed: no
- Why: The firmware provides a 409x307 logo in /sys/firmware/acpi/bgrt/image, readable without root. It is drawn at (1075,397): centered horizontally, at 38% of the height, on a 2560x1440 screen. two-step supports UseFirmwareBackground per mode, DialogClearsFirmwareBackground and bgrt-fallback.png. With a GRUB timeout of 0 the firmware logo stays on screen until Plymouth draws the same logo, giving a flicker-free start. With the current 20 s menu the logo reappears after GRUB.
- Default: Phase 2: a 'Firmware logo' checkbox in the Plymouth designer (per boot-up, shutdown and reboot), with a bgrt-fallback.png. The preview reads the image straight from /sys, no root needed. A KSplash starter option shows the same logo at the same relative position for continuity. Validation warns when the machine has no BGRT. Not in the MVP because the stock bgrt theme can already be imported.

#### Explicit non-goals for SPEC.md

- Priority: non-goal; owner decision needed: no
- Why: These are features that neighboring tools have or that a full feature list would suggest. Writing them down keeps v1 small and the host untouched.
- Default: Non-goals, with reasons:
- GRUB menu entry ordering, renaming, hiding, default entry, kernel parameters: Grub Customizer territory, and the command line is untouchable.
- GRUB_SAVEDEFAULT: GRUB cannot write grubenv on a btrfs /boot; 00_header says so itself.
- GRUB_INIT_TUNE boot sound: needs a PC speaker, delays boot, adds another host key.
- KDE Store or KNewStuff browsing and upload, update checks, telemetry, any network access: personal offline tool, and archive import replaces it.
- Scheduled or seasonal rotation and following the wallpaper automatically: extends round 1, since each switch costs a root prompt and a rebuild.
- Lock screen and logout screen: not boot stages.
- Background daemon or tray icon.
- In-app git or sync: plain files already allow it.

### UX flows and information architecture

The interview settles the objects and the safety rules, but not the screens. Three things block the SPEC: the window frame, one shared apply/restore pipeline, and the first-run page together with the moment Original is captured.

- Window frame: a Kirigami GlobalDrawer sidebar, Overview as the launch view, editors pushed as pages, details as a second column, apply and restore in pageStack.layers.
- Apply/restore pipeline: the review page is always shown, progress moves per kernel, and Plasma's own reboot prompt follows.
- First run: Original is captured per stage, right before Personae first changes that stage.

Next come, all with sensible defaults:
- how rollbacks, failed rollbacks and interrupted applies are presented
- history and restore semantics
- how drift is presented
- a read-only mode while the helper is missing or outdated
- separating app settings from system options such as the GRUB timeout
- a reinstall welcome-back flow keyed on machine-id
- the editor's save model
- the import report and thumbnails
- bundle references
- undo for deletions
- background encoding, empty states and a simulation mode

Checked read-only on this machine:
- Kirigami 6.24 and Kirigami Addons 1.11 are installed (FormCard, ConfigurationView, StatefulWindow, SegmentedButton).
- kde-config-plymouth (System Settings › Boot Splash Screen) is installed and is a source of drift. It writes plymouthd.conf, sets the default.plymouth alternative and runs only `update-initramfs -u`. It has no dracut path, and it reads `<theme>/preview.png`.
- KSplash packages ship contents/previews/splash.png.
- The active hollow-grub, fallout and Overload themes belong to no package.
- Timeshift takes hourly btrfs snapshots of / (which includes /boot and /etc) and can silently roll the boot look back.
- org.kde.LogoutPrompt.promptReboot exists on the session bus.
- /etc/machine-id and every config file the drift check reads are world-readable; grub.cfg is 0600.

No existing decision looks wrong. One leftover: round 1 Q6 says 'changes happen on clones inside the app', which round 2 Q3 replaced. Keep that wording out of SPEC.md.

#### Main window structure and navigation

- Priority: mvp-blocker; owner decision needed: no
- Why: INTERVIEW.md fixes the objects (designs, imports, bundles, assets, system history, Original) but not where they live, what the app opens on, or how editors, details and the apply flow fit Kirigami's page model. Every flow below and the SPEC's screen list depend on this.
- Default: Window: Kirigami Addons StatefulWindow, which brings About, a shortcut editor and the Ctrl+Alt+I command bar for free.

Sidebar: a non-modal, collapsible Kirigami.GlobalDrawer. The HIG calls for a GlobalDrawer above five destinations and puts the launch view first.
- Items: Overview (launch view), Boot Menu, Boot Splash, Plasma Splash, Bundles, Assets, History.
- Footer: Settings, About.
- Choosing a section resets pageStack to that section's root page.

Stage pages: one thumbnail grid per stage holding designs and imported themes together.
- Kirigami.Chip badges: Design, Imported, Active, Edited since applied.
- Filter: All / Designs / Imported.
- Toolbar: New Design, Import….
- Per-card ConvergentContextMenu: Apply…, Duplicate, Export…, Rename, Delete.

Opening items:
- A design opens its editor as a pushed full-width page (SingleColumn).
- An imported theme or a history entry opens as a second column (master-detail) on wide windows.
- First run, apply and restore run in pageStack.layers, so the sidebar can't be reached mid-flow.

Overview: three stage cards in boot order. Each shows:
- what is active now: thumbnail, name, origin (Personae design / imported / not in library)
- a state chip: In sync, Edited since applied, Changed outside Personae, or Shows at next boot / next login
- the detected toolchain (round 2 Q2)
- Change…, which opens that stage's page
The page header has Apply bundle…; the overflow menu has Restore Original….

Building is implicit: it runs on preview, apply and export, cached by content hash, so there is no Build button.

Desktop-only layout of at least about 1100×700; no narrow or mobile mode.
- Question: The app is called Personae: should the UI call a bundle a 'Persona' (one mask across all three stages), or keep 'Bundle'?
- Notes: UI terms: Design (the interview's 'profile'), Imported theme, Bundle, Original, History entry. Stage names: Boot Menu (GRUB), Boot Splash (Plymouth), Plasma Splash; they sit next to System Settings' 'Boot Splash Screen' and 'Splash Screen' pages without clashing. Shortcuts: Ctrl+N new design, Ctrl+I import, F5 re-check the system. StatefulWindow's shortcut editor makes every action assignable, as the HIG asks.

#### Apply pipeline: entry points, review page, progress, result

- Priority: mvp-blocker; owner decision needed: no
- Why: The ground rules require a dry run and validation, and round 6 fixes pkexec, the ~40 s rebuild and all-or-nothing. Still open: the screens themselves, which actions share this flow, whether one design can be applied without making a bundle, and what the owner sees during the 40 s rebuild.
- Default: One flow covers every system change:
- Apply: a bundle, or a single design or import treated as a bundle whose other slots are Leave as is (history title 'Applied Boot Splash Fallout')
- Restore
- Re-apply after drift
- Boot menu options

1) Preparing: unprivileged build and validation in library staging. Problems stop the flow here, with links to the fields.

2) Review page, always shown. Summary first, details on demand (HIG progressive disclosure).
- Each touched stage: before→after thumbnails and 'takes effect: next boot' or 'next login'.
- Warnings as InlineMessages: timeout 0, font fallback, outside changes that will be replaced, the history entry that gets pruned and the theme folder removed with it.
- Files: create / replace / remove, with path and an expandable unified diff (org.kde.syntaxhighlighting).
- Commands in run order, with Copy.
- Time estimate: last per-kernel time × kernel count, 'about 40 s' the first time.
- Primary button 'Apply…', with a lock icon only when GRUB or Plymouth change.

3) polkit prompt. Cancelling or denying returns to the review page with 'Authentication cancelled. Nothing was changed.'

4) Progress layer, with no Back button.
- Step list: Back up; Install files; Update GRUB configuration; Rebuild boot images (3 of 5, with kernel name); Apply Plasma splash; Save history entry. Plasma splash comes last so a failed privileged step never leaves it half-applied.
- A weighted progress bar, elapsed time and estimate, and a collapsible live log with Copy.
- No Cancel once privileged steps run.
- Closing the window shows a PromptDialog and waits.
- A KNotification on finish only if the window is in the background (HIG).

5) Result page: before/after per stage.
- Restart… calls org.kde.LogoutPrompt.promptReboot, Plasma's own dialog.
- Done returns to Overview, which now shows the change (HIG: show success through the UI).

A Plasma-Splash-only apply still shows the review page, but skips polkit and goes straight to the result.
- Notes: The helper should rebuild one kernel at a time, for both initramfs-tools and dracut, so progress moves per kernel instead of a 40 s spinner. Use KDBusService Unique for a single instance, plus a helper-side lock. Result-page hint: the restart's shutdown splash already uses the new Plymouth theme, because plymouthd reads plymouthd.conf from / at shutdown. Later: after the first boot following an apply, a one-time Overview message 'Keep Neon Night or restore the previous state?' (compare /proc/stat btime with the apply time).

#### First run: system check, current look, initial imports, when Original is captured

- Priority: mvp-blocker; owner decision needed: no
- Why: Round 2 wants the detected toolchains shown, and round 6 pins an Original entry. Nothing says what the first launch shows, when exactly Original is captured, or which installed themes get imported. None of the three active themes here (hollow-grub, fallout, Overload) belongs to a package, so if their folders disappear nothing can bring them back.
- Default: When this machine has no history yet, show one scrollable Welcome page in a layer, not a multi-step wizard.

1) System check: read-only, takes a few seconds, shown as a FormCard list.
- GRUB 2.14 with grub-mkconfig and grub-mkfont
- boot-image tool and kernel count (initramfs-tools 0.151 with 5 kernels, or dracut)
- Plymouth 24.004 plugins (two-step, script, label-pango) and renderers
- Plasma 6.6 with ksplashqml
- ffmpeg with libwebp_anim
- system helper and polkit policy (installed, version)
A missing item disables only what depends on it, with the reason and a copyable owner-run install command.

2) Your current look: three stage cards, with the note 'Personae saves each stage as Original right before it first changes it'. A checkbox, on by default, copies these three themes into the library, so they work in bundles and Original stays restorable.

3) Other installed themes:
- Third-party ones, unchecked: crossgrub, mint-galaxy, Penrose, plus WindozeXP and vortex-ubuntu marked 'nested, repaired on import'.
- Package-owned stock themes (bgrt, spinner, breeze, kubuntu-logo…) in a collapsed group, unchecked.

Finish leads to Overview.

Original is captured per stage, at Personae's first change to that stage.
- The pinned History row shows each stage as 'Saved <date>' or 'Not changed yet'.
- Restore Original restores only the saved stages.
- Adopting outside changes never alters Original.
- Notes: This refines round 6's 'state before the first apply'. Backups cover only touched files, so if Original were captured all at once, a Plasma-Splash-only first apply would freeze a GRUB/Plymouth Original whose files were never backed up. Package ownership was checked with dpkg-query -S.

#### How errors, automatic rollbacks and interrupted applies are presented

- Priority: mvp; owner decision needed: no
- Why: Round 6 decides on automatic rollback, but not how the owner learns about a failure, a failed rollback, or an apply cut off by a crash or power loss. Rolling back Plymouth means a second ~40 s rebuild. And because applies are all-or-nothing across all five kernels, one broken old kernel blocks every boot splash apply.
- Default: Five outcomes, each with a fixed presentation.

(a) Problems before apply: Apply is disabled, and a Problems InlineMessage has Show links to the fields (HIG: disable confirm on invalid input).

(b) polkit cancelled or denied: an info message on the review page, not logged as a failure.

(c) Failed and rolled back:
- The failed step turns red, and 'Rolling back' steps follow in the same list, including the second boot-image rebuild with its own progress.
- The result page is a Warning, not an Error: 'Couldn't apply Neon Night. Your previous setup is back; nothing changed.'
- It gives the cause in plain words and names third-party failures as such (HIG), e.g. 'update-initramfs failed for kernel 7.0.0-27: a hook from another package exited with an error. Not caused by the theme.'
- Actions: Show log, Copy details, and a Fix action when one is known.
- History gets a greyed, non-restorable 'Rolled back' row that doesn't count toward the 10.

(d) Rollback failed:
- An Error result page, plus a persistent Error InlineMessage on Overview and a warning icon on its sidebar entry until it is fixed.
- It states what is known, e.g. 'GRUB unchanged; 3 of 5 boot images have the new splash'.
- It lists the exact owner-run commands and the backup folder.
- Retry restore… goes through the normal flow.

(e) Interrupted: the helper writes a journal before its first change. At the next start, Overview shows 'The apply on 5 Nov 14:02 didn't finish', with Repair… (restores the pre-apply state through the normal review) and Show log.

Errors never use passive notifications.
- Notes: (e) needs a helper-side transaction journal; put it in the architecture proposal.

#### History page and restore semantics

- Priority: mvp; owner decision needed: no
- Why: Round 6 makes every apply restorable and keeps 10 entries plus Original. Still open: what 'restore entry N' means when that apply touched only one stage, how pruning shows up, whether a restore rewrites newer history, and what happens when a theme folder it needs is gone. Timeshift's hourly btrfs snapshots of / can roll those folders back.
- Default: History is a list, newest first, with Original pinned on top. Each row shows:
- date
- title: Applied bundle X / Applied Boot Splash Y / Restored Z / Adopted outside changes / Rolled back
- three stage icons, with the changed ones highlighted
- a 'Current' chip on the entry that matches the live system

Entry detail (second column):
- what each stage was set to, with a thumbnail if available
- backed-up files (viewable), the privileged log, the duration
- actions: Restore…, Export log

Restore goes through the normal review flow.
- The target is the full state right after that entry, with per-stage checkboxes on the review page, all checked by default. With only Plasma Splash checked, no polkit is needed.
- A restore adds a 'Restored <entry>' entry and never truncates newer ones.
- It rewrites only the keys and files Personae manages and diffs against the live files, so other lines (e.g. GRUB_CMDLINE_LINUX_DEFAULT) are never reverted.
- A missing folder is reinstalled from its library copy. If there is no copy, that stage is blocked and the reason shown.
- When an 11th entry would drop the oldest one, the review page names that entry and any theme folder uninstalled with it.

Restore Original sits in Overview's overflow menu and on the pinned row, and always goes through review.
- Question: When you restore an entry, should it bring back the whole system as it was right after that entry (default), or only re-apply the stages that entry changed?
- Notes: Keep one install ID per design (personae-<slug>), so the grids in System Settings' Boot Splash Screen and Splash Screen pages don't list every kept version. Restoring an older entry rebuilds that entry's pinned design snapshot into the same folder; history pins the snapshots it uses. Later: a console command such as `personae-helper restore original` for when the GUI can't run.

#### Drift found at startup

- Priority: mvp; owner decision needed: no
- Why: Round 6 says outside changes are detected, never silently overwritten, and can be adopted or re-applied. Still open: when the check runs, what it can see without root (grub.cfg and the initrds are 0600), how drift is shown, and how to keep an outside change on purpose. Two likely causes are installed here. System Settings › Boot Splash Screen (kde-config-plymouth) writes plymouthd.conf, runs `update-alternatives --set` and only `update-initramfs -u`. Timeshift restores roll / back.
- Default: When: at app start and whenever the window gets focus. No daemon, tray or autostart (non-goal: change as little on the host as possible).

What (read-only):
- effective GRUB keys from /etc/default/grub plus grub.d/*.cfg, parsed, not sourced
- hashes of the installed theme folders
- the default.plymouth alternative and plymouthd.conf Theme=, and whether they agree
- ksplashrc Theme= and the package hash
grub.cfg and the initrds show as 'not checked (needs administrator rights)'.

Compare the live state with every kept entry, not only the last one. If it matches an older entry, say so: 'Matches the state from 3 Nov, perhaps restored from a Timeshift snapshot'.

Presentation:
- The stage card gets 'Changed outside Personae'.
- A header Warning InlineMessage names what changed, the likely cause and the effect, e.g. 'Boot Splash is now Breeze, probably via System Settings › Boot Splash Screen. Only the newest kernel's boot image was rebuilt, so older kernels still show Neon Night.'
- Overview's sidebar entry gets a warning icon.

Actions:
- Adopt: no root needed. Records the live state as 'Adopted outside changes', backs up the world-readable config files, and offers to import a theme that isn't in the library.
- Re-apply <last>…: the normal flow.
- Details: an expected-vs-actual table.
- Closing the message snoozes it until the live state changes again.

If the alternative and plymouthd.conf disagree, the stage shows 'Inconsistent'. The next apply lists outside changes on its review page as 'will be replaced'.
- Notes: kcm_plymouth's helper only knows update-initramfs and mkinitcpio, not dracut. Re-check its behaviour after the dracut-based reinstall.

#### Read-only mode while the system helper is missing or outdated

- Priority: mvp; owner decision needed: no
- Why: The owner installs the polkit policy and helper as root. They fall behind after every git pull and rebuild of this from-source app, and a Timeshift restore can remove or downgrade them. Without an explicit state, Apply either fails deep inside pkexec or talks to a helper with an older protocol.
- Default: On every start:
- Check the policy file and the helper at their fixed paths.
- Run `<helper> --version` without privileges to compare protocol versions.

States: OK, Not installed, Outdated, Newer than the app.

In any state other than OK:
- Overview shows a header Warning InlineMessage with the exact owner-run install command (Copy), what it installs and how to remove it.
- Apply and Restore for Boot Menu and Boot Splash are disabled, with the reason as a tooltip.
- Designing, previews, Test, export and Plasma-Splash-only applies keep working.

About shows both versions.

#### App settings versus system options

- Priority: mvp; owner decision needed: no
- Why: The owner asked about settings. Most candidates are really system changes (GRUB timeout, resolution order) that need review, polkit and history, and nothing says where they live or whether designs or bundles carry them. A countdown bar designed with a 0 s timeout makes no sense.
- Default: App settings: Kirigami Addons ConfigurationView with FormCard pages, stored per user in KConfig, applied instantly, no polkit. Three pages:
- Library location (FormFolderDelegate; asks 'Move library' or 'Use the library found there')
- Keyboard shortcuts (ShortcutsConfigurationModule)
- About / System information: app and helper versions, detected toolchains, library and log paths, Copy

Deliberately not settings:
- history size (fixed at 10 plus Original)
- resolutions (fixed by round 5)
- the drift check (always on; the HIG advises against on/off feature switches)
- the dry run (a ground rule)

System options: on Overview's Boot Menu card, under 'Menu options…'.
- Timeout in seconds, with an Error-type InlineMessage at 0 ('no way into the menu').
- Resolution order 1440p→1080p→720p, shown read-only.
- Changes go through review → polkit → history, and are not part of designs or bundles.

The GRUB editor previews the countdown with the current timeout, and warns when a countdown bar or label is designed but the timeout is 0.
- Question: Should a bundle be able to carry the GRUB menu timeout (e.g. a 0 s gaming look and a 10 s maintenance look), or does it stay one system-wide option?

#### After an OS reinstall: welcome-back flow

- Priority: mvp; owner decision needed: no
- Why: The library on /home survives the planned reinstall. History entries, though, point at paths, kernels and an initramfs generator that won't exist any more (the fresh install will likely use dracut), and the helper and policy are gone. The Plasma splash lives in ~/.local and ksplashrc, so it survives, while GRUB and Plymouth go back to stock. Restoring old entries onto the new system would be wrong.
- Default: Key system history by /etc/machine-id, which is new on every install and world-readable.

If the library exists but has no history for this machine, show a 'Welcome back' version of the first run:
- Re-run the system check: generator detected, helper 'Not installed'.
- Show the previous installation's history read-only under 'Previous installation'. Logs can be viewed; nothing can be restored.
- Offer 'Re-create your last look': a bundle prefilled from the previous install's last applied entry, applied through the normal flow.

Overview shows Plasma Splash as still applied, and Boot Menu and Boot Splash as stock. The new Original is captured from the fresh install.

Designs keep the font files they use in the asset library, copied the first time a system font is chosen, so builds don't break when the minimal install lacks that font.

#### Design editor: saving, undo, problems, preview controls

- Priority: mvp; owner decision needed: no
- Why: Round 4 takes a snapshot on every save. Still open: whether saving is explicit or automatic (autosave would bury the history in snapshots), how in-editor undo relates to snapshots, where problems show, and how the editor exposes round 5's resolutions and the shared assets of round 3 Q6.
- Default: Saving:
- Explicit save with Ctrl+S, and a dirty dot in the page title.
- In-memory Undo/Redo (Ctrl+Z / Ctrl+Shift+Z), separate from saved versions.
- Leaving or quitting with unsaved changes shows a PromptDialog: Save / Discard / Cancel.
- Each save is a snapshot. A Versions dialog lists them with thumbnail and time; 'Restore this version' saves a new version, so nothing is lost.

Layout: a rough-preview canvas plus a FormCard property panel.
- GRUB: Background, Images, Menu, Selection, Countdown, Font, Icons
- Plymouth: Background, Spinner/Progress, Text
- KSplash: Video/Background, Fade, Indicator

Preview controls:
- A SegmentedButton switches between 1440p, 1080p and 720p.
- Clicking an element on the canvas scrolls to its section.
- Play animates the spinner and countdown.
- Plasma Splash adds Test: it builds the package and runs `ksplashqml --test --window <path>`.

Problems show per field and in a page-level InlineMessage. They block Apply, not Save.

New Design dialog: name, stage, and a starting point: Blank, Duplicate, or 'Match <design>' (reuses that design's background and spinner assets). The slug is derived from the name, frozen at the first apply, and shown under Details.

#### Import flow and imported-theme pages

- Priority: mvp; owner decision needed: no
- Why: Imports are read-only copies with Re-import (rounds 1, 2 and 4), and the importer must find nested theme roots and never run bundled scripts. Still open: which sources it takes, what it reports, how duplicates are handled, and where thumbnails come from, since arbitrary script themes can't be rendered and plymouth-x11 isn't installed.
- Default: Where: Import… in each stage toolbar and on Overview, or drop a folder or archive on the window.

Sources:
- Installed theme (the first-run scanner)
- Folder
- Archive (.tar.*, .zip), unpacked into library staging with path-traversal and symlink checks
- Personae export archives, detected automatically; these restore editable designs, bundles and assets

Report step:
- the theme root found, explaining any nesting (e.g. WindozeXP)
- the module and how it fits this system (two-step and script yes, label-freetype no)
- fonts that will fall back to Ubuntu under initramfs-tools
- scripts that are ignored (fallout ships preview.sh)
- a verdict: OK, Works with limitations, or Won't work (importable, but Apply is blocked)

Duplicates: the same source path offers Re-import; the same content hash says 'Already in your library'.

Re-import: lists added/changed/removed files, re-validates, keeps the item's identity, and marks an applied import 'Edited since applied'.

Thumbnails:
- Plymouth: preview.png (System Settings' Boot Splash Screen page reads it)
- KSplash: contents/previews/splash.png
- GRUB: desktop-image or background.png
- otherwise a placeholder
KSplash imports also get a live Test. Generated packages ship the same preview files, so Personae themes look native in System Settings.
- Notes: KNewStuff 'Get New…' is a non-goal for v1; downloaded archives go through Import…. INTERVIEW.md round 1 Q6 still says 'changes happen on clones inside the app'; round 2 Q3 replaced that, so SPEC should drop the clone wording.

#### Bundle editor and broken references

- Priority: mvp; owner decision needed: no
- Why: Bundles reference designs and imports instead of copying them (round 4 Q2). Still open: how a slot is filled, what a bundle with a problem does when applies are all-or-nothing, and what happens to a slot whose design is deleted, or edited after the apply.
- Default: - Bundle cards show three thumbnails in boot order, with Active and Edited since applied chips.
- The editor has three slot rows (FormCard). Each opens a picker with that stage's designs and imports, Leave as is, and New design….
- Apply… on a bundle is disabled while any slot has problems; an InlineMessage lists them with Open links.
- Overview's header says 'Matches bundle X' when the live system matches all of the bundle's filled slots.
- Deleting a design or import that a bundle or history entry uses first shows a PromptDialog listing them. Afterwards the slot shows 'Missing' as an error, never silently 'Leave as is'.
- Notes: Later: Play sequence, a rough preview of GRUB → Plymouth → KSplash in order.

#### Undo for library deletions

- Priority: mvp; owner decision needed: no
- Why: The owner wants undo everywhere, but round 4's snapshots only cover saves. Deleting a design throws away all its versions, and nothing brings them back.
- Default: - Delete moves the item's folder to library/.trash/ and shows a passive notification 'Deleted Neon Night' with Undo (HIG: passive notifications for low-importance messages, and keep dialogs to a minimum).
- A PromptDialog appears only when bundles or history entries use the item.
- A Recently deleted page, opened from the library pages' overflow menu, offers Restore and Delete permanently.
- Items are purged after 30 days, but never while a history entry needs them for restore.
- Assets show 'Used by N' and follow the same rule.

#### Feedback for background work and empty views

- Priority: mvp; owner decision needed: no
- Why: Turning a KSplash video into animated WebP at three resolutions can take minutes, far longer than the 40 s apply. The first launch shows mostly empty views. Neither is specified.
- Default: Background work:
- Video assets are encoded when added, not at apply time.
- The asset card shows progress (frames done/total from ffmpeg -progress) and Cancel; results are cached by source hash and parameters.
- A design whose asset is still encoding shows 'Preparing video 45%', and Apply waits in its Preparing step.
- Builds are cached, so restores and re-applies skip rendering.
- No global jobs panel: progress shows where the work was started.

Empty views: Kirigami.PlaceholderMessage with one sentence and one action.
- Stage page: 'No boot splashes yet', with New Design and Import….
- Bundles: 'A bundle applies one look to all three stages at once', with New Bundle (disabled until a design or import exists).
- Assets: 'Images, videos and spinners you add here can be used by every design', with Add… and a drop zone.
- History: 'Nothing applied yet. Personae saves each stage as Original before it first changes it.'
- Search with no results: no action.

A missing tool shows a FormPlaceholderMessageDelegate with the install command inside the affected editor section. Kirigami.LoadingPlaceholder covers the first system scan.

#### Simulation mode for clicking through apply and rollback

- Priority: mvp; owner decision needed: no
- Why: Only the owner may run real applies, and the rollback, failed-rollback and interrupted screens are hard to trigger on a real system. Without a simulation, the most safety-critical screens would first be seen during a real failure. Fake system roots with stubbed tools already exist for tests (round 3 Q1).
- Default: - A launch option `--simulate <dir inside the repo>` runs the whole UI against a fake system root with stubbed tools, without pkexec.
- A permanent banner says 'Simulation: nothing on this computer changes'.
- An optional failure switch (fail at step N, fail the rollback, kill the helper mid-rebuild) produces every outcome from the error-presentation gap.
- The same mode drives UI tests without root.

### Tech stack, CLI, packaging and development workflow

Recommended stack: C++20 + Qt 6.10 Quick/QML + Kirigami 6.24 (KF6 CoreAddons and Config; Kirigami Addons FormCard used from QML only), CMake 4.2/ECM/Ninja, QtTest/ctest, a QtCore-only C++ helper run via pkexec, and a CPack-built .deb with no maintainer scripts. One binary `personae` is both GUI and CLI over a shared core library.

Why this stack: every build dependency is already installed (pulled in by the hand-installed plasma-workspace-dev), so work starts with zero host changes. At runtime it adds nothing a Plasma install lacks.

The credible alternative is Python + PySide6. It is faster to write and pytest is already installed. Against it:
- It needs three more apt packages for QML.
- Ubuntu ships no KF6 Python bindings.
- qmllint cannot see types registered from Python, so mistakes in AI-written QML only show up at runtime.

Ruled out:
- PyQt6: its QML bindings are installed, but it is GPLv3 and only pulled in by Kubuntu tools.
- PySide6 from pip: it cannot load the system Kirigami.
- Qt Widgets: every preview would be hand-painted.
- Rust/cxx-qt: no toolchain installed.
- AppImage or an install script.

Checked on this machine:
- Plain `qmllint` and `qmltestrunner` on PATH are broken qtchooser wrappers pointing at Qt5. Use /usr/lib/qt6/bin or the all_qmllint target.
- clang-format, xvfb and ruff are missing. Offscreen QPA replaces xvfb; ruff is not even in the archive.
- pkexec 127 supports exec.argv1.
- ECM installs helpers to /usr/lib/x86_64-linux-gnu/libexec.
- polkit 127 also reads /usr/local/share and /etc action dirs.
- The installed Qt image plugins already read WebP, AVIF, JXL, PSD, XCF and KRA, so ImageMagick is not needed.

Questions for the owner:
1. DECISION: C++ (recommended) or Python? Will you read or hand-edit the core code yourself, and in which language?
2. CONFIRM: a v1 CLI with operational commands only (status, list, build, apply, restore, export, import); design editing stays in the GUI.
3. CONFIRM: Personae is distributed only to your own machine as a locally built .deb, with no public releases.

#### Implementation stack (language, UI toolkit, test framework)

- Priority: mvp-blocker; owner decision needed: yes
- Why: Nothing can be built until this is fixed. It decides the KSplash preview, the helper's language, the test tooling and the packaging. Compared on this machine: (1) Native look: Kirigami 6.24 + qqc2-desktop-style (installed) works the same from C++ and from distro PySide6. Qt Widgets also looks native, but every preview would be hand-painted, and PySide6 has no QtQuick bindings installed for an embedded KSplash preview. (2) KSplash live preview: either QML variant can load the generated Splash.qml in-process with the same Kirigami that ksplashqml uses. (3) Image/video: no difference; both use QImage/QPainter plus ffmpeg and grub-mkfont subprocesses. (4) Helper: a QtCore-only C++ binary or a stdlib-only Python script ('#!/usr/bin/python3 -I'); both are fine. (5) Tests: QtTest/ctest vs pytest (installed); both handle goldens and fake roots. (6) Packaging: a CPack .deb for both. The C++ app needs only libraries every Plasma install already has; PySide6+QML adds python3-pyside6.qtqml, .qtquick and .qtquickcontrols2 (in the archive, not installed). (7) KF6: C++ gets KConfig (writes ksplashrc correctly, KConfigXT settings) and KAboutData; Ubuntu ships no KF6 Python bindings (verified). (8) AI speed: Python is quicker to write. In C++, qt_add_qml_module lets qmlcachegen and qmllint check QML against the C++ types at build time; types registered from Python are invisible to qmllint without extra generation steps, so mistakes in AI-written QML only show up at runtime.
- Default: C++20 (g++ 15) + Qt 6.10 Quick/QML + Kirigami 6.24. Use KF6 CoreAddons (KAboutData) and KF6 Config (KConfigXT settings, ksplashrc). Use Kirigami Addons FormCard from QML only: the runtime modules are installed, kirigami-addons-dev is not, so no find_package for it. Build with CMake 4.2 + ECM + Ninja; test with QtTest + ctest; write the helper in C++ against QtCore only; package as a CPack .deb. Every build dependency is installed today, so development starts with zero host changes. Keep model boilerplate small: QVariantList/Q_GADGET for short lists (profiles, bundles, history); a QAbstractListModel only for the asset grid.
- Question: C++/Qt/Kirigami (recommended) or Python/PySide6? Will you read or hand-edit the core code yourself, and in which language?
- Notes: Ruled out: PyQt6 (its QML bindings are installed, but it is GPLv3, against the MIT intent, and only pulled in by ubuntu-release-upgrader-qt, apport-kde and similar tools that a minimal reinstall may drop); PySide6 from pip (bundles its own Qt, so it cannot load the system Kirigami plugin; KDE's Python+Kirigami tutorial uses distro packages and 'venv --system-site-packages'); Rust/cxx-qt (no cargo or rustc installed). If Python is chosen: the owner installs the three PySide6 QML packages; a repo-local .venv with --system-site-packages; ruff installed by pip inside .venv (it is not in the archive); a stdlib-only helper; ksplashrc written through a kwriteconfig6 subprocess.

#### Dev sandbox and --sysroot mode (making the hard rule mechanical)

- Priority: mvp-blocker; owner decision needed: no
- Why: By default Qt/KDE code writes to ~/.config, ~/.local/share and ~/.cache (QML disk cache, fontconfig, Mesa shader cache). gcc and QTemporaryDir write to /tmp. The KSplash apply path writes ksplashrc and ~/.local/share/plasma. Round 3 settled fake roots and stubbed tools but not the mechanism. Without one, the first test or GUI launch in a future session breaks the hard rule.
- Default: (a) CMakePresets.json 'dev' configure/build/test presets set TMPDIR, HOME and XDG_{CONFIG,DATA,CACHE,STATE}_HOME to <repo>/.sandbox/ (gitignored), plus QT_QPA_PLATFORM=offscreen for tests. 'cmake --build --preset dev' and 'ctest --preset dev' then cannot forget them. XDG_RUNTIME_DIR is left alone. (b) Dev builds are sealed off by construction. CMake option PERSONAE_DEV_SANDBOX (ON in the dev preset, OFF in the release preset that makes the .deb) makes the binary point HOME, XDG_* and TMPDIR at .sandbox/ before QGuiApplication starts, and default to --sysroot .sandbox/root (seeded from a fixture). '--sysroot /' allows read-only use of the real system (status, import); any real write still needs the installed helper and the owner's password. (c) --sysroot DIR (app, CLI, helper) resolves every system path and every system-tool path under DIR; user-level KSplash targets follow XDG_* instead. In sysroot mode the core runs the build-tree helper directly, without pkexec. (d) Fixtures tests/fakeroots/initramfs-tools/ and tests/fakeroots/dracut/ mirror the verified facts. They hold stub tools at the real paths (DIR/usr/sbin/update-initramfs, DIR/usr/sbin/grub-mkconfig, DIR/usr/bin/update-alternatives, DIR/usr/bin/dracut) that append their argv to DIR/stub-calls.log. (e) The helper rejects --sysroot when euid is 0, because a user-controlled directory plus root writes allows a symlink attack. pkexec already strips the environment.
- Notes: Optional extra safety: release builds refuse apply/restore while CLAUDECODE is set (Claude Code sets it in its shells; verified here). About 3 lines; it catches a future session running the wrong build.

#### Helper invocation, polkit actions and protocol

- Priority: mvp; owner decision needed: no
- Why: Round 6 fixed pkexec, an installed policy and kept authorization, but not how the app talks to the helper. That decides the prompt texts, what happens when a build-tree app meets an older installed helper, and whether 'the dry run is exactly what gets applied' holds in practice.
- Default: Paths and policy:
- The helper path comes from one CMake variable, KDE_INSTALL_FULL_LIBEXECDIR/personae-helper (ECM default here: /usr/lib/x86_64-linux-gnu/libexec/personae-helper).
- The .policy is generated from that variable with configure_file, and the same path is compiled into the app. A mismatched exec.path makes pkexec fall back to its generic 'run as super user' prompt.
- Two actions, selected by org.freedesktop.policykit.exec.argv1: io.github.nomoruyi.personae.apply and .restore, each with its own message. Defaults: allow_any=no, allow_inactive=no, allow_active=auth_admin_keep.

Invocation:
- The core runs 'pkexec <helper> apply|restore' directly from the long-running process (QProcess, never via sh).
- polkit keeps the authorization per calling process and action, so one GUI session gets the 5-minute window.
- The CLI applies a whole bundle in one helper call, so one prompt.

Protocol:
- 'apply' reads one JSON plan on stdin: {protocol, ops[], files{dest: base64 + sha256}, expect{path: sha256 of current content}}.
- Fixed ops: install-theme, write-grub-defaults, set-alternative, set-plymouthd-theme, run-grub-mkconfig, rebuild-initramfs. The helper detects the initramfs generator again itself.
- The helper re-checks every op: names match ^personae-[a-z0-9-]+$, destinations come from compiled-in allowlists, file types are limited to .png/.jpg/.pf2/.txt/.plymouth/.script, total size is capped at about 64 MB.
- If any 'expect' hash differs, the helper aborts with 'system changed since the dry run'.
- Progress goes to stdout as JSON lines. Exit codes: 0 ok, 1 failed and rolled back, 2 rollback incomplete.
- A protocol integer is checked first, so an app newer than the installed helper reports 'reinstall the .deb'.
- No helper config file, nothing in /etc/personae.
- Notes: Verified: pkexec 127 supports exec.argv1 (the installed meson policy uses it). polkit's KEEP applies to the same action and subject, and pkexec's subject is its parent process. Because KEEP is per action, a manual restore right after an apply prompts again. That is acceptable, arguably correct.

#### Helper trust boundary, crash safety and privileged log

- Priority: mvp; owner decision needed: no
- Why: The helper runs as root for about 40 s per apply, on input from user space. The interview fixes rollback and logging, but not where backups live, what happens if the GUI dies or the user logs out mid-apply, or where the privileged log goes.
- Default: (1) Root never opens user-owned paths. Built packages arrive inside the stdin plan, which rules out symlink and race tricks in ~.
(2) System history and backups are root-owned under /var/lib/personae/. Entry metadata is mode 0644, so GUI and CLI can list history without a prompt. The helper never restores from the user library. History sits on / and dies with a reinstall, like the state it describes; designs in /home survive.
(3) Emergency fallback: 'sudo <helper> restore original' works with no GUI, no PKEXEC_UID and no home directory.
(4) The helper ignores SIGPIPE: once started, an apply finishes or rolls back even if the GUI crashes. It re-executes itself once under 'systemd-run --scope' so a Plasma logout cannot kill it; otherwise the pkexec child sits in the GUI's user cgroup, which systemd tears down after logout. An flock on /var/lib/personae/lock stops GUI and CLI from applying at the same time.
(5) Rebuild commands run inside 'systemd-inhibit --what=shutdown:sleep --mode=block --who=Personae --why=...', so shutdown waits for the ~40 s rebuild with no extra code.
(6) The privileged-action log goes to syslog(3) under the tag personae-helper ('journalctl -t personae-helper'). Each run's log is also copied into its history entry.
(7) Drift detection at startup reads only world-readable state. grub.cfg and initrd checks (mode 0600) run only inside apply/restore, so opening the app never asks for a password.

#### CLI scope and one shared core

- Priority: mvp; owner decision needed: no
- Why: Not discussed yet. On a shared core a CLI costs almost nothing. It allows recovery from a text console when Plasma is broken, makes the apply path scriptable, and lets tests and future sessions run the whole flow without a display.
- Default: One binary:
- 'personae' with no arguments starts the GUI. With a subcommand it runs the CLI (QCommandLineParser) and never loads QML.
- CLI mode sets QT_QPA_PLATFORM=offscreen when unset, because rendering text into images needs a QGuiApplication.

Commands:
- 'status [--json]': per target, the active theme, whether Personae manages it, drift since the last apply, and the detected initramfs generator. Exit code 3 on drift.
- 'list profiles|bundles|assets|history [--json]'.
- 'build <profile|bundle> [-o DIR]': standard packages, no root. ksplashqml --test --window works directly on the output.
- 'apply <bundle|profile> [--dry-run] [--yes]' and 'restore <entry|original> [--dry-run] [--yes]': print the same dry run as the GUI, then ask y/N.
- 'export <name> -o FILE' and 'import <path> [--refresh]'.
- Design editing and previews stay in the GUI.

Code layout:
- Static library personae-core (QtCore + QtGui, no QML): model, library store, generators, imaging, system probe, plan/diff, helper client.
- The GUI is QML plus thin QObject adapters; the CLI is one cli.cpp over the same calls; the helper links only the plan/validation part.

Shared state:
- Settings live in ~/.config/personaerc via a KConfigXT-generated class (library path from round 4, among others); --library DIR overrides it.
- A QLockFile around each library write makes a CLI run safe while the GUI is open.
- Question: A v1 CLI with operational commands only (status, list, build, apply, restore, export, import), design editing GUI-only: OK?

#### Packaging: .deb contents, dependencies and uninstall behaviour

- Priority: mvp; owner decision needed: no
- Why: The polkit policy and a root-owned helper must sit at fixed system paths. How they get there decides how reversible the install is ('change as little as possible') and whether generated themes keep working after the app is uninstalled (CLAUDE.md rule).
- Default: Build:
- 'cpack -G DEB' from the CMake install rules. No debian/ directory and no debhelper (not installed); dpkg-shlibdeps (installed) fills in library dependencies.

Contents:
- /usr/bin/personae
- <libexecdir>/personae-helper (root, 0755)
- /usr/share/polkit-1/actions/io.github.nomoruyi.personae.policy (polkitd picks it up without a restart)
- /usr/share/applications/io.github.nomoruyi.personae.desktop
- /usr/share/metainfo/io.github.nomoruyi.personae.metainfo.xml
- a hicolor SVG icon

Dependencies written by hand for what shlibdeps cannot see:
- QML modules: qml6-module-org-kde-kirigami, -org-kde-desktop, -qtquick-controls, -qtquick-layouts, -qtquick-dialogs, -org-kde-kirigamiaddons-formcard.
- Tools: ffmpeg, grub-common, plymouth, pkexec, plasma-workspace (for ksplashqml).

Uninstall:
- No maintainer scripts. dpkg owns none of the installed themes, the GRUB settings, the alternatives or /var/lib/personae, so 'apt remove' leaves the current look working.
- Purge keeps the history. Documented options: restore Original first, or 'sudo rm -r /var/lib/personae'.

Details:
- The app calls QGuiApplication::setDesktopFileName('io.github.nomoruyi.personae'); otherwise Plasma on Wayland shows a generic window icon.
- The owner installs with 'sudo apt install ./build/release/personae_<ver>_amd64.deb' (add --reinstall for the same version) and undoes with 'sudo apt remove personae'.

Rejected:
- An install script: untracked files under /usr, no uninstall.
- An AppImage: it mounts at a random user-writable path, so it cannot serve as a pkexec exec.path, and it would have to bundle KF6.
- Question: Only your machine via a locally built .deb, or also releases for others?
- Notes: Releases for others would add a proper debian/ directory, signed GitHub releases or a PPA, and a review of the Kubuntu-specific assumptions. Nothing else changes, so the default is local only, which matches 'primarily personal, private repo'. polkit 127 also reads /usr/local/share/polkit-1/actions (verified), so prefix is not the reason the install script loses; untracked files are.

#### Repo layout and the commands for CLAUDE.md

- Priority: mvp; owner decision needed: no
- Why: CLAUDE.md asks for build, test, lint and single-test commands once a stack exists. Fixing them now keeps future AI sessions consistent. Two traps on this machine: the plain qmllint and qmltestrunner on PATH are qtchooser wrappers for Qt5 binaries that are not installed (verified: 'could not exec /usr/lib/qt5/bin/qmllint'), and clang-format is not installed.
- Default: Layout:
- CMakeLists.txt, CMakePresets.json, .gitignore (build/, .sandbox/)
- src/core/: model, store, generators/, imaging, probe, plan, helperclient
- src/helper/
- src/app/: main.cpp (GUI/CLI dispatch), cli.cpp, adapters
- src/app/qml/: Main.qml, pages/, editors/, previews/
- src/templates/: theme.txt, .plymouth, Splash.qml, metadata.json, compiled in as resources
- data/: .desktop, metainfo, .policy.in, icon
- tests/: test_*.cpp, golden/<generator>/<case>/, fakeroots/, stubs/
- tools/sandbox: a 5-line env wrapper for running test binaries directly

Commands:
- Configure: 'cmake --preset dev'
- Build: 'cmake --build --preset dev'
- All tests: 'ctest --preset dev'
- Single test: 'ctest --preset dev -R grubtheme', or 'tools/sandbox build/dev/bin/test_grubtheme testGolden'
- Update goldens: 'PERSONAE_UPDATE_GOLDENS=1 ctest --preset dev -L golden', then review 'git diff tests/golden'
- Lint: 'cmake --build --preset dev --target all_qmllint', 'appstreamcli validate --no-net data/*.metainfo.xml', 'desktop-file-validate data/*.desktop'
- Qt tools only by full path: /usr/lib/qt6/bin/qmllint, qmlformat, qmlls
- Run: 'build/dev/bin/personae' (sandboxed by construction)
- Package: 'cmake --preset release && cmake --build --preset release && cpack --preset release'; the owner installs it.

Formatting: ECM's KDEClangFormat target once the owner installs clang-format (one apt package). Until then, -Werror warnings from KDECompilerSettings plus qmllint.

#### Golden-file, QML and rollback test mechanics

- Priority: mvp; owner decision needed: no
- Why: Goldens are required, but image outputs, the validity of generated QML and the all-or-nothing rollback (round 6) each need their own approach. Otherwise the tests are brittle, or the risky paths go untested.
- Default: Determinism and goldens:
- Generators are deterministic: no timestamps, random IDs or absolute paths in output. Export archives use fixed mtimes and ordering.
- Text goldens (theme.txt, .plymouth, .script, Splash.qml, metadata.json, GRUB settings) are byte-exact, one directory per case. They are regenerated only with PERSONAE_UPDATE_GOLDENS=1 and reviewed in git diff.
- Image outputs: check size and format, then compare decoded pixels with a small tolerance. Qt scaler or zlib updates break byte-exact PNG comparisons.

QML smoke test:
- Load each generated Splash.qml under offscreen QPA with QT_QUICK_BACKEND=software (no xvfb needed, and none is installed).
- Step the root 'stage' property from 1 to 6. ksplashqml sets this property; confirmed in Breeze, Overload and Penrose.
- Fail on any QML warning.
- The same in-process check is the app's validate-before-apply step for KSplash, so qmllint never becomes a runtime dependency.

Helper tests (black-box):
- Run the real helper binary with --sysroot on a fresh copy of each fake root, then check the resulting files and stub-calls.log.
- PERSONAE_STUB_FAIL=<tool> makes one stub fail. It reaches the stubs because sysroot mode skips pkexec. Use it to prove that a failure at any step rolls the whole bundle back.

CTest labels golden, helper and qml give quick subsets.

#### Headless screenshot mode for AI-assisted UI work

- Priority: mvp; owner decision needed: no
- Why: Claude cannot see a native Wayland window and must not drive the app on the real session. Without rendering to a file, every UI change is made blind and each visual check costs the owner a round trip.
- Default: A dev flag 'personae --screenshot out.png [--size 1280x800] [--page bundles|history|settings|editor:<profile>] [--dark]'. It forces offscreen QPA and the software Quick backend, waits for the scene to settle, saves QQuickWindow::grabWindow() and exits; Claude then reads the PNG. About 30 lines. The same mechanism can later produce AppStream screenshots and preview images.
- Notes: Software rendering drops some Kirigami shader effects such as shadows. Good enough for checking layout; judging final polish stays with the owner.

#### Live preview implementation (KSplash, GRUB, Plymouth)

- Priority: mvp; owner decision needed: no
- Why: Round 2 settled that a rough preview is enough, but not how it is built. For KSplash, an in-app preview and the exact external one are both cheap and cover each other's gaps, but in-app QML previews can silently show a stale cached version.
- Default: KSplash:
- A Loader shows the built package's Splash.qml in the editor, rendered at the real logical size (switch: 2560×1440, 1920×1080, 1280×720) and scaled down to fit. Layouts based on Kirigami.Units would differ at thumbnail size.
- A Timer steps 'stage' from 1 to 6, like ksplashqml.
- Every reload after a rebuild calls clearComponentCache() or uses a versioned URL; otherwise the engine serves the cached component and the preview shows the old splash.
- A 'Run real splash' button starts 'ksplashqml --test --window <package>' (verified to take a path).

GRUB and Plymouth:
- QML components draw from the same design model the generators use (not by parsing generated files), at the three target resolutions.
- The Plymouth spinner plays the generated frames.

Video:
- Source videos play in the QtMultimedia QML Video type (runtime module installed, no -dev package needed).
- Converted output plays in AnimatedImage, exactly what the splash uses.

#### Image and video processing toolchain

- Priority: mvp; owner decision needed: no
- Why: Rounds 3 and 4 need derived assets, static text rendered into images and video → animated WebP, but nothing says which libraries or tools do the work. ImageMagick is installed and tempting, but not needed.
- Default: Images:
- QImage/QPainter for scaling, cropping, compositing and text rendering; any font loads through QFontDatabase::addApplicationFont.
- Masters are read through the installed Qt image plugins: WebP from qt6-image-formats-plugins; AVIF, JXL, HEIF, PSD, XCF and KRA from kimageformat6-plugins (verified).

Video and fonts:
- ffmpeg (libwebp_anim present) turns video into animated WebP and extracts spinner frames.
- grub-mkfont makes the .pf2 fonts.
- Both run through QProcess with absolute paths, parse '-progress pipe:1' and can be cancelled, all off the GUI thread (QtConcurrent).

No ImageMagick, and no linking against libav* or Magick++: one runtime dependency fewer, and no surprises from ImageMagick's policy.xml resource limits.

A startup check reports a missing ffmpeg or grub-mkfont clearly; both are .deb dependencies anyway.

#### Build dependencies exist by accident

- Priority: mvp; owner decision needed: no
- Why: Every C++ build dependency is present only because plasma-workspace-dev was installed by hand (verified with apt-mark and rdepends). It pulls in libplasma-dev → libkirigami-dev, libkf6config-dev, qt6-declarative-dev and ECM. The planned minimal reinstall will not have them, and 'apt autoremove' after removing plasma-workspace-dev would drop them.
- Default: Keep the exact list in the repo (tools/build-deps.txt, referenced from CLAUDE.md): build-essential, cmake, ninja-build, extra-cmake-modules, qt6-base-dev, qt6-declarative-dev, qt6-declarative-dev-tools, libkirigami-dev, libkf6coreaddons-dev, libkf6config-dev, and optionally clang-format. After the reinstall the owner runs 'sudo apt install $(cat tools/build-deps.txt)', which also marks them as manually installed. Add libkf6i18n-dev, libkf6crash-dev, libkf6dbusaddons-dev or kirigami-addons-dev only when a feature needs them. Re-check the runtime dependency list on the fresh system.

#### Deferred desktop-app extras

- Priority: later; owner decision needed: no
- Why: A full-fledged KDE app usually has these. Each costs a missing -dev package or ongoing upkeep, and none is needed for the MVP.
- Default: Deferred:
- KI18n translations. Strings are wrapped in tr()/qsTr() so translation stays possible; the UI is English.
- KCrash/DrKonqi.
- KDBusService single instance. The helper's flock and the library QLockFile already prevent conflicts.
- Bash completion and a man page. '--help' is enough; help2man later.
- CI, later: GitHub Actions in an ubuntu:26.04 container running build + ctest. It needs no root thanks to --sysroot.

Kept in v1 because they cost almost nothing:
- An About page: KAboutData through the installed org.kde.coreaddons QML module plus Kirigami.AboutPage, with 'copy diagnostics' (versions, detected initramfs generator, helper protocol).
- AppStream metainfo, so Discover can show and uninstall the app.
- qCDebug logging categories (personae.core, personae.helper), switchable via QT_LOGGING_RULES.
- Notes: If you want a German UI, KI18n moves into v1 (libkf6i18n-dev, one apt package).

### robustness, safety and lifecycle operations

INTERVIEW.md (/extra/data/Coding/linux-theming/INTERVIEW.md) settles the safety policy: a pkexec helper with a policy file, a dry run, a history with a pinned Original, all-or-nothing bundles and detection of outside changes. It does not settle how an apply actually runs, or what happens over the app's lifetime.

Read-only checks of this machine found traps that SPEC.md must cover:
- grub-mkconfig (update-grub) checks grub.cfg.new and then rewrites the live grub.cfg in place with 'cat >', with no fsync.
- GRUB's btrfs reader ignores the btrfs log tree, so it can miss files that were only fsync'd before a crash.
- 'update-initramfs -u -k all' runs newest-first and stops at the first failing kernel, which leaves some initrds new and some old.
- Both 00_header and the Plymouth initramfs hook can drop a theme silently while still exiting 0.
- Neither update-initramfs nor grub-mkconfig takes a lock.
- /etc/default/grub is managed by ucf, and plymouthd.conf is a dpkg conffile that has already been edited.
- The installed Plymouth KCM (System Settings > Boot Splash) changes only the default.plymouth alternative and rebuilds only the newest initrd. That leaves the two theme inputs disagreeing, which likely explains the broken splashes.
- Timeshift takes btrfs snapshots of @ (which includes /boot) every hour and at boot, but not of /home.
- polkit keeps an authorization per action ID and per pkexec parent process.

One recorded (Claude) proposal is wrong. Installing every build of a profile into one folder, personae-<slug>, breaks the round-6 promise that every history entry can be restored, so installed builds must be immutable and versioned.

There are 15 gaps: 5 must be settled before building and 10 belong in the MVP. None strictly needs an owner decision because each has a workable default. Four carry a question for the owner to confirm the default.

#### Installed theme folders must be immutable and versioned (the round-6 folder naming personae-<slug> is wrong as written)

- Priority: mvp-blocker; owner decision needed: no
- Why: Round 6 suggests one folder per profile: /boot/grub/themes/personae-<slug> and /usr/share/plymouth/themes/personae-<slug>. Re-applying an edited profile would then overwrite, in place, the folder that the current grub.cfg, every initrd, the kernel postinst hooks and the earlier history entries all point to. Round 6 also backs up only config files, so earlier entries could no longer be restored faithfully, which breaks 'every apply is a restorable entry'. There would also be a window in which GRUB or a kernel hook reads a half-replaced theme.
- Default: Install every build once and never modify it afterwards. Folder name and ID: personae-<slug>-<hash8>, where hash8 is the first 8 hex digits of the SHA-256 over the built file tree (sorted paths plus contents).
- GRUB: /boot/grub/themes/personae-<slug>-<hash8>/theme.txt.
- Plymouth: /usr/share/plymouth/themes/<id>/<id>.plymouth, with Theme=<id>.
- KSplash: io.github.nomoruyi.personae.<slug>-<hash8>.

Re-applying an unchanged build reuses its folder. A new build goes into a new folder next to the old one, so switching only changes which folder the config points to.

Cleanup runs only after a verified apply. It deletes personae-* folders that neither the current state, nor Original, nor the 10 kept entries reference. It never touches folders the app did not install. Each package's display name carries the profile name and the apply date, so the lists in System Settings stay readable.
- Question: Is it OK that System Settings (Boot Splash and Splash Screen) may list a few Personae versions per target? Only versions still used by the last 10 entries are kept.
- Notes: Alternative if the owner wants exactly one folder per profile: keep personae-<slug>, swap new builds in atomically with renameat2(RENAME_EXCHANGE), and archive old builds root-side in /var/lib/personae for restores. That is more code in exchange for a tidier host. The hash is taken over the build output, so an ImageMagick or grub-mkfont upgrade that changes bytes produces a new version. That is correct, but it costs one initramfs rebuild.

#### Transaction journal, step order and recovery after a crash or power loss

- Priority: mvp-blocker; owner decision needed: no
- Why: An apply takes about 1 minute: 5 initrds at roughly 8 s each, update-grub with os-prober, then verification. During that time the owner may click Restart to test, the GUI can crash, or the power can fail. INTERVIEW decides that failures roll back, but not:
- where the transaction state lives,
- the order of steps,
- what happens on the next start,
- what all-or-nothing means when the rollback itself fails (for example, an unrelated initramfs hook breaks again).
- Default: Store: /var/lib/personae/, owned by root and readable by everyone.
- state.json.
- history/<seq>/entry.json with state planned, applying, applied, rolled-back or needs-attention.
- history/<seq>/backup/: exact bytes plus mode and owner of every file about to be touched, or an 'absent' marker; also the alternative's mode and value.
- history/<seq>/steps.jsonl: append-only, fsync after every line.
- history/<seq>/log.txt and verify.json.
- Original lives in history/0000-original.
- Entries are ordered by a counter, not a timestamp, because Windows dual-boot can shift the clock.
- The store sits in subvolume @, so a Timeshift rollback rolls it back together with /boot and /etc.

Order of the system part (every intermediate state boots):
1. Take the locks and a logind block inhibitor (blocks shutdown and sleep).
2. Preflight checks.
3. Write the backups.
4. Stage and validate the new files.
5. Rename the new versioned folders into place; nothing that already exists is modified.
6. Write the configs (temp file, fsync, rename) and set the alternative.
7. Rebuild the initrds one kernel at a time, logging each one.
8. Regenerate grub.cfg.
9. syncfs.
10. Verify, then mark the entry applied.

The GUI then applies KSplash, with its own small journal under $XDG_STATE_HOME/personae/. If that fails, the GUI calls the helper's restore; no new prompt appears within the 5-minute window. Cleanup runs last.

Recovery:
- While any entry is planned or applying, the helper refuses every verb except 'recover'.
- recover restores the backups, rebuilds only the kernels whose initrd changed, regenerates grub.cfg, syncs and verifies.
- If recovery fails, the entry becomes needs-attention, showing the failing command and RECOVERY.txt. The helper never retries in a loop.
- On start, the GUI reads the store without a prompt and shows 'Interrupted at step X; the system boots either way' with [Roll back] as the default (per round 6), or [Finish] when only the KSplash step is missing.
- Cancel is no longer possible once step 5 starts.
- Every external command has a 5-minute watchdog; on timeout the helper kills the process group and rolls back.

Spec wording: config files and grub.cfg are all-or-nothing. For initrds the guarantee is 'each one is complete and either old or new', because update-initramfs replaces a file only after a successful build.
- Notes: The dry run should be computed from inputs everyone can read. The apply then carries a state token: hashes of every input file plus the alternative's value. The helper aborts if the token changed between preview and click, for example because apt or the KCM changed something.

A full grub.cfg diff is not possible: the file is root-only, and grub-mkconfig needs the new drop-in already in place. So the dry run shows the predicted 'set theme=' and 'loadfont' lines, and verification checks exactly those.

#### Trust boundary: how build output reaches the root helper and what the helper accepts

- Priority: mvp-blocker; owner decision needed: no
- Why: The library and the build output live in places the user can write. Any process running as the owner can:
- swap files,
- plant symlinks or ../ names,
- race between the helper's check and its copy.

Whatever the helper installs ends up in /boot, /usr and all 5 initrds. Also, /etc/default/grub.d/*.cfg is shell code that every update-grub run sources as root, including the runs apt does on kernel updates.
- Default: 1. The helper never opens a path supplied by the GUI and never touches $HOME. The request arrives on stdin: one JSON header (at most 64 KiB) followed by a tar stream of the built folders. No user data goes into argv.

2. Extract into a fresh root-owned staging folder next to the target:
- Accept only regular files and directories; reject symlinks, hardlinks, devices and FIFOs.
- Names must match ^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$, at most 2 levels deep (GRUB uses f/ and icons/).
- Limits: at most 2000 entries, 16 MiB per file, 64 MiB in total for GRUB and 30 MiB for Plymouth.
- Force root:root, files 0644, folders 0755, no xattrs.

3. Validate only the root-owned copy:
- Allowed extensions: GRUB txt, png, jpg, jpeg, pf2; Plymouth plymouth, script, png.
- PNG: check the signature and header; for GRUB require 8-bit RGB or RGBA, not interlaced, at most 4096 px. JPEG must be baseline. pf2 must start with the FILE/PFF2 header.
- Parse theme.txt against a list of allowed properties; every file it references must exist inside the folder.
- .plymouth must contain a literal 'ModuleName=two-step' or 'ModuleName=script' with no spaces, exactly one 'ImageDir=/usr/share/plymouth/themes/<id>', and a ScriptFile inside the folder. .script files at most 256 KiB.

4. The helper writes root config files itself, from typed values it has checked: the drop-in (folder name matched by regex, gfxmode from a fixed list, timeout an integer from 0 to 999, values quoted, then checked with sh -n) and the Theme= line. It never writes text supplied by the GUI.

5. Restores use only the root-side backups.

6. Tools are called by absolute path with a fixed environment (LC_ALL=C, fixed PATH).
- Notes: Facts behind the rules:
- The Plymouth hook reads ModuleName with a sed pattern anchored at '^ModuleName='. If the value is written with spaces, the hook prints 'W: plymouth module ... missing, skipping that theme', still exits 0, and the splash is silently not packed.
- The hook also greps every ImageDir line and copies that folder into the initrd as well.

The unprivileged importer must normalize imported themes before they can be applied:
- Dereference symlinks (the packaged spinner/watermark.png is one).
- Convert images to 8-bit RGBA.
- Drop scripts and docs. fallout/ ships install.sh, uninstall.sh and preview.sh, which the hook's 'cp -a' would pack into every initrd.
- Rewrite ImageDir and ScriptFile to the new folder.

An imported Splash.qml is code: previewing it runs it as the user, and it never runs as root.

#### Helper process and polkit contract

- Priority: mvp-blocker; owner decision needed: no
- Why: Round 6 fixes pkexec plus a policy file with kept authorization. It leaves open:
- the verbs the helper accepts,
- how many polkit actions there are,
- how auth_admin_keep behaves across several calls,
- where the helper is installed,
- what happens if the GUI dies mid-apply, or the installed helper is older than the GUI.

With heavy AI-assisted development, the GUI and the installed helper will drift apart.
- Default: One action, io.github.nomoruyi.personae.apply:
- allow_any=no, allow_inactive=no, allow_active=auth_admin_keep.
- exec.path=/usr/libexec/personae/personae-helper, owned root:root, mode 0755.
- Message: 'Personae needs administrator rights to change or check the boot look (GRUB menu, boot splash)'.

Do not split verbs into separate actions via the exec.argv1 annotation. pkexec 127 supports it, but polkit keeps authorization per action ID, so apply, verify and rollback would each prompt.

The GUI starts pkexec directly, with no shell in between, because pkexec authorizes its parent process. A shell in between would mean a prompt on every call.

Verbs, as a fixed list in the helper:
- --version: runs without root and prints the protocol version and app version.
- apply, restore <seq|original>, recover, verify, uninstall-cleanup: refuse unless euid is 0. They also accept sudo (SUDO_UID) for use from a TTY.

The GUI checks --version before prompting; on a protocol mismatch it says 'reinstall the helper'. Progress goes to stdout as JSON lines.

During changes the helper calls setsid() and ignores SIGPIPE, SIGHUP and SIGINT. If the GUI disappears, the helper finishes or rolls back, writes the journal, and only then exits.
- Notes: exec.path must never point into the repo or into $HOME. A helper the user can write lets any process running as the owner swap it and get root at the next authentication. Development uses the fake-root test harness; real applies always go through the installed helper.

#### Where system settings are written, and exact restore semantics for Original and the default.plymouth alternative

- Priority: mvp-blocker; owner decision needed: no
- Why: What is on the host today:
- /etc/default/grub is registered with ucf (by grub-pc) and already hand-edited: GRUB_THEME=hollow-grub, GRUB_GFXMODE, GRUB_RECORDFAIL_TIMEOUT=20.
- /etc/plymouth/plymouthd.conf is a dpkg conffile of the plymouth package and already modified (Theme=fallout).
- default.plymouth is in manual mode, pointing at fallout. In auto mode, kubuntu-logo (priority 150) would win.

INTERVIEW decides neither how the app writes these files nor how it restores them exactly.
- Default: GRUB:
- The helper generates one drop-in, /etc/default/grub.d/zz-personae.cfg. The folder is empty today and the name sorts last.
- It holds GRUB_THEME, GRUB_GFXMODE, GRUB_TIMEOUT and GRUB_RECORDFAIL_TIMEOUT.
- /etc/default/grub is never edited. Restoring Original means deleting the drop-in and running update-grub.
- The UI shows 'overrides /etc/default/grub'.

plymouthd.conf: change only [Daemon] Theme= and keep every other line.

Alternative:
- Register with 'update-alternatives --install ... default.plymouth <path> 10'. The low priority means app themes never win in auto mode.
- Then select with --set, which puts the group in manual mode. Package postinst scripts keep a manual choice.
- Restores re-apply the recorded mode (manual or auto) and value.
- Set the original value first, then --remove the personae paths. Removing the currently selected alternative would silently switch the group to auto mode, which means kubuntu-logo.

What Original captures:
- whether the drop-in exists, and its bytes;
- the bytes of plymouthd.conf;
- the alternative's mode, value and list of registered themes;
- ksplashrc [KSplash]: Engine and Theme (currently KSplashQML and Overload);
- a SHA-256 manifest of the themes the app did not install that Original points to (hollow-grub, fallout, Overload).

Before any restore, check that those themes still exist and are unchanged. If one is gone, offer a package-managed fallback for that target: no GRUB theme (text menu), Plymouth bgrt, or the KSplash theme org.kde.breeze.desktop.
- Question: OK to put all GRUB settings into one drop-in file and leave /etc/default/grub untouched? Its GRUB_THEME line stays but is overridden.
- Notes: - A drop-in with a syntax error would break every later update-grub run, including the ones apt does on kernel updates. That is why the helper generates it from typed values and checks it with sh -n.
- If the owner ever answers 'install the package maintainer's version' to a dpkg prompt about plymouthd.conf, the Theme= line disappears. The health check flags this as 'inputs disagree'.
- The initramfs hook always packs the details and text themes, so plymouthd already has an automatic fallback inside the initrd.

#### Safe file writes, and the distro tools that fail silently

- Priority: mvp; owner decision needed: no
- Why: Inspection of the installed tools found traps that a naive apply would hit:
- grub-mkconfig validates grub.cfg.new and then rewrites the live file in place ('cat grub.cfg.new > grub.cfg') with no fsync. A crash at that moment leaves a truncated grub.cfg and a grub> prompt at boot.
- GRUB's btrfs reader ignores the btrfs log tree, so files that were only fsync'd and not yet committed can be invisible to it after a crash.
- 'update-initramfs -u -k all' runs newest-first under 'set -eu' and stops at the first failing kernel, so newer initrds get rebuilt and older ones don't.
- 00_header silently leaves the theme out, still exiting 0, if the GRUB_THEME file is missing or gfxterm is not the terminal.
- The Plymouth hook skips a theme whose plugin module is missing with nothing but a 'W:' line, also exiting 0.
- Default: 1. grub.cfg: run 'grub-mkconfig -o /boot/grub/.grub.cfg.personae', fsync the result, rename() it over /boot/grub/grub.cfg (mode 0600), and keep the previous grub.cfg in the entry backup. If /usr/sbin/update-grub ever becomes more than its current 3-line wrapper, fall back to plain update-grub followed by 'sync -f'.
2. End every transaction with syncfs on /. On this machine / also holds /boot, /etc, /usr and /var/lib.
3. Rebuild kernels one at a time: 'update-initramfs -u -k <v>' for each version from 'linux-version list' that has a /boot/initrd.img-<v> (the same enumeration update-initramfs uses), recording each success. Use the dracut equivalent if dracut is detected.
4. Treat these as failures: no 'Found theme: <path>' on grub-mkconfig's stderr, any 'W: plymouth' line in update-initramfs output, or a non-zero exit code.
5. Stage new folders next to their target before renaming, so the rename can never fail with EXDEV. Write configs as: temp file, fsync file, rename, fsync folder.
6. Skip the initramfs rebuild entirely when the Plymouth slot and both Plymouth inputs are unchanged and already verified.
- Notes: update-initramfs itself is atomic per file: it writes .new, then 'mv -f', then 'sync -f'. dracut writes .tmp, then 'mv -f', then sync, and skips fsfreeze here because /boot is on the root filesystem. If update_initramfs=no is ever set in update-initramfs.conf, '-u' silently does nothing; the initrd fingerprint check in verification catches it.

#### Concurrency and locking

- Priority: mvp; owner decision needed: no
- Why: Neither update-initramfs nor grub-mkconfig takes a lock. Two runs in parallel write the same initrd.img-<v>.new and can move a corrupt initrd into place. Other things that run these tools on this machine: apt, Discover via PackageKit 1.3.4, unattended-upgrades (installed, currently disabled) and the kernel postinst hooks. Two GUI instances, or two logged-in sessions, could also apply or write the library at the same time.
- Default: GUI:
- Single instance via KDBusService::Unique; a second launch raises the existing window.
- A QLockFile in the library guards writes.

Helper:
- Takes a non-blocking flock on /var/lib/personae/lock. If it is held, it reports 'another Personae operation is running'.
- Before the first change, takes /var/lib/dpkg/lock-frontend and then /var/lib/dpkg/lock, with the same fcntl protocol apt uses. It holds both until verification ends, so apt, PackageKit and dpkg cannot start kernel hooks or the update-initramfs trigger mid-apply.
- If dpkg is busy, it names the process holding the lock and shows 'waiting for the package manager...' with a Cancel button. It never breaks a lock.

None of the commands the helper runs (update-alternatives, update-initramfs, grub-mkconfig, lsinitramfs) takes the dpkg lock itself, so holding it cannot deadlock.
- Notes: kde-config-plymouth (System Settings > Boot Splash) is installed. Its strings show that its helper runs 'update-alternatives --set' and then 'update-initramfs -u' without any lock, so it cannot be locked out. Detecting outside changes, plus a hint in the docs, covers it.

#### Checking the result after an apply, and detecting outside changes without root

- Priority: mvp; owner decision needed: no
- Why: Round 6 promises that outside changes are detected from the real system state. The lens asks for proof that the theme is inside every initrd and that grub.cfg references it. But grub.cfg and the initrds are mode 0600, so the GUI cannot read them without a prompt on every start. And because 00_header and the Plymouth hook can drop a theme while exiting 0, a successful exit code proves nothing.
- Default: The helper verifies at the end of every apply or restore, and on 'Verify now'.

GRUB: grub.cfg must contain:
- 'set theme=($root)/@/boot/grub/themes/<id>/theme.txt' (note the btrfs subvolume prefix);
- one 'loadfont ($root)/@/boot/grub/themes/<id>/...pf2' line per font;
- 'insmod png' or 'insmod jpeg' matching the image types at the theme folder's root;
- the expected 'set gfxmode=' and timeout.

Plymouth, for each kernel: the initrd listing ('lsinitramfs -l', or lsinitrd under dracut) must contain every theme file, the <module>.so plugin, the link 'usr/share/plymouth/themes/default.plymouth -> .../<id>/<id>.plymouth' and etc/plymouth/plymouthd.conf. Under dracut, also read its Theme= with 'lsinitrd -f'.

KSplash, checked in the GUI: ksplashrc Engine and Theme have the expected values, and the package loads (see the Splash.qml gap).

Results go into state.json, readable by everyone, together with a fingerprint (size and mtime) per initrd.

When the GUI starts, it checks for outside changes without a prompt, using only files everyone can read:
- hash of the drop-in;
- Theme= in plymouthd.conf;
- 'update-alternatives --query default.plymouth' (mode and value);
- hashes of the installed theme folders;
- ksplashrc;
- initrd fingerprints, compared for equality and never as 'newer than';
- the list of kernels.

A changed initrd fingerprint alone means 'unverified', not 'differs', because kernel and plymouth updates rebuild initrds legitimately.
- Notes: The GUI shows a status per target and per kernel, such as 'verified <date>', 'unverified (kernel 7.0.0-45 is new)', or 'differs: plymouthd.conf says fallout, the alternative says personae-...'. Verification only proves what is on disk. Whether the firmware actually boots this GRUB is out of scope: BootOrder is ubuntu, then Windows Boot Manager, then Kubuntu.

#### Health checks after new kernels, package upgrades, generator switches, other tools, rollbacks and reinstalls

- Priority: mvp; owner decision needed: no
- Why: The look can break or drift while the app is closed, and each of these can happen on this machine:
- new kernels;
- grub, plymouth or Plasma upgrades;
- a release upgrade that switches the initramfs generator to dracut;
- the Plymouth KCM, which changes the alternative but not plymouthd.conf and rebuilds only the newest initrd (likely how today's broken splashes came about);
- a dpkg conffile prompt;
- a Timeshift restore (btrfs mode snapshots @ hourly, daily, weekly, monthly and at boot, and excludes /home), which rolls back the system but not the library or ~/.config;
- a reinstall, which wipes /var/lib/personae while /home survives;
- Secure Boot being turned on later.
- Default: On every GUI start and before every apply, run cheap checks that need no root.

Record and compare:
- package versions: grub-common, grub-efi-amd64-signed, plymouth, initramfs-tools, dracut-core, plasma-workspace, Qt;
- the detected initramfs generator;
- the list of kernels;
- /etc/machine-id;
- the SecureBoot efivar (readable by everyone; currently 0).

When something changed, re-run the probes and show a status card with [Verify now]. The probes:
- 00_header's pf2 and insmod logic is unchanged;
- the plugin .so named by ModuleName exists;
- the initramfs hook still picks the theme via 'update-alternatives --query';
- Splash.qml still loads.

Specific cases:
- The two Plymouth inputs disagree, or only the newest initrd changed: offer [Adopt current] or [Re-apply], as decided in round 6.
- The system history's counter is lower than the GUI last saw: show 'the system was rolled back (e.g. Timeshift) to entry #N; the Plasma splash is still from #M'.
- The machine-id is new: treat it as a fresh install. Capture a new Original at the next apply; old system entries cannot be restored.
- Secure Boot is on: warn that GRUB's shim-lock policy refuses .pf2 fonts that are not built into GRUB.

No background service and no apt hook. Checking at app start is enough for a look the owner changes rarely.
- Question: Is a check at app start enough, or do you want a notification right after apt upgrades? That would need an apt hook on the host.
- Notes: New kernels keep the look automatically: /etc/kernel/postinst.d/initramfs-tools packs the theme the alternative points at, and zz-update-grub reads the drop-in. So a new kernel is 'unverified', not broken.

The Secure Boot font refusal ('prohibited by secure boot policy') has been reported since GRUB's 2022 font hardening. Sources: https://github.com/Lxtharia/minegrub-theme/issues/45 and https://lists.nongnu.org/archive/html/help-grub/2023-02/msg00001.html. Windows is on the ESP, so turning on Secure Boot later is plausible. That part is priority 'later'.

#### Generated Splash.qml must survive Qt and Plasma upgrades

- Priority: mvp; owner decision needed: no
- Why: KPackage falls back to the default look-and-feel package only when a file is missing. A Splash.qml that fails to compile gives a blank splash until the desktop is ready. Breeze's own Splash.qml imports org.kde.kirigami and reacts only to stage 2 and stage 5. A generated splash that stays hidden until a certain stage would go black if Plasma renumbered its stages.
- Default: Rules for the generator:
- Import only the versionless QtQuick module, plus QtMultimedia only when a Video element is really used.
- Never import org.kde.plasma.* or Kirigami.
- Declare 'property int stage', but make the splash visible without any stage change: fade in on Component.onCompleted, and use stages only for optional extras.
- Prefer AnimatedImage with animated WebP, as Overload does. It needs qt6-image-formats-plugins, which is installed.

The GUI validates at build time, at apply time, and after any Qt or Plasma version change:
- Load the installed Splash.qml in a QQmlComponent offscreen and fail on any error.
- Check that QImageReader supports webp, and that a multimedia backend exists if Video is used.

On failure: notify, and offer one click to revert the splash to the previous entry or to Breeze. That runs as the user and needs no root.
- Notes: A broken splash cannot stop the machine from booting or Plasma from starting, so this is MVP rather than a blocker. 'ksplashqml --test --window <path>' remains the manual preview.

#### Disk space check before applying, and size limits

- Priority: mvp; owner decision needed: no
- Why: INTERVIEW sets no policy for disk space or theme size, and these facts make one necessary:
- update-initramfs writes a ~94 MB .new file next to each old initrd.
- btrfs can run out of metadata space (ENOSPC) before df reaches 0.
- The Plymouth hook copies the whole theme folder into each of the 5 initrds.
- GRUB reads each initrd through its slow btrfs driver at boot.
- Timeshift snapshots of @ keep the old initrds, so each apply leaves ~470 MB allocated until the snapshots rotate.
- Default: Before applying, check free space with statvfs on / (which holds /boot, /usr and /var/lib here). Require at least the larger of 1 GiB and (2 x the largest initrd + the bytes of the new themes). Otherwise refuse before touching anything.

Check free space on the library's filesystem before rendering videos.

Theme size limits:
- Plymouth: warn above 5 MB, refuse above 30 MB. Existing themes are 0.4 to 2.7 MB.
- GRUB: warn above 20 MB in total or a background above 8 MB, because GRUB decodes images in software.

The dry run says: 'rewrites 5 initrds (~470 MB); Timeshift snapshots keep the old ones until they rotate'.
- Notes: Today / has 34 GB free and /home has 244 GB free (84% used). The limits are constants in the helper, not settings, because a setting would itself become input the helper has to trust.

#### Packaging, GUI/helper version mismatch, and clean uninstall

- Priority: mvp; owner decision needed: no
- Why: The helper and the polkit policy must be root-owned system files, but INTERVIEW says nothing about how the app is installed, updated or removed. 'Generated packages keep working after uninstall' needs a defined uninstall path. Alternatives registrations and the drop-in should not outlive what the owner wants.
- Default: A repo script builds a local .deb named personae containing:
- /usr/bin/personae;
- /usr/libexec/personae/personae-helper;
- /usr/share/polkit-1/actions/io.github.nomoruyi.personae.policy;
- the .desktop file, icon and metainfo.

/var/lib/personae is created by the helper at the first apply, not by the package. During development, the GUI may run from the build folder against the installed helper; the --version handshake catches mismatches.

Removal:
- 'apt remove' keeps the applied looks, the drop-in, the alternatives and the history.
- 'apt purge' also deletes /var/lib/personae, including the Original backup, and its postrm prints a notice. It never touches applied system config.

An in-app action, 'Restore Original and remove Personae files':
1. Restore Original.
2. Re-set the original alternative, then remove the alternatives Personae registered.
3. Delete the drop-in.
4. Delete every personae-* folder and the user's Personae KSplash packages.
5. Rebuild the initrds and regenerate grub.cfg.
6. Tell the owner to run 'sudo apt purge personae'.

The library and user state are never deleted automatically. The UI lists them: ~/.local/share/personae, ~/.config/personaerc, ~/.local/state/personae, ~/.cache/personae.
- Question: Install via a locally built .deb (clean apt remove and purge), or via 'cmake --install' into /usr/local?
- Notes: polkitd 127 also reads /usr/local/share/polkit-1/actions, so installing to /usr/local would work. A .deb is preferred because dpkg tracks every file and removes them cleanly.

#### Library format versions, migrations and safe writes

- Priority: mvp; owner decision needed: no
- Why: The plain-file library on /home outlives app versions and reinstalls. Because a snapshot is taken on every save, old snapshots will be in old formats, and the owner may downgrade a development build. Without a format version from day one, later migrations become guesswork.
- Default: Versioning:
- library.json plus a formatVersion integer in every profile, bundle and snapshot file.
- Older format on open: copy the JSON files (not the assets) to backups/pre-v<N>-<date>/, migrate one version at a time (v1 to v2 to ...), and write atomically with QSaveFile.
- Newer format: open read-only with a banner.
- Restoring a snapshot runs it through the same migrations.
- The system history in /var/lib/personae and the helper protocol have their own version numbers. The helper reads every older entry format.
- Export archives carry a manifest.json with the format version; archives from a newer version are refused.

Safe writes and damaged files:
- Before saving, compare the file's hash or mtime and offer reload or overwrite if it changed on disk; plain files invite hand edits.
- A file that cannot be parsed marks its profile as broken. The file is kept, never deleted, and everything else still loads.
- If the configured library path is missing (for example /extra is not mounted), show 'library not found' and never silently create an empty library.
- The repo keeps a v1 sample library for golden tests of the migrations.

#### Library storage growth and backup

- Priority: mvp; owner decision needed: no
- Why: Round 4 takes a snapshot on every save and keeps videos in the asset library. It defines no retention, no split between library and cache, and no backup. Timeshift does not cover /home here (include_btrfs_home=false, and /home is a separate filesystem), so the library is the only copy of the owner's designs.
- Default: Storage:
- Assets are stored under their content hash and never change: assets/<sha256>.<ext>. A snapshot is then only a few KB of JSON.
- Derived renders (per-resolution PNGs, animated WebP, .pf2) live in ~/.cache/personae/, keyed by input hash. They can be rebuilt, so they are left out of backups and exports; an export rebuilds the packages.

Retention:
- Keep the last 50 snapshots per profile, plus every snapshot that a history entry or a bundle references.
- A manual 'Clean up library' with a size preview deletes assets nothing references.

Backup:
- 'Export library' and 'Import library' as a single .tar.zst file.
- Settings show the library path and size, plus a hint to include it in the owner's own backups. No built-in backup scheduler.
- Notes: Storing assets by hash and separating library from cache must be in the first data model. The whole-library export and import can come later. After a reinstall, /home survives but /var/lib/personae does not: profiles and bundles rebuild fine, but old system history entries cannot be restored.

#### Logging, diagnostics and recovery without the GUI

- Priority: mvp; owner decision needed: no
- Why: Logging every privileged action is decided, but not where the logs go, their format, how long they are kept, or how the owner sees them. A Qt or Plasma upgrade could also break the GUI, and the owner still needs a way back to a known-good state.
- Default: Logs:
- The helper logs every privileged step and command to journald under the identifier personae-helper, with structured fields SEQ, STEP, CMD and EXIT. View them with 'journalctl -t personae-helper'.
- It also writes the full stdout and stderr to history/<seq>/log.txt, kept as long as the entry.
- The GUI logs to $XDG_STATE_HOME/personae/personae.log, rotated as 3 files of 1 MB.
- A 'Copy diagnostics' button collects versions, the detected generator, health-check results and the last entry's log.

Recovery without the GUI:
- 'sudo personae-helper restore original|<seq>' and 'sudo personae-helper recover' work from a TTY.
- After each apply, the helper rewrites /var/lib/personae/RECOVERY.txt with the exact manual commands to get back to Original without Personae: remove the drop-in, run update-grub, run 'update-alternatives --set default.plymouth <orig>', set Theme=<orig> in plymouthd.conf, run 'update-initramfs -u -k all', and the user's kwriteconfig6 command for ksplashrc.
- Notes: The KSplash part is restored with the user's own command because the helper never touches $HOME. From a live USB, /boot is inside subvolume @, so mount with subvol=@ before chrooting.

### Settings, desktop integration and polish

13 gaps. Two block the spec. (1) The round-2 timeout rule only holds for this btrfs /boot. /etc/grub.d/00_header (verified) forces timeout_style=menu and GRUB_RECORDFAIL_TIMEOUT on EFI when GRUB cannot write /boot. On a writable /boot, which the reinstall may produce, the existing GRUB_TIMEOUT_STYLE=hidden hides the menu, and setting GRUB_RECORDFAIL_TIMEOUT to 0 removes the menu GRUB shows after a failed boot. (2) pkexec with cached authorization needs a root-owned helper at a fixed system path, so the spec must say how Personae is installed and removed (proposed: a locally built .deb).

Settings stay minimal:
- one app preference: library location, in KConfig
- two machine-wide boot options (timeout, resolution order), each with its own dry run and history entry
- everything else fixed and shown read-only

Verified facts that shape drift detection and integration:
- The Boot Splash KCM rebuilds only the newest initrd and has no dracut path.
- This machine is already in a mixed state: plymouthd.conf and default.plymouth changed on 2026-09-25, but the initrds of 7.0.0-27 and -29 date from 2026-09-22.
- Applying a Global Theme reverts the user's ksplashrc Theme key via ~/.config/kdedefaults.
- Splash-only packages never appear in Global Themes.
- The desktop language is English with German formats.
- logind lets an active session block shutdown during an apply without authentication.

#### GRUB timeout: which keys to write depends on /boot (correction to round 2)

- Priority: mvp-blocker; owner decision needed: no
- Why: Verified in /etc/grub.d/00_header: when GRUB cannot write /boot/grub (btrfs, zfs, LVM, RAID) and the firmware is EFI, Ubuntu forces timeout_style=menu and timeout=GRUB_RECORDFAIL_TIMEOUT. It then ignores GRUB_TIMEOUT and GRUB_TIMEOUT_STYLE. Today those are 10 s and hidden, with GRUB_RECORDFAIL_TIMEOUT=20, so the menu shows for 20 s. The planned reinstall may produce a /boot that GRUB can write. There, the GRUB_TIMEOUT_STYLE=hidden in /etc/default/grub hides the menu for any N, so 'N seconds of visible menu' fails. GRUB_RECORDFAIL_TIMEOUT is then the only thing that shows the menu after a failed boot. The round-2 rule 'write both keys' would set it to 0 exactly when the owner picks the riskiest value.
- Default: Detect /boot read-only the way grub-mkconfig does: findmnt FSTYPE of /boot/grub, lsblk for LVM/RAID, /sys/firmware/efi. If GRUB cannot write /boot: N>0 sets GRUB_RECORDFAIL_TIMEOUT=N and GRUB_TIMEOUT=N. 0 sets both to 0, with the hard warning 'no menu ever; if the default entry stops booting you need a live USB'. If GRUB can write /boot: N>0 sets GRUB_TIMEOUT=N and GRUB_TIMEOUT_STYLE=menu. 0 sets GRUB_TIMEOUT=0 and GRUB_TIMEOUT_STYLE=hidden, and leaves GRUB_RECORDFAIL_TIMEOUT untouched, so a failed boot still shows the menu (softer warning). The setting shows the effective result in one sentence ('Menu visible for 20 s: /boot is on btrfs, so GRUB_RECORDFAIL_TIMEOUT decides'), with the raw keys underneath. Original records all four keys, including absent ones. With an effective 0 s, the bundle view marks the GRUB slot 'never visible at boot'.
- Question: With 0 s on a writable /boot, keep GRUB's failed-boot menu (recordfail timeout untouched)?
- Notes: No round covers GRUB_TIMEOUT_STYLE. The app has to manage it for the owner's timeout semantics to survive the reinstall. CLAUDE.md already says to detect, never assume.

#### Installing Personae itself: footprint, app ID, root-owned helper

- Priority: mvp-blocker; owner decision needed: no
- Why: Round 6 chose pkexec with an installed policy and cached authorization (auth_admin_keep). pkexec matches the policy by the helper's absolute path, so the helper and the policy must be installed system-wide. A helper in ~/.local, a venv or the repo checkout lets any process running as the owner replace it. That process then gets root behind Personae's real prompt, or with no prompt while the authorization is cached. INTERVIEW says nothing about how the app is installed and removed, and 'change as little on the host as possible' needs that footprint written down.
- Default: Ship one locally built .deb named 'personae' (package and binary name are free in the archive, checked with apt-cache). `dpkg -L personae` lists the footprint. `apt remove personae` removes it, while generated themes, the library and the history keep working. Contents, all named after io.github.nomoruyi.personae:
- /usr/bin/personae
- /usr/libexec/personae/personae-helper: root:root 0755, never accepts a root-override flag.
- /usr/share/polkit-1/actions/io.github.nomoruyi.personae.policy: org.freedesktop.policykit.exec.path annotation, icon_name set to the app icon, vendor Personae, allow_active auth_admin_keep, allow_any and allow_inactive no.
- /usr/share/applications/io.github.nomoruyi.personae.desktop: Categories=Qt;KDE;Settings;DesktopSettings; Keywords=boot;splash;grub;plymouth;ksplash;theme; SingleMainWindow=true; no MimeType in v1.
- A hicolor scalable icon: placeholder mask, readable on the Breeze Dark panel.
The GUI calls QGuiApplication::setDesktopFileName with the app ID, so the Wayland app_id matches the .desktop file (taskbar icon, notifications, job progress). GUI and helper exchange a protocol version and refuse to apply on a mismatch. When no helper is installed (running from the build tree), Apply is disabled with a banner instead of failing inside pkexec. AppStream metainfo comes later, and only if the app is published (appstreamcli is installed for validation).
- Question: Ship as a locally built .deb rather than `cmake --install` into /usr/local?
- Notes: Not a wrong decision: it is a prerequisite that round 6 implies, and the architecture gate needs it. polkitd 127 here also reads /usr/local/share/polkit-1/actions, so a /usr/local install works technically, but its files are untracked and uninstalling is manual.

#### Settings inventory: scope, storage and lifetime of each value

- Priority: mvp; owner decision needed: no
- Why: INTERVIEW fixes several values: a configurable library path, three resolutions, 1440p, then 1080p, then 720p, all kernels, and 10 entries plus Original. It does not say which ones can be changed, at which scope, or where they are stored. Timeout and resolution order belong to the machine and its firmware, not to a look. Stored per design or per bundle, switching looks would silently change boot behaviour. The reinstall keeps /home (library, ~/.config) and wipes /, so some stores outlive the system and others die with it.
- Default: - App preference (KConfig, ~/.config/personaerc): library location only. Changing it offers 'Move library here' or 'Use the library found there' and never silently starts an empty library. Local filesystems only, because the root helper cannot read another user's FUSE or sshfs mount.
- UI state (KF6 state config, ~/.local/state/personaestaterc): window geometry, last page, last bundle, preview resolution, zoom.
- Machine boot options on the System page. Each change goes through dry run, helper and its own history entry. Values are read back from the system files every time, never cached in personaerc. They are the GRUB menu timeout (see the timeout gap) and, under Advanced, the resolution order. That order defaults to 2560x1440,1920x1080,1280x720,auto: with auto, a new monitor or GPU without those modes still gets the scaled graphical theme instead of the text console. Advanced also holds the actions 'Reveal built package' and 'Show privileged log'.
- Per bundle: slot assignments only. Per design: look properties only.
- Fixed and shown read-only, deliberately not settings: initramfs scope (all kernels); system history of 10 entries plus Original; the detected toolchain, with no override (an ambiguous or missing generator blocks Plymouth applies and says why); dry run, validation and backup; kernel cmdline and plymouthd.conf daemon keys.
- Design snapshots: keep all. 'Clean up library…' shows reclaimable space and prunes snapshots older than a chosen date, then moves unreferenced assets to the trash. Snapshots referenced by a bundle or a system-history entry are protected.
- Every system-history entry records an app-specific hash of /etc/machine-id; machine-id(5) says not to store it raw. After the reinstall, entries with a different hash are listed read-only as 'previous installation' and can never be restored. A Personae splash that survives in ~/.config shows 'applied, no history on this system' with Adopt.
- 'Configure Personae…' (Ctrl+Shift+,) opens the Storage section of the System page; there is no separate settings dialog.
- Question: Keep timeout and resolution order machine-wide instead of per design or per bundle?
- Notes: The trailing 'auto' extends round 5's 1440p, 1080p, 720p order with a last-resort entry. Drop it if the owner prefers text mode on unknown screens.

#### Coexistence with System Settings' Boot Splash, Splash Screen and Global Theme modules

- Priority: mvp; owner decision needed: no
- Why: Round 6 says drift is detected and never overwritten, but not who the other writers are or how to read what they wrote. Verified:
- Boot Splash KCM (kde-config-plymouth 6.6.4): writes plymouthd.conf Theme=, registers the theme at priority 100 if needed and runs --set on default.plymouth, then runs `update-initramfs -u` (newest kernel only) or mkinitcpio. It has no dracut branch, so after the reinstall it would change the config and the alternative and rebuild nothing.
- This machine is already mixed: plymouthd.conf and the default.plymouth link changed on 2026-09-25, but the initrds of 7.0.0-27 and -29 date from 2026-09-22, so those two kernels still boot the previous splash.
- Splash Screen KCM (plasma-desktop): writes Theme and Engine in ~/.config/ksplashrc, including None.
- Global Theme KCM and lookandfeeltool: applying a theme that has a splash writes ~/.config/kdedefaults/ksplashrc and reverts the user's Theme key, so Personae's splash disappears. Today kdedefaults says org.kde.breezedark.desktop, the user file says Theme=Overload, and Engine exists only in kdedefaults.
- Default: Read effective values the way the consumers do:
- KSplash through KConfig: XDG_CONFIG_DIRS includes ~/.config/kdedefaults, and a missing Engine means KSplashQML.
- GRUB keys by sourcing /etc/default/grub, then /etc/default/grub.d/*.cfg in order.
- Plymouth from plymouthd.conf plus the alternative.
Use file mtimes to spot half-finished changes; stat works on the 0600 grub.cfg and on the initrds. GRUB config newer than grub.cfg means 'update-grub not run'. Plymouth config newer than some initrds means 'kernels X and Y boot an older splash'. Check on start, on window activation and after each apply; no daemon, tray icon or autostart. A banner per target names the likely writer (using the patterns above) and offers Adopt or Re-apply. For a mixed Plymouth state, Adopt also rebuilds the stale initrds (privileged, with dry run). For KSplash, Original and restore record whether the user key existed, and restore by writing or deleting that key, never by writing the effective value. The same check flags imported themes whose source has changed and offers Re-import. The first start shows the current mixed state instead of hiding it. Non-goal: a copy of 'Get New…'. The owner downloads with the KCMs, then imports.
- Notes: Splash-only packages (no contents/defaults and no contents/layouts) are not listed in Global Themes, so Personae splashes cannot be applied from there. The Boot Splash KCM offers Delete only for themes it installed itself, so its UI cannot remove personae-* themes. Sources: invent.kde.org plasma/plymouth-kcm src/helper.cpp and src/kcm.cpp; plasma-workspace kcms/lookandfeel/kcm.cpp and libklookandfeel/klookandfeelmanager.cpp.

#### Generated packages should look native in System Settings

- Priority: mvp; owner decision needed: no
- Why: Personae's output shows up in two KCMs. The Boot Splash KCM scans /usr/share/plymouth/themes and shows preview.png plus Name and Description from the .plymouth file. The Splash Screen KCM lists packages that have contents/splash, shows contents/previews/splash.png, and its Test button runs ksplashqml --test. Without those files the packages appear as blank tiles. Since output must keep working after the app is uninstalled, System Settings may end up as the only way to switch.
- Default: Generators always write:
- Plymouth: preview.png (from the 1080p render) and [Plymouth Theme] Name=<display name>, Description=<design description>.
- KSplash: contents/previews/splash.png and a metadata.json with KPlugin Id io.github.nomoruyi.personae.<slug>, Name, Description, Authors nomoruyi and KPackageStructure Plasma/LookAndFeel. Never contents/defaults or contents/layouts, which keeps them out of Global Themes.
Golden-file tests assert that these files exist and that defaults/layouts do not. The importer lists personae-* and app-prefixed packages as 'Installed by Personae', not as import sources.

#### Long applies: progress, notifications, quit and shutdown protection

- Priority: mvp; owner decision needed: no
- Why: A bundle apply takes about a minute: grub-mkconfig plus about 8 s per initrd for 5 kernels. The helper runs as root via pkexec, so the GUI cannot send it signals. If the GUI closes or crashes while a QProcess still runs the helper, the QProcess destructor blocks for up to 30 s and then closes the pipe. The helper then dies of SIGPIPE at its next progress write, between 'config written' and 'initrds rebuilt', with no rollback. Shutting down during the rebuild has the same effect.
- Default: - Show a step list in the window (Validate, Back up, Install files, Update GRUB config, Rebuild initramfs k/n with the kernel version, Verify) with an expandable raw log.
- The helper rebuilds one installed kernel at a time, with the same loop for initramfs-tools and dracut. That gives exact k/n progress and per-kernel errors.
- Run the apply as a KJob on the Plasma job tracker (KUiServerV2JobTracker). That gives progress in the notification area and on the taskbar button, and a finished or failed message when the window is not active. Add a two-event notifyrc only if the finished popup does not appear.
- Cancel works until the first system write; after that it is disabled, with a tooltip saying why.
- Closing the window and Ctrl+Q are refused during an apply, with an inline message.
- Hold a logind block inhibitor 'shutdown:sleep' for the duration. Active sessions get it without authentication (checked in org.freedesktop.login1.policy).
- The helper ignores SIGPIPE, SIGHUP and SIGINT, logs to its own file, and finishes or rolls back without the GUI. The next start reports 'the last apply did not finish' and its outcome.

#### Sandbox mode: run the whole GUI against a fake system root (CLI-only advanced option)

- Priority: mvp; owner decision needed: no
- Why: CLAUDE.md forbids Claude any write outside the repo. The apply logic is tested against fake roots, but the GUI also reads /boot, /etc and /usr/share/plymouth, writes ~/.config/ksplashrc and ~/.local/share/personae, and calls pkexec. Without a sandbox switch, the GUI cannot be run, screenshotted or tested end to end during AI-assisted development without touching the real system. The owner also gets a safe way to try the app.
- Default: `personae --sandbox <dir>`:
- system root at <dir>/root, seeded from a repo fixture
- HOME, XDG_{CONFIG,DATA,CACHE,STATE}_HOME and TMPDIR under <dir>
- stub grub-mkconfig, update-initramfs, dracut and update-alternatives that record their calls
- the build-tree helper, run unprivileged without pkexec
- a separate D-Bus name, so it can run next to a real instance
- a permanent 'Sandbox: <dir>' banner and window title
It refuses '/' and requires a marker file in <dir>. The installed helper rejects any root override when running as root. Works with QT_QPA_PLATFORM=offscreen for screenshots.

#### Confirmations: undo instead of dialogs, fixed gates for privileged steps

- Priority: mvp; owner decision needed: no
- Why: The owner strongly wants undo. INTERVIEW fixes a dry run before every apply and a hard 0 s warning, but nothing covers library deletions, items in use, Adopt, Restore Original or unsaved edits. Dialogs on everything get clicked through; KDE practice is to confirm only what cannot be undone.
- Default: - Library deletions (design, bundle, asset, import) move the folder to the system trash (plain files, restorable from Dolphin) and show 'Deleted X, Undo'. No dialog.
- Items in use cannot be deleted. Show where they are used (bundles, designs, installed themes, history entries) and offer Duplicate instead.
- Re-import takes a snapshot first; no dialog.
- Privileged actions: the dry-run review is the one confirmation. It cannot be turned off and has no 'Don't ask again'; polkit prompts after it.
- Extra gates inside the review: an effective 0 s timeout requires ticking 'I can recover with a live USB' before Apply is enabled. Restore Original lists every Personae change it reverts. Adopt states what changes: usually nothing, or a rebuild of stale initrds for a mixed Plymouth state.
- Unsaved editor changes on close or switch: Save/Discard/Cancel, plus a crash-recovery draft every 30 s that is not a snapshot.
- Notes: No explicit current HIG rule was found. KDE developer discussions favour undo over confirmation: https://bugs.kde.org/show_bug.cgi?id=290352 and https://mail.kde.org/pipermail/plasma-devel/2015-April/040451.html

#### Single instance and concurrent writers

- Priority: mvp; owner decision needed: no
- Why: A second launch, a second login, or a sandbox pointed at the real library could run two applies at once, or write one library from two processes. The history and the all-or-nothing rollback assume a single writer.
- Default: - KDBusService Unique on io.github.nomoruyi.personae: a second launch raises the running window (matches SingleMainWindow=true).
- The helper takes an exclusive flock on a /run/lock file for each privileged operation and refuses a concurrent one with 'another apply is running'.
- The open library holds a QLockFile; a second process opens it read-only with a banner.
- v1 command line: --sandbox and --version only. No headless apply; recovery from a text console goes through the helper (see the help gap).

#### Accessibility and visual baseline for designers and the review

- Priority: mvp; owner decision needed: no
- Why: The designers are canvases. Drag-only placement is inaccessible and imprecise; GRUB positions are px, % or %+px. The dry-run diff is the main safety screen and must not rely on red and green alone. The GRUB menu gets read under stress (choosing recovery mode), so unreadable designed text is a real failure. KDE HIG asks for keyboard-only operation, a distinct screen-reader name per element, instant transitions when animations are disabled globally, and testing with another color scheme.
- Default: - Every canvas element is editable from a property panel with numeric x/y/w/h in px or %. Arrow keys nudge 1 px, Shift+arrow 10 px, Tab cycles elements.
- Every control and canvas item has an accessible name and role, e.g. 'Boot menu, 40% wide, centred'.
- Diffs carry +/- markers and labels as well as color.
- The validator warns, but does not block, when designed text has less than 4.5:1 contrast against its background: menu items, selected item vs selection fill, countdown, KSplash text. Contrast is computed per derived resolution from the master image under the text box.
- Animated previews autoplay unless kdeglobals [KDE] AnimationDurationFactor is 0 (the owner has 0.35, so autoplay is on), with a visible play/pause control.
- App chrome follows the Plasma color scheme (Breeze Dark today), with no in-app theme switch. Previews always show the true boot colours on a neutral frame, never tinted by the app palette.
- No reduced-motion variant of generated splashes in v1.
- Notes: https://develop.kde.org/hig/accessibility/

#### Keyboard shortcuts and which undo Ctrl+Z means

- Priority: mvp; owner decision needed: no
- Why: There are three undo layers: unsaved editor changes, saved design snapshots and system history. A shortcut that reaches the wrong one either loses work or starts a privileged rollback.
- Default: Ctrl+Z and Ctrl+Shift+Z act only on unsaved changes in the open design, never on library or system state. Library deletions use the inline Undo; snapshots and system history open from their History pages. Use KDE-standard keys where they exist, rebindable in the standard shortcuts dialog:
- Ctrl+S save (creates a snapshot), Ctrl+N new design, Ctrl+I import, Ctrl+E export
- Ctrl+1/2/3 GRUB/Plymouth/KSplash
- Ctrl+0 actual pixels, Ctrl+= and Ctrl+- zoom, Space play/pause preview
- F5 re-check system state
- Ctrl+Return 'Review & Apply…', which opens the dry run; no shortcut ever runs a privileged action
- Ctrl+Shift+, configure, F1 help, Ctrl+Q quit (refused during an apply)

#### In-app help and recovery without the GUI

- Priority: mvp; owner decision needed: no
- Why: History-based undo only works while the GUI runs. If the default kernel stops booting, or a Qt/KF upgrade breaks the app, the owner needs a way back without the app, and it has to work offline.
- Default: - Inline help at each decision point: effective timeout, the 0 s gate, Adopt vs Re-apply, Restore Original, Plymouth live text falling back to the Ubuntu font, 'slot not visible at boot'. Self-contained, no web links.
- Help > Recovery, available offline. It gives the exact commands to list and restore history from a text console through the helper (pkexec /usr/libexec/personae/personae-helper restore original; pkexec brings its own text agent on a TTY). It also gives the manual live-USB steps: which files, chroot, update-grub, initramfs rebuild for the detected generator. The apply path writes the same text next to its backups.
- Help > About shows app and helper versions plus the detected toolchain, with 'Copy system report' for issues and AI sessions.
- Non-goals: a DocBook handbook or KHelpCenter entry, and online docs (the repo is private).

#### Language: English UI; boot-screen strings follow the system locale

- Priority: mvp; owner decision needed: no
- Why: The owner is German, but the desktop is English. Plasma has LANGUAGE=en_US and LANG=en_US.UTF-8 with German address, currency, name and phone formats; /etc/default/locale has LANG=en_US.UTF-8. GRUB menu titles ('Advanced options for Ubuntu') are translated when grub-mkconfig runs. The countdown is translated at boot using the lang that grub-mkconfig wrote; de.mo is in /boot/grub/locale. So both depend on the locale grub-mkconfig ran with. A helper that runs grub-mkconfig with LC_ALL=C or with the caller's environment can produce a different menu from a kernel-update update-grub, with different text widths.
- Default: - English UI only; a German translation is a non-goal for v1. Strings still go through KDE's i18n calls, but no catalog is maintained.
- Dates, sizes and durations use the session locale; history and snapshot file names use ISO 8601 UTC.
- The helper runs grub-mkconfig with LANG, LANGUAGE and LC_MESSAGES from /etc/default/locale, so Personae applies and package-triggered runs produce identical menus. Previews use those same strings.
- If fonts are subset with grub-mkfont, keep at least U+0020–U+017F plus GRUB's arrow glyphs, so umlauts or os-prober entries do not lose glyphs later.
- polkit message, .desktop file and notifications are in English.
- Question: English-only UI, with no German translation planned?

### Per-target editors and previews (GRUB, Plymouth two-step, KSplash)

INTERVIEW.md settles what each designer contains, but not how editing, previewing, validating and saving work. I checked target behaviour read-only on this machine and in the Ubuntu and upstream sources, and several facts change what the editors must model.

Two items must be settled before anything is built:
1. Plymouth two-step fails silently, or loses its spinner, unless the generator follows the plugin's rules: entry.png and lock.png are mandatory; a theme with only throbber frames needs UseEndAnimation=false in every mode; live text is always white; the throbber loop is fixed at 2.0 s.
2. The implied KSplash plan (animated-WebP size variants in AnimatedImage) has two problems. With Overload it peaked at 1.5 GB RAM over a 3.8 s splash this boot. It also depends on a Qt plugin that nothing installed strictly requires, so a minimal reinstall could drop it. QtMultimedia Video with one H.264 encode is the better default, pending a short spike.

How close each preview can get to the real boot:
- **GRUB:** our own renderer. Geometry and 1-bit .pf2 text match exactly for the generated subset; image filtering is approximate; speed and firmware mirroring are not simulated. The QEMU pixel-exact preview stays a later option.
- **Plymouth two-step:** our own renderer. Layout formulas and the 2.0 s throbber timing are exact; live text is approximate; the simpledrm-to-amdgpu handover is not simulated.
- **Plymouth script themes:** a static image only.
- **KSplash:** near-exact for generated packages (embedded QML, simulated stage timing; the real splash lasts about 3.8 s). Imported splashes are exact only through ksplashqml run as a separate process, at the real screen size.

No files were written.

#### Plymouth two-step: editor model must encode the plugin's hard rules

- Priority: mvp-blocker; owner decision needed: no
- Why: Checked in Ubuntu's two-step source and this system's plymouth 24.004.60; none of it is in INTERVIEW. (1) entry.png and lock.png are mandatory: if either fails to load, the splash fails and Plymouth drops to the text theme, even without LUKS. (2) A theme with only throbber-NNNN.png and UseEndAnimation left at its default (true) loses its loop. throbber- is the last-resort end-animation prefix, so the looping throbber is discarded and the frames play once at the end. (3) Live text (title, subtitle, message, prompt) is always opaque white. two-step never sets a label colour, and ply_label_new defaults to 1.0/1.0/1.0/1.0 (disassembled from libply-splash-graphics here). (4) Mode sections (boot-up, shutdown, reboot, updates, system-upgrade, firmware-upgrade, system-reset) can override only SuppressMessages, UseProgressBar, ProgressBarShowPercentComplete, UseFirmwareBackground, UseAnimation, UseEndAnimation, Title and SubTitle. All images are shared across modes except the end animation (startup-animation- vs shutdown-animation-).
- Default: Fixed Plymouth element set: Background, Fixed-size layers (watermark), Spinner (throbber) or Progress (built-in bar or progress-NNNN frames), optional End animation, Password prompt, Live text.
(a) The app ships default prompt images (entry, bullet, lock, box, capslock, keyboard). 'Password prompt' can be restyled but not removed, and a golden test fails if entry.png or lock.png is missing.
(b) The generator writes UseEndAnimation in every mode section: false unless the design has an end animation. An end animation is emitted as startup-animation-/shutdown-animation- frames, never as throbber-.
(c) Live text offers size only; its colour is shown as fixed white. The validator samples the background under the title and message areas at each resolution and warns below 3:1 contrast.
(d) Preview controls: mode switcher, progress slider 0-100 %, 'Play boot' at the measured length (plymouth-quit ran 17.2 s after kernel start here, so about 15 s), prompt on/off.
v1 uses the same visuals in every mode. It keeps Ubuntu's stock Title/SubTitle for the update, upgrade and reset modes, and shows no titles for boot-up, shutdown and reboot.
- Question: Should shutdown/reboot look different from boot (another end animation, or a white Ubuntu-font 'Shutting down' title)?
- Notes: Mode-specific text in a custom font needs the script module (round 3 Q3: script only when a design needs it).

#### KSplash full-screen video: QtMultimedia Video (H.264 MP4), not animated-WebP size variants

- Priority: mvp-blocker; owner decision needed: no
- Why: Round 3 found the Overload approach (animated WebP shown by AnimatedImage), and the plan builds on it with per-resolution WebP variants. That approach has two verified problems.
Memory: AnimatedImage defaults to cache: true, which sets QMovie::CacheAll and keeps a pixmap for every decoded frame (qquickanimatedimage.cpp, Qt 6.10). This boot's user journal shows plasma-ksplash.service with Overload (1080p WebP) at a '1.5G memory peak' for a 3.8 s splash. ksplashqml opens one window per screen, so each screen has its own decoder. A 1440p variant has 1.8x the pixels, decoded on the GUI thread, twice.
Dependency: libqwebp.so comes from qt6-image-formats-plugins. Nothing installed hard-depends on it (only weak dependencies from kubuntu-desktop, dolphin and gwenview), so the planned minimal reinstall can drop it and the splash goes black.
QtMultimedia, by contrast, is guaranteed: plasma-workspace -> qml6-module-qtquick-virtualkeyboard -> qml6-module-qtmultimedia, and the FFmpeg backend ships in libqt6multimedia6. Qt 6.10's Video.qml exposes loops and fillMode, and ffmpeg 8.0.1 here has libx264.
- Default: The generated Splash.qml plays one silent H.264 MP4, encoded once at 2560x1440 and at most 30 fps, via Video { loops: MediaPlayer.Infinite; fillMode: VideoOutput.PreserveAspectCrop }. It sits on top of an Image of the first frame, so nothing goes black while the decoder starts. Content fades in at stage 2. No per-resolution variants: QML scales on the GPU.
Animated WebP stays available as an opt-in for short clips that need alpha, always with cache: false.
Before building the generator, run a spike with ksplashqml --test on both monitors. Compare MP4 and WebP on systemd's 'memory peak', first-frame delay and the loop seam.
Build-time check: the QtMultimedia QML module is present.
- Notes: KSplash was on screen about 3.8 s here (journal 25.80 s -> 29.65 s). The first ~4 s of a clip is what the owner sees, so the trim matters more than the loop point.

#### Editor shell: canvas + element list + inspector, writing each target's native units

- Priority: mvp; owner decision needed: no
- Why: INTERVIEW lists what each designer contains (round 3 Q2/Q3/Q5) but not how editing works. The targets position things differently (checked in source):
- GRUB: component left/top/width/height accept integer % plus integer px. '35.5%' is misread because the parser uses grub_strtoul. boot_menu metrics (item_height, item_padding, item_spacing, icon_width/height, item_icon_space) and fonts are pixels only.
- Two-step: throbber, progress animation and end animation are centred on the alignment point (x = a*W - w/2). Watermark, progress bar, dialog and title are edge-aligned (x = a*(W - w)).
- KSplash: QML is free-form.
A generic 'x/y in %' model would make the preview and the real boot disagree.
- Default: One shell for all three editors: element list on the left, live canvas in the centre, inspector with numeric fields on the right.
The canvas shows the real build artefacts (flattened PNGs, 9-slice sets, .pf2 text, generated Splash.qml), produced by an incremental preview build into the cache. Preview and output therefore share one code path.
Canvas: click to select, drag to move, arrow-key nudge (1 px at the 1440p reference, Shift = 10 px), snap to screen centre lines and other elements' edges, zoom Fit/100 %/200 %. Resize handles appear only where the target has a box (GRUB components, KSplash elements).
A drag writes the native value:
- GRUB: whole % with the remainder as a px offset.
- Two-step: the alignment fraction from that element's formula.
- KSplash: % of screen for position, % of screen height for size.
The element list has fixed singletons per target, plus 'Add image/label' (GRUB) and 'Add image/text' (KSplash). List order is draw order where the target honours it (GRUB file order, QML order); two-step's order is fixed.
Every canvas action has an inspector equivalent, so keyboard-only editing works.
Not in v1: rotation, rulers, grouping, multi-select align tools.
- Question: Do you want drag-on-canvas in v1, or are click-to-select plus numeric fields enough?

#### Animation import: Plymouth's fixed 2.0 s throbber loop and per-target presets

- Priority: mvp; owner decision needed: no
- Why: Checked in ply-throbber.c: THROBBER_DURATION is 2.0 with a 30 fps tick, and the frame shown is frames * (t mod 2.0) / 2.0. Any sequence is stretched to exactly 2 s, frames beyond 60 are skipped, and frames are never scaled. So fps and loop length are not free settings for Plymouth: a 1.2 s GIF spinner silently plays at 60 % speed. GIFs also bring 1-bit transparency (jagged edges over the background) and per-frame delays. The current splash (fallout) is a script theme that rotates a single PNG at runtime, which two-step cannot do.
- Default: One import dialog with a target preset and a live preview of the result.
Plymouth spinner:
- Resample to 60 frames covering exactly 2.0 s. Pack k whole source loops when the source loop is 1 s or shorter, otherwise time-stretch, and show the speed change ('plays at 60 % speed').
- Size in px at the 1440p reference.
- 'Matte against design background' option for GIF edges.
- A generator that rotates a still image 360 degrees over 2 s.
KSplash spinner: the same PNG frames, played by QtQuick AnimatedSprite (core QtQuick, no plugin).
KSplash video: trim in/out with the visible window (about the first 4 s) marked, crop-fill to 16:9, fps cap 30, one quality slider. Encoding follows the KSplash video gap.
The editor plays the master with trim and crop applied live. Encoding happens only at build time, in the background with progress and cancel, cached by input hash.
Decoders already installed: ffmpeg 8.0.1 for video, GIF and APNG; Pillow 12 or ImageMagick 7 for animated-WebP sources.
- Notes: Later: loop-seam crossfade (ffmpeg xfade) and end-animation import.

#### GRUB preview: own renderer that draws text from the generated .pf2 and simulates the real menu

- Priority: mvp; owner decision needed: no
- Why: A plain Qt preview would mislead:
- PF2 glyphs are 1 bit per pixel (grub-mkfont renders with FT_LOAD_RENDER | FT_LOAD_MONOCHROME), so GRUB text is never antialiased. Antialiased preview text flatters thin or small fonts.
- Long titles are clipped with no ellipsis. The visible-entry count is the box height minus paddings, divided by item_height + item_spacing.
- The installed themes (hollow-grub, crossgrub) ship only .pf2 files, and their internal names differ from the file names ('Trajan Pro Bold 25' inside HollowFont25.pf2). A .pf2 reader is needed anyway.
- An unknown global theme property makes GRUB abort the whole theme and fall back to the text menu. Unknown component properties are ignored: hollow-grub's item_align is not a GRUB 2.14 property and does nothing.
- The real entry titles are in /boot/grub/grub.cfg, mode 0600, which the GUI cannot read.
- Default: The renderer covers what the generator emits plus what the installed themes use:
- integer % plus/minus px geometry
- desktop-image with its scale methods
- image
- label, including __timeout__ with %d and @KEYMAP_SHORT/MIDDLE/LONG@
- progress_bar
- several boot_menus
- 9-slice pixmap styles, stretched the way GRUB stretches them
- icons/<class>.png scaled to icon_width x icon_height (first matching class wins, PNG only)
- terminal box
Text is blitted from .pf2 glyph bitmaps; grub-mkfont runs for the sizes in use.
Sample menu, built from files the GUI can read: GRUB_DISTRIBUTOR ('Kubuntu') and the /boot kernels give 'Kubuntu' and 'Advanced options for Kubuntu'. Add 'UEFI Firmware Settings' and whatever the installed memtest86+, os-prober and fwupd scripts may add. The list is editable. Icons show only on entries that carry classes (kubuntu, gnu-linux, gnu, os, memtest); the submenu, UEFI and fwupd entries have none.
Preview states: countdown running, countdown interrupted (timeout components hidden), submenu, terminal ('e'/'c').
Strings follow GRUB's locale, taken from LANG (en_US here).
Fidelity: geometry and text exact for this subset; image filtering approximate; speed not simulated. The pixel-exact QEMU preview stays the later option from round 3.
- Notes: Cheap: the .pf2 format is a short sectioned binary and the item-count formula is one line. This is not a push for pixel-perfect previews (round 2 Q4); it is the cheapest way to keep the preview from misleading.

#### GRUB designer: compile high-level controls into GRUB primitives

- Priority: mvp; owner decision needed: no
- Why: Round 3 Q2 asks for 'selection images or colors', but GRUB 2.14's boot_menu has no background-colour or text-alignment property. list_set_property knows only item/selected fonts and text colours, three *_pixmap_style 9-slice sets, metrics, scrollbar, visible and id. Entries are always left-aligned after the icon, and there are no shadows or rounded boxes.
A font reference must equal the full name stored inside the .pf2 ('Family Style Size', with the family from -n or FreeType). On a mismatch GRUB silently uses another loaded font.
The v1 list leaves out terminal-box and terminal-font. Yet the terminal ('e'/'c') is the emergency path for editing kernel options, and without terminal-font it uses a small default font at 1440p.
- Default: - Selection or menu 'colour' = fill, corner radius and border, rendered at build time into 9-slice PNG sets that the preview also uses.
- 'Text shadow' = a duplicate label or boot_menu offset by 1-3 px (crossgrub's trick).
- Decorative static text (title, tagline) can be a baked PNG image component: antialiased, any font, scales with its % box. Dynamic text (entries, countdown, keymap) stays .pf2.
- Menu text alignment is not offered.
- Fonts: grub-mkfont -n personae-<slug> -s <px> -r 0x20-0x7E,0xA0-0x17F,0x2010-0x2027,0x2190-0x2193 (ASCII, Latin-1, Latin Extended-A, punctuation, arrows). The builder reads NAME back from each .pf2 and writes that exact string into theme.txt. Hinting (default/autohint/none) sits under Advanced.
- Terminal: terminal-box reuses the selection style. terminal-font is Ubuntu Mono (installed) at about 24 px as a .pf2, the one fixed exception to 'one font'.
- Notes: 00_header emits insmod png/jpeg only for formats present in the theme root. The builder therefore keeps one PNG in the root whenever icons/ or the 9-slices use PNG, and the validator checks it. Golden tests assert that no unknown global keys are emitted.

#### Plymouth layers: flatten free layers into background.png (scales) and watermark.png (fixed px)

- Priority: mvp; owner decision needed: no
- Why: Round 3 Q4 bakes static text into images but does not say which image. Two-step has exactly one background and one watermark. The background is stretched to the screen when ScaleBackgroundImage is set (ply_pixel_buffer_resize keeps no aspect ratio). The watermark, like the throbber, is drawn at its native pixel size. Text baked into the background shrinks to half size at 720p; text in the watermark stays crisp but grows relative to the screen. Text that differs per mode cannot be baked, because all modes share one image set.
- Default: The Plymouth layer list has two groups:
- 'Scales with screen': images and baked text flattened into background.png at 2560x1440. The master is cropped to 16:9 in the picker, since the stretch ignores aspect.
- 'Fixed size': logos and baked text flattened into one watermark.png, cropped to the group's bounding box. WatermarkHorizontal/VerticalAlignment are set to x/(W - w) and y/(H - h) of that box. The group then lands exactly where drawn at 1440p and stays proportionally anchored at other resolutions.
Baked text defaults to the fixed group. Text that must differ per mode becomes live text (white, Ubuntu) or waits for the script module.

#### Per-resolution validation: what 'works at 1440p/1080p/720p' checks

- Priority: mvp; owner decision needed: no
- Why: Round 5 requires every target to work at all three resolutions without rebuilding. But GRUB fonts and metrics and two-step images have fixed pixel sizes, while boxes and backgrounds scale. At lower resolutions the usual failure is therefore overflow, because fixed-size parts grow relative to the screen. 'Text too small' only applies to content that scales (text baked into the Plymouth background, KSplash). Example: hollow-grub's 500 px boot_menu at top = 55% ends 14 px below the screen at 1080p and 176 px below at 720p.
- Default: A validator panel, recomputed at all three resolutions on every change. Canvas views: resolution switcher, 'Compare' (the three resolutions side by side at the same relative scale) and 'Bundle' (the three targets at one resolution, for round 3 Q6 consistency).
Checks:
- GRUB: component off-screen; fewer visible entries than the sample menu (exact formula); titles clipped (measured with .pf2 advances); countdown or keymap text wider than its label.
- Two-step: fixed elements off-screen or overlapping (throbber, watermark, prompt, and the message line at animation bottom + 10 px); baked text in the scaling group with cap height under 9 px at 720p; live-text contrast.
- KSplash: text under 11 px at 720p.
Red (blocks the build) only when output would be wrong or missing, such as an off-screen menu or missing mandatory files. Everything else is yellow and never blocks.

#### Save, autosave, snapshot and undo semantics

- Priority: mvp; owner decision needed: no
- Why: Round 4 Q3 says 'automatic snapshot on every save', but not whether saving is explicit or continuous; continuous autosave would create thousands of snapshots. It also leaves open how undo/redo relates to snapshots, and what build/apply uses while edits are unsaved. Round 6's system history should also lead back to the exact design that was applied.
- Default: - Explicit Save (Ctrl+S) writes the profile and creates a snapshot.
- A crash-recovery draft is written after about 10 s idle, to a separate file (not a snapshot), and offered at next start. Closing with unsaved changes asks first.
- Undo/redo per open profile, unlimited for the session and kept across saves. Each drag or slider move counts as one step.
- 'Restore snapshot' loads it as a new unsaved state, which can itself be undone.
- Build and Apply always use a saved snapshot, prompting to save first. Each system-history entry records the snapshot IDs it applied, so restoring an entry can reopen the design.
- Snapshots hold only profile text, because assets are immutable and content-addressed. Keep them all. An asset referenced by any snapshot can be hidden but not deleted.

#### KSplash: embedded live QML preview with a stage timeline; imports run out of process

- Priority: mvp; owner decision needed: no
- Why: Checked in plasma-workspace 6.6: ksplashqml sets a single property on the splash root, stage (1 to 6), and closes at 6, with a 30 s safety timeout. --test advances one stage every 2 s (about 10 s in total), closes on click or Esc, opens one window per screen, takes a package path, and cannot render at 1080p or 720p. The real splash here lasted about 3.8 s, so --test overstates the timing roughly 3x. System fonts live on / and will be lost in the planned reinstall; look-and-feel packages live in ~/.local and are kept.
- Default: With a Qt/QML stack, the KSplash canvas is the generated Splash.qml in an embedded QQuickView or QQuickWidget. It is sized to the selected resolution, scaled to fit, and driven by a stage scrubber and Play over the measured duration (default 4 s, or '2 s per stage like --test').
'Run real preview' launches ksplashqml --test --window on the built package.
Generated QML imports only QtQuick (plus QtMultimedia for video), sizes elements in % of screen height, and bundles the TTF/OTF fonts it uses under contents/splash/fonts via FontLoader.
The build renders contents/previews/splash.png from the preview, so the Splash Screen page in System Settings shows a thumbnail.
Imported splashes (Overload, Penrose) are never loaded into the app's own process.
- Notes: This argues for Qt/QML at the architecture gate. With another stack, the KSplash preview is ksplashqml only, at the real screen size.

#### Plymouth initramfs size budget meter

- Priority: mvp; owner decision needed: no
- Why: Both initramfs generators copy the whole theme directory into each initrd (the initramfs-tools hook uses cp -a; dracut's plymouth-populate-initrd uses inst_recur), and round 6 rebuilds all 5 kernels. Plymouth decodes PNG only (libply-splash-graphics links libpng; BMP is used just for the firmware logo), so a photographic 2560x1440 background costs several MB per initrd. The installed graphical themes range from 0.4 to 2.7 MB, and fallout's README, install.sh and preview.sh all land in the initramfs.
- Default: The builder emits only files the theme references. The Plymouth editor shows a live meter: 'theme X MB, adds X MB to each of 5 initrds (Y MB total)'. It turns yellow above 4 MB and asks for confirmation above 16 MB.
Remedies offered in place:
- background at 1920x1080, upscaled at boot (the stretch is on anyway)
- PNG optimisation
- optional 256-colour quantisation via Pillow (installed), with a before/after preview to check banding
- fewer or smaller spinner frames

#### Asset picker and per-use derivations

- Priority: mvp; owner decision needed: no
- Why: Round 4 Q1 stores assets once and has profiles reference them, but does not say where per-use edits live or what the picker shows. One master serves targets with different needs:
- GRUB loads PNG/JPEG/TGA, Plymouth PNG only, KSplash MP4 or frames.
- One video gets a different trim for KSplash than for a spinner.
- One font file becomes .pf2 (GRUB), baked PNG text (Plymouth, GRUB decorations) and a bundled TTF (KSplash).
- Default: Masters are immutable and content-addressed. Per-use transforms (crop rect, trim, frame count, px size, quality) live on the profile element, not on the asset. Derived files are cached by hash(master + params + tool version); they are safe to delete and regenerated on demand.
Picker: grid with a type filter (image, animation, video, font), thumbnail or poster frame, size/duration/frame count, a 'used by' list, import by file dialog or drag-and-drop, and 'Replace everywhere' as one undo step.
The crop tool locks backgrounds to 16:9. Fonts show a sample in each target's renderer (1-bit for GRUB).

#### Previews of imported read-only themes

- Priority: mvp; owner decision needed: no
- Why: Imports are read-only and preview-only (rounds 1-2), but how faithfully they can be previewed depends on the kind:
- Script themes (fallout, the current default; mint-galaxy; breeze) would need an interpreter for Plymouth's script language.
- Third-party QML splashes (Overload, Penrose) are code.
- Some imports ship scripts (fallout: install.sh, preview.sh).
- GRUB themes can carry properties that GRUB ignores, or that make it reject the whole theme.
- Default: - GRUB imports: the GRUB renderer, with a yellow badge for ignored component properties (hollow-grub's item_align) and a red one for unknown global properties ('GRUB will show the text menu').
- Two-step imports (spinner, bgrt, kubuntu-logo): the two-step renderer, including designs that show the firmware logo, since /sys/firmware/acpi/bgrt/image and its offsets are world-readable here.
- Script imports: no live preview in v1. Show the theme's preview.png if present (kubuntu-logo has one), otherwise background.png plus a contact sheet of its images, labelled 'script theme, not simulated'. Bundled scripts never run.
- KSplash imports: contents/previews/splash.png, plus 'Run real preview' via ksplashqml --test --window <path> as a separate process.
- Notes: A real Plymouth render through the x11 renderer would need the plymouth-x11 package and a running plymouthd. That clashes with 'change as little on the host as possible', so it is not planned.

#### Colour picking per target; eyedropper; palette extraction deferred

- Priority: mvp; owner decision needed: no
- Why: Each target takes colours differently. Two-step uses 0xRRGGBB with no alpha for BackgroundStartColor/EndColor (a gradient under the background image) and for the progress bar, and live text is fixed white. GRUB takes colour strings for text and progress bars. KSplash takes full RGBA. On Plasma Wayland a picker that samples anywhere on screen needs the desktop portal, but the colours people want are in their own assets.
- Default: One picker widget (hex, HSV, recent colours) that hides alpha on fields whose target ignores it. An eyedropper that samples the app's own canvas or asset. A 'bundle swatches' row with every colour used across the bundle's three profiles (round 3 Q6 consistency). Palette extraction from images comes later: the eyedropper covers the need, and extraction is about 20 lines with Pillow's quantize if it is wanted.
## 4. Model and mode research

**Short answer:** Opus 5.5 at max is a sound default, and nothing in the docs justifies Fable yet. Anthropic's guidance is to tune effort on Opus 5.5 first and move to Fable 5.1 only when evals at xhigh or max still fall short ([choosing a model](https://platform.claude.com/docs/en/about-claude/models/choosing-a-model)). Ultracode is not a quality upgrade. It is a Claude Code setting that adds multi-agent workflows and multiplies tokens and time ([workflows](https://code.claude.com/docs/en/workflows)). The ID `claude-fable-5-1` is correct.

**Facts**

Fable 5.1 vs Opus 5.5 (list prices per MTok):

| | Fable 5.1 (`claude-fable-5-1`) | Opus 5.5 (`claude-opus-5-5`) |
|---|---|---|
| Context / max output | 1M / 128K | 1M / 128K |
| Input / output | $10 / $50 | $4 / $20 |
| Cache read | $0.25 | $0.20 |
| Comparative latency | Slower | Moderate |
| Default effort | high | medium |
| Fast mode | not offered | $8 / $40, up to 2.5x output speed, usage credits only on subscriptions |
| Intended use | demanding reasoning, long-horizon agentic work | long-running agentic coding and knowledge work |

Sources: [Fable 5.1 overview](https://platform.claude.com/docs/en/models/fable-5-1/overview), [Opus 5.5 overview](https://platform.claude.com/docs/en/models/opus-5-5/overview), [fast mode](https://code.claude.com/docs/en/fast-mode).

- **Cost:** Fable is 2.5x Opus per token for uncached input and output, but only 1.25x for cache reads (my arithmetic). Thinking is billed as output, so max-effort turns feel the 2.5x.
- **Capability:** Anthropic calls Fable 5.1 its most capable model open to all customers ([choosing a model](https://platform.claude.com/docs/en/about-claude/models/choosing-a-model)). The Opus 5.5 announcement says Opus 5.5 "performs at the level of Claude Fable 5.1 on most work." It also reports CursorBench 52.5% for Opus 5.5 at medium vs 51.8% for Fable 5.1 at max, and GDPval-AA v2.1 1846 vs 1735 at max ([Opus 5.5 announcement](https://www.anthropic.com/claude-opus-5-5)). I read these through a fetch summary. The two announcements use different benchmark versions, so don't compare numbers across them.
- **Safeguards:** Fable's safeguards "still redirect several kinds of dual-use cybersecurity tasks" to Opus models, including penetration testing, exploit generation and binary-based vulnerability scanning. Life-sciences R&D also routes to Opus ([Fable 5.1 announcement](https://www.anthropic.com/claude-fable-and-mythos-5-1)).
- **Billing:** Depending on plan and seat tier, Fable usage can bill to usage credits instead of your plan's included limits ([model config](https://code.claude.com/docs/en/model-config)).
- **Switching:** On the Claude API, moving from Opus 5.5 to Fable 5.1 keeps reasoning blocks. Moving to any other model drops them for later turns ([Opus 5.5 what's new](https://platform.claude.com/docs/en/models/opus-5-5/whats-new-opus-5-5)). `/model fable` resolves to Fable 5.1 ([model config](https://code.claude.com/docs/en/model-config)).

Ultracode:
- It is a setting, not a model, effort level or permission mode. It turns on automatic workflow orchestration "at whichever effort level the session runs at" ([workflows](https://code.claude.com/docs/en/workflows)). Some third-party articles call it the top effort rung. The official docs don't.
- Turn it on with `/effort ultracode`, off with `/effort ultracode off`, or set the `ultracode` setting for every session. `claude --effort ultracode` starts at xhigh. A single prompt containing `ultracode` runs that task as a workflow without changing effort ([workflows](https://code.claude.com/docs/en/workflows), [model config](https://code.claude.com/docs/en/model-config)).
- Keeping it at any level other than xhigh needs Claude Code v2.1.284 or later. Older builds set xhigh ([model config](https://code.claude.com/docs/en/model-config)).
- Cost and time: each request "uses more tokens and takes longer," and on subscriptions it reaches session and weekly limits sooner ([workflows](https://code.claude.com/docs/en/workflows)). Every agent a workflow spawns sends its own requests ([costs](https://code.claude.com/docs/en/costs)).
- Scale: up to 16 concurrent agents and 1,000 per run by default. The Large-workflow warning is suppressed while ultracode is on. The default size guideline is medium (under 10 agents), or small on Pro ([workflows](https://code.claude.com/docs/en/workflows)).
- Effort in agents: a subagent on the session's model runs at the session's effort ([sub-agents](https://code.claude.com/docs/en/sub-agents)). Workflow agents use the session's model unless assigned otherwise ([workflows](https://code.claude.com/docs/en/workflows)). A max session therefore probably fans out at max, though the workflow page doesn't say so outright.
- Limits: runs take no input mid-run and pause only for permission prompts or usage-limit waits. "For sign-off between stages, run each stage as its own workflow" ([workflows](https://code.claude.com/docs/en/workflows)).
- Documented fits: auditing many files with adversarial verification of each finding, parallel migrations, and drafting a plan "from several independent angles" ([workflows](https://code.claude.com/docs/en/workflows)).
- Permissions: workflow agents' tool calls get the same permission checks and sandboxing as any other tool call ([workflows](https://code.claude.com/docs/en/workflows)).

Effort:
- Claude Code levels are low, medium, high, xhigh and max. Fable 5.1 and Opus 5.5 both accept all five ([model config](https://code.claude.com/docs/en/model-config)).
- Max is "the deepest reasoning level" and "may show diminishing returns and is prone to overthinking, so test before adopting it broadly" ([model config](https://code.claude.com/docs/en/model-config)).
- Opus 5.5 "tends to think more per turn" at a given level, most at xhigh and max ([Opus 5.5 what's new](https://platform.claude.com/docs/en/models/opus-5-5/whats-new-opus-5-5)). Anthropic claims Opus 5.5 at default effort beats Opus 5 at max on Terminal-Bench 4.0 for about a fifth of the cost ([Opus 5.5 announcement](https://www.anthropic.com/claude-opus-5-5), fetch summary).
- Effort is calibrated per model, so max on Opus 5.5 and max on Fable 5.1 are not the same amount of thinking ([model config](https://code.claude.com/docs/en/model-config)).
- In the `/effort` slider, Enter saves a level as that model's default. `s` applies it to this session only (v2.1.257+) ([model config](https://code.claude.com/docs/en/model-config)).
- `/usage` can flag max as a cost driver. Its docs use "Switch from max to high effort" as an example row ([costs](https://code.claude.com/docs/en/costs)).

**Could not verify**
- Ultracode "research preview" status. Secondary sources say it is one; the workflows page I read does not.
- Per-task cost and time for Fable vs Opus 5.5. Official pages give list prices and comparative labels only. A third-party index's per-task figures (medium near $1.34, max near $5.98) are unconfirmed.
- Anthropic's benchmark tables. I saw them only through a fetch summary.
- Whether workflow agents run at max. This is inferred from the subagent rule.
- Your plan, seat tier and build. Run `claude --version` (2.1.284+ keeps ultracode at max) and `/usage`.
- Whether Opus 5.5 at max beats the alternatives on your tasks. Only your own evals can show that.

**Recommendation per phase** (my synthesis of the docs)
- **Spec (interview to SPEC.md):** Opus 5.5 at `high`. Keep ultracode off and interview turns in normal conversation, since a workflow can't take your answers mid-run. Max adds latency and tokens per turn while your answers are the bottleneck.
- **Architecture:** Opus 5.5 at `xhigh`. Try `max` once on the hardest decision (privileged-helper trust boundary, apply and rollback) and keep it only if clearly better. Optionally, a one-off "use a workflow" to draft two or three competing designs, with `/config workflowSizeGuideline=small`. No Fable yet.
- **Implementation:** Opus 5.5 at `medium` for well-scoped steps, and `high` for the privileged helper, apply and rollback paths, and bug fixes. Keep ultracode off so diffs stay small and reviewable. A one-off workflow suits mechanical fan-out, such as golden-file tests across generators. Use `/fast` only for live debugging. `opusplan` (Opus plans, Sonnet executes) is a cost option once steps are well specified.
- **Review:** `max` for the privileged helper and security review. A one-off workflow that reviews each changed file and adversarially verifies findings is the documented pattern. Keep Fable out of security review, since vulnerability scanning and pentesting route to Opus anyway. Enforce the repo-only write rule in permission settings, because that is what workflow tool calls are checked against.
- **Fable trigger:** Move only when golden-file tests or an eval set show Opus 5.5 at xhigh or max failing on real tasks. The project's golden files can serve as that eval set.
