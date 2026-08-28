#!/bin/sh
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
updater_dir=$(CDPATH='' cd -- "$script_dir/../.." && pwd)
output_dir=${1:-"$updater_dir/dist"}
cli=${HC32_WINDOWS_CLI:-"$updater_dir/target/x86_64-pc-windows-gnu/release/hc32-updater.exe"}
gui=${HC32_WINDOWS_GUI:-"$updater_dir/target/x86_64-pc-windows-gnu/release/hc32-updater-gui.exe"}
version=$(sed -n 's/^version = "\([^"]*\)"/\1/p' "$updater_dir/Cargo.toml" | head -n 1)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM

test -n "$version"
file "$cli" | grep -q 'PE32+ executable'
file "$gui" | grep -q 'PE32+ executable'
install -m755 "$cli" "$stage/hc32-updater.exe"
install -m755 "$gui" "$stage/hc32-updater-gui.exe"
install -m644 "$script_dir/README.txt" "$stage/README.txt"
(cd "$stage" && sha256sum hc32-updater.exe hc32-updater-gui.exe >SHA256SUMS)
mkdir -p "$output_dir"
archive="$output_dir/hc32-updater_${version}_windows-x64.zip"
(cd "$stage" && cmake -E tar cf "$archive" --format=zip README.txt SHA256SUMS hc32-updater.exe hc32-updater-gui.exe)
cmake -E tar tf "$archive"
printf '%s\n' "$archive"
