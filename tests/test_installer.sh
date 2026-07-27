#!/bin/sh

set -eu

installer=$1
solar_binary=$2
work=$(mktemp -d "${TMPDIR:-/tmp}/solar-installer-test.XXXXXX")
payload="$work/payload"
release="$work/release"
prefix="$work/prefix"

cleanup()
{
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

mkdir -p \
    "$payload/bin" \
    "$payload/libexec/solar" \
    "$payload/share/doc/Solar/assets" \
    "$payload/share/bash-completion/completions" \
    "$payload/share/zsh/site-functions" \
    "$payload/share/fish/vendor_completions.d" \
    "$release" \
    "$prefix"

cp "$solar_binary" "$payload/bin/solar"
cp "$installer" "$payload/libexec/solar/install.sh"
printf 'release asset\n' > "$payload/share/doc/Solar/assets/logo.svg"
printf 'bash completion\n' > "$payload/share/bash-completion/completions/solar"
printf 'zsh completion\n' > "$payload/share/zsh/site-functions/_solar"
printf 'fish completion\n' > "$payload/share/fish/vendor_completions.d/solar.fish"

tar -czf "$release/solar-linux-x86_64.tar.gz" \
    -C "$payload" bin libexec share
(
    cd "$release"
    sha256sum solar-linux-x86_64.tar.gz > SHA256SUMS
)

SOLAR_INSTALL_BASE_URL="file://$release" \
    sh "$installer" --version v0.5.0 --prefix "$prefix"

[ -x "$prefix/bin/solar" ]
[ -f "$prefix/share/bash-completion/completions/solar" ]
[ -f "$prefix/share/zsh/site-functions/_solar" ]
[ -f "$prefix/share/fish/vendor_completions.d/solar.fish" ]

sh "$prefix/libexec/solar/install.sh" --prefix "$prefix" --uninstall
[ -z "$(find "$prefix" -mindepth 1 -print -quit)" ]

bad_release="$work/bad-release"
bad_payload="$work/bad-payload"
mkdir -p "$bad_release" "$bad_payload"
printf 'must not escape\n' > "$bad_payload/evil"
tar -czf "$bad_release/solar-linux-x86_64.tar.gz" \
    -C "$bad_payload" --transform='s#^evil$#include/solar/../../evil#' evil
(
    cd "$bad_release"
    sha256sum solar-linux-x86_64.tar.gz > SHA256SUMS
)
if SOLAR_INSTALL_BASE_URL="file://$bad_release" \
    sh "$installer" --version v0.5.0 --prefix "$prefix" >/dev/null 2>&1; then
    printf 'installer accepted a traversal path\n' >&2
    exit 1
fi
[ ! -e "$work/evil" ]

printf 'installer completion paths passed\n'
