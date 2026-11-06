"""Shared code of the fake system commands the test helper runs (tests only).

Each stub works on the fake root in $EKPO_HELPER_ROOT. Creating
$EKPO_HELPER_ROOT/stub-fail/<command> makes that command fail; the marker is
removed on the first failure unless its content is "always".
"""
import glob
import os
import subprocess
import sys

ROOT = os.environ["EKPO_HELPER_ROOT"]


def at(path):
    return ROOT + path


def maybe_fail(name):
    marker = at(f"/stub-fail/{name}")
    if os.path.exists(marker):
        always = open(marker).read().strip() == "always"
        if not always:
            os.remove(marker)
        sys.exit(f"{name}: injected failure")


def kernels():
    names = glob.glob(at("/boot/vmlinuz-*"))
    return sorted(os.path.basename(n)[len("vmlinuz-"):] for n in names if not os.path.islink(n))


def read_alternative(name):
    """Returns (mode, link, slaves, choices) of /var/lib/dpkg/alternatives/<name>."""
    lines = open(at(f"/var/lib/dpkg/alternatives/{name}")).read().split("\n")
    mode, link, i, slaves = lines[0], lines[1], 2, []
    while lines[i]:
        slaves.append((lines[i], lines[i + 1]))
        i += 2
    i += 1
    choices = []
    while i < len(lines) and lines[i]:
        choices.append([lines[i], int(lines[i + 1]), lines[i + 2 : i + 2 + len(slaves)]])
        i += 2 + len(slaves)
    return mode, link, slaves, choices


def write_alternative(name, mode, link, slaves, choices):
    lines = [mode, link]
    for slave in slaves:
        lines += list(slave)
    lines.append("")
    for path, priority, slave_values in choices:
        lines += [path, str(priority)] + slave_values
    lines.append("")
    with open(at(f"/var/lib/dpkg/alternatives/{name}"), "w") as f:
        f.write("\n".join(lines) + "\n")


def select(name, path):
    target = at(f"/etc/alternatives/{name}")
    if os.path.lexists(target):
        os.remove(target)
    os.symlink(path, target)


def selected_plymouth_theme():
    path = os.readlink(at("/etc/alternatives/default.plymouth"))
    return os.path.basename(os.path.dirname(path))


def build_initrds(only=None):
    """An initrd here is a text file listing the Plymouth theme files it holds.
    only: rebuild just this kernel version's initrd."""
    theme = selected_plymouth_theme()
    theme_dir = at(f"/usr/share/plymouth/themes/{theme}")
    listing = []
    for dirpath, _, files in os.walk(theme_dir):
        for f in files:
            listing.append(os.path.relpath(os.path.join(dirpath, f), ROOT))
    for version in [only] if only else kernels():
        if version not in kernels():
            sys.exit(f"no kernel {version}")
        with open(at(f"/boot/initrd.img-{version}"), "w") as f:
            f.write("\n".join(sorted(listing)) + "\n")


def effective_grub_theme():
    script = 'set -a; . "$1/etc/default/grub"; for f in "$1"/etc/default/grub.d/*.cfg; do [ -e "$f" ] && . "$f"; done; printf "%s" "${GRUB_THEME-}"'
    return subprocess.run(["bash", "-c", script, "bash", ROOT], capture_output=True, text=True, check=True).stdout
