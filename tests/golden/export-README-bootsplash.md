# Night

A boot splash (Plymouth theme) made with [Ekpo](https://github.com/nomoruyi/ekpo), from version 1 of the design.

By Somebody, under the CC-BY-4.0 licence (LICENSE).

## Install

Built for a 2560×1440 screen. On Debian, Ubuntu and their relatives:

```sh
sudo cp -r ekpo-night-v1 /usr/share/plymouth/themes/
sudo update-alternatives --install /usr/share/plymouth/themes/default.plymouth default.plymouth /usr/share/plymouth/themes/ekpo-night-v1/ekpo-night-v1.plymouth 100
sudo update-alternatives --set default.plymouth /usr/share/plymouth/themes/ekpo-night-v1/ekpo-night-v1.plymouth
sudo update-initramfs -u -k all
```

If /etc/plymouth/plymouthd.conf names a theme (`Theme=`), set it to ekpo-night-v1 too. On Fedora, Arch and others: `sudo plymouth-set-default-theme -R ekpo-night-v1`. The kernel command line needs `splash` for a graphical boot splash.
