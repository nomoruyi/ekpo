#!/bin/bash
# Runs Ekpo against a writable copy of the reference system in .dev/sandbox: applies
# go through the test helper and stub commands, so nothing outside the repo changes.
# Usage: dev/sandbox.sh [--reset]
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo/dev/env.sh"
sandbox="$EKPO_DEV/sandbox"
if [[ "${1:-}" == "--reset" || ! -d "$sandbox/root" ]]; then
    rm -rf "$sandbox"
    mkdir -p "$sandbox"
    cp -a "$repo/tests/fixtures/roots/reference" "$sandbox/root"
    mkdir -p "$sandbox/root/boot/grub" "$sandbox/root/var/log"
    EKPO_HELPER_ROOT="$sandbox/root" "$repo/tests/stubs/update-grub"
    EKPO_HELPER_ROOT="$sandbox/root" "$repo/tests/stubs/update-initramfs" -u -k all
fi
# Dev runs disable core dumps, which also hides /proc/<pid>/root from the desktop
# portal; its "Failed to register with host portal" warning is harmless here.
export QT_LOGGING_RULES="${QT_LOGGING_RULES:+$QT_LOGGING_RULES;}qt.qpa.services.warning=false"
EKPO_SYSTEM_ROOT="$sandbox/root" EKPO_SYSTEM_HOME="$sandbox/root/home" exec "$repo/build/src/app/ekpo"
