# First real applies

Ekpo has only ever run against fake system copies inside the repo. This is the
first time it touches the real boot setup, so go one stage at a time and reboot
after each. Every step says what it changes and how to undo it.

What hasn't been tested yet and is tested by these steps: the password prompt
(polkit + pkexec), the real `update-grub`, `update-alternatives` and
`update-initramfs`, and `systemd-inhibit`.

## 0. Safety net (once)

1. Have a bootable live USB at hand.
2. Take a Timeshift snapshot of `/`:

   ```bash
   sudo timeshift --create --comments "before Ekpo"
   ```

   Changes: adds one btrfs snapshot of `@`. Undo: `sudo timeshift --delete` and pick it.

## 1. Install

```bash
sha256sum /extra/data/Coding/ekpo/build-release/ekpo_0.2.0_amd64.deb
```

The sum must be `aab345884f6a59f101988f5d90963ed2b8383a89e79f61b580c1dbac456e7a97`.

```bash
sudo apt install /extra/data/Coding/ekpo/build-release/ekpo_0.2.0_amd64.deb
```

If an older version is installed, the same command upgrades it. Changes: installs `/usr/bin/ekpo`, `/usr/libexec/ekpo/ekpo-helper`, the polkit
action `io.github.nomoruyi.ekpo.apply`, a menu entry and the icon. Every dependency
is already installed, so nothing else is added. Nothing about the boot look changes.
Undo: `sudo apt remove ekpo`.

Check that polkit knows the action (read-only):

```bash
pkaction --action-id io.github.nomoruyi.ekpo.apply --verbose
```

## 2. Boot menu only

1. Start Ekpo from the menu (or run `ekpo`). Read the intro.
2. **Boot Menu** page → under "Installed on this system" → **Import** next to `hollow-grub`.
3. On its card → **Apply…**. The review shows one stage: Ekpo writes its own GRUB
   settings with the same theme plus the video modes 1440p/1080p/720p.
4. **Apply (asks for your password)** → enter your password.

Changes: writes `/etc/default/grub.d/zz-ekpo.cfg`, runs `update-grub`. Original is
saved in `~/.local/share/ekpo/history/original.json`. Check:

```bash
cat /etc/default/grub.d/zz-ekpo.cfg
```

Reboot. The menu should look as before. Ekpo then asks "Did the new look work?".

Undo (any of these): History → **Restore Original…**; `ekpo restore original`; or by hand:

```bash
sudo rm /etc/default/grub.d/zz-ekpo.cfg && sudo update-grub
```

To try a different look, apply `crossgrub` the same way.

## 3. Boot splash only

1. **Boot Splash** page → **Import** next to `mint-galaxy`.
2. **Apply…** → the review shows the alternative switch, `plymouthd.conf`, and the
   initrd rebuild. Leave **Rebuild the initrds of all 5 kernels** off for this first
   try: only the newest kernel (7.0.0-42) gets the new splash (about 8 s), and the
   four older ones keep fallout, so they boot with a known-good splash from
   "Advanced options" if mint-galaxy misbehaves. → **Apply**.

Changes: `update-alternatives --set default.plymouth …/mint-galaxy.plymouth`,
`Theme=mint-galaxy` in `/etc/plymouth/plymouthd.conf`, `update-initramfs -u -k 7.0.0-42-generic`,
`update-grub`. Reboot (default entry): the splash is mint-galaxy. The Overview notes that
the other kernels keep the previous splash; that's expected.

Undo: History → **Restore Original…** (back to fallout, initrds rebuilt again), or by hand:

```bash
sudo update-alternatives --set default.plymouth /usr/share/plymouth/themes/fallout/fallout.plymouth
sudo sed -i 's/^Theme=.*/Theme=fallout/' /etc/plymouth/plymouthd.conf
sudo update-initramfs -u -k all
```

## 4. A whole Mask

1. **Plasma Splash** page → **Import** `Penrose`.
2. **Masks** → **New Mask…** → pick a theme per stage → **Save** → **Apply…**.
3. Log out and in to see the Plasma splash; reboot for the boot stages.

The Plasma splash needs no password and is undone with the rest.

## If something goes wrong

- **Splash broken or hanging:** if only the newest kernel was rebuilt, pick an older
  kernel under "Advanced options"; it still has the previous splash. Otherwise, in the GRUB menu press `e` on the first entry,
  delete the word `splash` from the `linux` line, boot with Ctrl+X (text boot),
  then run `ekpo restore original`.
- **An apply was cut off** (power loss, crash): Ekpo shows **Roll Back Now**, or run
  `ekpo recover`.
- **Can't boot at all:** start the live USB and restore the Timeshift snapshot.
- **What the helper did:** `journalctl -t ekpo-helper`.

Please send me the output of these after each step:

```bash
ekpo status
journalctl -t ekpo-helper -b --no-pager
```
