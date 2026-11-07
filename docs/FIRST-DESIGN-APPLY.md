# First real applies of your own designs

Designs from the editor (M2 splash designers, M3 Boot Menu designer) have only been
applied to fake system copies inside the repo. This guide applies one design per
stage on the real system, one at a time, with a re-login or reboot after each.
Every step says what it changes and how to undo it.

What these steps test for the first time: the packages Ekpo generates (a Plasma
look-and-feel package, a Plymouth two-step theme, a GRUB theme with `.pf2` fonts)
shown by the real KSplash, Plymouth and GRUB; `GRUB_GFXMODE` pinned to your screen;
the check for cut-off menu entries against the real `grub.cfg`; and the Overview's
check of the mode GRUB really booted in.

Your current look is history entry **#5 "FirstTry"** (crossgrub, WindozeXP, Penrose).
"**Back to #5**" below means: History → "FirstTry" → **Restore…** → **Apply**.

## 0. Safety net (once)

1. Have a bootable live USB at hand.
2. Take a Timeshift snapshot of `/`:

   ```bash
   sudo timeshift --create --comments "before Ekpo designs"
   ```

   Changes: adds one btrfs snapshot of `@`. Undo: `sudo timeshift --delete` and pick it.
3. Back up the library. On its first start, 0.4 moves it from format 1 to 2 (it adds
   `designs/` and `assets/`; nothing is removed), and Ekpo 0.2.0 refuses a format 2
   library:

   ```bash
   cp -a ~/.local/share/ekpo ~/.local/share/ekpo.backup-0.2.0
   ```

## 1. Install 0.4.1

The release appears once CI has run on the commit that adds this guide. Download it and check the sums:

```bash
GH_TOKEN=$(gh auth token --user nomoruyi) gh release download v0.4.1 -R nomoruyi/ekpo -D ~/Downloads/ekpo-0.4.1
```

```bash
cd ~/Downloads/ekpo-0.4.1 && sha256sum -c SHA256SUMS
```

```bash
sudo apt install ~/Downloads/ekpo-0.4.1/ekpo_0.4.1_amd64.deb
```

Changes: upgrades Ekpo to 0.4.1. Its new dependencies (`ffmpeg`,
`qml6-module-qtmultimedia`, `fontconfig`) are already installed, so apt adds nothing
else. Nothing about the boot look changes. Undo: `sudo apt remove ekpo` (the looks it
applied stay; they are plain theme packages).

Start Ekpo. The Overview shows the same look as before, and the Boot Menu card says
"Primary screen DP-1 at 2560×1440; this boot's menu showed at 2560×1440."

## 2. Plasma splash design (no password)

1. **Plasma Splash** → **New Design** → **Welcome and Spinner**. The editor opens.
2. Change the text, give the background a second colour (**Gradient to:**), and add a
   **Progress Bar** (**Add** menu). **Save**.
3. **Run Real Splash**: Plasma's own splash plays the design in a window. Nothing on
   the system changes.
4. **Apply…** → the review shows one stage: copy the package to
   `~/.local/share/plasma/look-and-feel/ekpo-<name>-v1` and set it in
   `~/.config/ksplashrc`. → **Apply**.

Check:

```bash
grep Theme= ~/.config/ksplashrc && ls ~/.local/share/plasma/look-and-feel/
```

Log out and in: the splash shows, and the bar fills while Plasma starts.

Optional, for video: **Add** → **Video…** with a short MP4 (the splash plays it
itself) → **Save** → **Apply…** → log out and in.

Undo: back to #5 (no password).

## 3. Boot splash design

1. **Boot Splash** → **New Design** → **Spinner on a Gradient** → **Save**.
2. **Apply…** → the review shows: install a copy as
   `/usr/share/plymouth/themes/ekpo-<name>-v1`, select it as the boot splash,
   `Theme=ekpo-<name>-v1` in `/etc/plymouth/plymouthd.conf`, and rebuild only the newest
   kernel's initrd. Leave **Rebuild the initrds of all 5 kernels** off, and
   **Built for the screen size** at 2560×1440 (your primary screen).
3. **Apply (asks for your password)**.

Changes: copies the theme, registers it as a `default.plymouth` alternative
(priority 50) and selects it, writes `plymouthd.conf`, runs
`update-initramfs -u -k 7.0.0-42-generic` and `update-grub`. Check:

```bash
update-alternatives --query default.plymouth | grep -E '^(Status|Value)'
```

```bash
grep Theme= /etc/plymouth/plymouthd.conf && ls /usr/share/plymouth/themes | grep ekpo
```

Reboot (default entry): the spinner on the gradient, at full resolution from the
first frame. The shutdown splash on the way down shows it too.

Undo: back to #5 (WindozeXP again; it rebuilds all initrds, about 40 s). If the
splash misbehaves before that: the older kernels under "Advanced options" still
have WindozeXP.

## 4. Boot menu design

1. **Boot Menu** → **New Design** → **Centred Menu**. The preview draws in GRUB's
   own fonts, with the entries this system's GRUB shows.
2. Under **Entry icons:** → **Built-in**. Under the preview there should be no
   warning, or only "The menu widens to … so … fits."
3. **Save** → **Apply…** → the review shows: install a copy as
   `/boot/grub/themes/ekpo-<name>-v1`, and `zz-ekpo.cfg` with that theme and
   `GRUB_GFXMODE="2560x1440,auto"`; built for 2560×1440. → **Apply (asks for your
   password)**.

Changes: copies the theme, rewrites `/etc/default/grub.d/zz-ekpo.cfg`, runs
`update-grub`; afterwards Ekpo checks the real menu entries against the theme. Check:

```bash
cat /etc/default/grub.d/zz-ekpo.cfg && ls /boot/grub/themes/ekpo-*/
```

Expect `theme.txt`, `.pf2` fonts, `select_*.png` and `icons/`; `background.png` only
with a gradient or an image.

Reboot and look at the menu. The text should be sharp and the size the preview
showed. No entry should be cut off, and the icons should show. The countdown runs
from 20 s; on this system GRUB always waits 20 s, because `/boot` is on btrfs. A
phone photo of the menu next to the editor's preview helps.

After logging in, check the Boot Menu card on the Overview. It should say "this boot's
menu showed at 2560×1440", with no warning. "GRUB couldn't use …" or "GRUB cuts off …"
is what I need to know about.

Undo: back to #5 (crossgrub again).

## 5. All three as one Mask (optional)

1. **Masks** → **New Mask…** → pick the three designs → **Save** → **Apply…** →
   reboot.
2. Change a colour in one of the designs → **Save**. The Mask says "Update available:
   a design in it has changed since it was applied." → **Apply…** applies the new
   version.

Undo: back to #5.

## If something goes wrong

- **Boot menu garbled or unreadable:** GRUB still boots the first entry when the
  countdown ends. Then go back to #5.
- **Splash broken or hanging:** pick an older kernel under "Advanced options"; it
  still has WindozeXP. Otherwise press `e` on the first entry, delete the word
  `splash` from the `linux` line and boot with Ctrl+X. Then go back to #5, or run
  `ekpo restore 5`.
- **An apply was cut off** (power loss, crash): Ekpo shows **Roll Back Now**, or run
  `ekpo recover`.
- **Can't boot at all:** start the live USB and restore the Timeshift snapshot.
- **Something in Ekpo looks wrong:** Report a Bug… (Ctrl+Shift+B) attaches the page,
  the system report and the helper's log.

Please send me the output of these after each step:

```bash
ekpo status
journalctl -t ekpo-helper -b --no-pager
```
