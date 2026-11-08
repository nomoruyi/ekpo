# Night Menu

A boot menu (GRUB theme) made with [Ekpo](https://github.com/nomoruyi/ekpo), from version 1 of the design.

By nomoruyi, under the MIT licence (LICENSE).

## Install

Built for a 2560×1440 screen. On Debian, Ubuntu and their relatives:

```sh
sudo cp -r ekpo-night-menu-v1 /boot/grub/themes/
printf 'GRUB_THEME="/boot/grub/themes/ekpo-night-menu-v1/theme.txt"\nGRUB_GFXMODE="2560x1440,auto"\n' | sudo tee /etc/default/grub.d/zz-ekpo-night-menu-v1.cfg
sudo update-grub
```

Elsewhere, set GRUB_THEME and GRUB_GFXMODE like that in /etc/default/grub and rebuild grub.cfg (grub-mkconfig or grub2-mkconfig). To undo: delete the .cfg file and the folder, then run update-grub again.
