#!/bin/sh
set -eu

test "$#" -eq 3
cli=$1
gui=$2
output_dir=$3
osslsigncode=${OSSLSIGNCODE:-osslsigncode}
: "${HC32_WINDOWS_SIGN_PFX:?set HC32_WINDOWS_SIGN_PFX to the external PKCS#12 file}"
: "${HC32_WINDOWS_SIGN_PASSWORD_FILE:?set HC32_WINDOWS_SIGN_PASSWORD_FILE to a private password file}"
: "${HC32_WINDOWS_TIMESTAMP_URL:?set HC32_WINDOWS_TIMESTAMP_URL to an RFC3161 service}"

test -r "$HC32_WINDOWS_SIGN_PFX"
test -r "$HC32_WINDOWS_SIGN_PASSWORD_FILE"
mkdir -p "$output_dir"
for input in "$cli" "$gui"; do
    output="$output_dir/$(basename "$input")"
    "$osslsigncode" sign \
        -pkcs12 "$HC32_WINDOWS_SIGN_PFX" \
        -readpass "$HC32_WINDOWS_SIGN_PASSWORD_FILE" \
        -h sha256 \
        -ts "$HC32_WINDOWS_TIMESTAMP_URL" \
        -n "HC32 Firmware Updater" \
        -in "$input" \
        -out "$output"
    if test -n "${HC32_WINDOWS_SIGN_CA_FILE:-}"; then
        "$osslsigncode" verify -CAfile "$HC32_WINDOWS_SIGN_CA_FILE" -in "$output"
    else
        "$osslsigncode" verify -in "$output"
    fi
done
sha256sum "$output_dir/hc32-updater.exe" "$output_dir/hc32-updater-gui.exe"
