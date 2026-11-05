# Source before every build, test or run: keeps all writes inside the repo (CLAUDE.md hard rule).
_personae_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PERSONAE_DEV="$_personae_root/.dev"
export HOME="$PERSONAE_DEV/home"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CACHE_HOME="$HOME/.cache"
export XDG_STATE_HOME="$HOME/.local/state"
export TMPDIR="$PERSONAE_DEV/tmp"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$TMPDIR"
unset _personae_root
