#!/usr/bin/env bash
# Register zwriter with the desktop (dock / taskbar icon + hover name) for runs
# from a build tree, without installing anything system-wide.
#
#   tools/install-desktop-entry.sh [path/to/zwriter]   # default: build/zwriter
#   tools/install-desktop-entry.sh --remove
#
# Writes ~/.local/share/applications/zwriter.desktop (Exec points at the given
# binary) and the icons under ~/.local/share/icons/hicolor. A .deb install
# does not need this. On GNOME/Wayland the dock matches the running window's
# app id ("zwriter") to this file, so the file name must stay zwriter.desktop.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
apps="$data/applications"
icons="$data/icons/hicolor"

refresh() {
    command -v update-desktop-database >/dev/null && update-desktop-database "$apps" 2>/dev/null || true
    command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t -f "$icons" 2>/dev/null || true
}

if [[ "${1:-}" == "--remove" ]]; then
    rm -f "$apps/zwriter.desktop" "$icons/scalable/apps/zwriter.svg"
    for s in 16 24 32 48 64 128 256 512; do rm -f "$icons/${s}x${s}/apps/zwriter.png"; done
    refresh
    echo "removed zwriter desktop entry and icons"
    exit 0
fi

bin="$(realpath "${1:-$root/build/zwriter}")"
[[ -x "$bin" ]] || { echo "not an executable: $bin (build first, or pass the path)" >&2; exit 1; }

mkdir -p "$apps" "$icons/scalable/apps"
for s in 16 24 32 48 64 128 256 512; do
    mkdir -p "$icons/${s}x${s}/apps"
    install -m 644 "$root/assets/icons/zwriter-$s.png" "$icons/${s}x${s}/apps/zwriter.png"
done
install -m 644 "$root/assets/icons/zwriter.svg" "$icons/scalable/apps/zwriter.svg"

sed "s|^Exec=.*|Exec=\"$bin\" %F|" "$root/assets/linux/zwriter.desktop" > "$apps/zwriter.desktop"
chmod 644 "$apps/zwriter.desktop"
refresh
echo "installed $apps/zwriter.desktop -> $bin"
