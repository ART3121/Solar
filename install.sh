#!/bin/sh

set -eu

SOLAR_RELEASE_VERSION="0.5.0"
SOLAR_REPOSITORY="ART3121/solar"
SOLAR_ARCHIVE="solar-linux-x86_64.tar.gz"
SOLAR_MANIFEST_RELATIVE="share/solar/install-manifest.txt"

prefix=${HOME:+"$HOME/.local"}
requested_version="latest"
uninstall=false
work_directory=""
rollback_required=false
installed_list=""
backup_list=""

usage()
{
    cat <<'EOF'
Usage: install.sh [--version v0.5.0] [--prefix PATH] [--uninstall]

Install the Solar Linux x86_64 release below ~/.local by default.
The installer does not use sudo, install EDA tools, or edit shell files.
EOF
}

fail()
{
    printf 'solar installer: error: %s\n' "$1" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

safe_relative_path()
{
    safe_path_syntax "$1" || return 1
    case "$1" in
        bin/solar|include/solar/*|lib/libsolar_core.a|libexec/solar/*|\
        share/doc/Solar/*|share/bash-completion/completions/solar|\
        share/zsh/site-functions/_solar|\
        share/fish/vendor_completions.d/solar.fish)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

safe_path_syntax()
{
    case "$1" in
        ""|/*|.|..|./*|../*|*/.|*/..|*/./*|*/../*|*//*|\
        *[!A-Za-z0-9._/+-]*)
            return 1
            ;;
    esac
    return 0
}

safe_archive_path()
{
    member_path=${1%/}
    safe_path_syntax "$member_path" || return 1
    case "$member_path" in
        bin|include|include/solar|lib|libexec|libexec/solar|libexec/solar/*|\
        share|share/doc|share/doc/Solar|share/doc/Solar/*|\
        share/bash-completion|share/bash-completion/completions|\
        share/zsh|share/zsh/site-functions|share/fish|\
        share/fish/vendor_completions.d)
            return 0
            ;;
    esac
    safe_relative_path "$member_path"
}

remove_empty_directories()
{
    rmdir "$prefix/include/solar" 2>/dev/null || true
    rmdir "$prefix/include" 2>/dev/null || true
    rmdir "$prefix/libexec/solar/yanc/bin" 2>/dev/null || true
    rmdir "$prefix/libexec/solar/yanc/HDL" 2>/dev/null || true
    rmdir "$prefix/libexec/solar/yanc/Macros" 2>/dev/null || true
    rmdir "$prefix/libexec/solar/yanc/Header" 2>/dev/null || true
    rmdir "$prefix/libexec/solar/yanc" 2>/dev/null || true
    rmdir "$prefix/libexec/solar" 2>/dev/null || true
    rmdir "$prefix/libexec" 2>/dev/null || true
    rmdir "$prefix/share/doc/Solar/licenses" 2>/dev/null || true
    rmdir "$prefix/share/doc/Solar/pt-BR" 2>/dev/null || true
    rmdir "$prefix/share/doc/Solar/assets" 2>/dev/null || true
    rmdir "$prefix/share/doc/Solar" 2>/dev/null || true
    rmdir "$prefix/share/doc" 2>/dev/null || true
    rmdir "$prefix/share/bash-completion/completions" 2>/dev/null || true
    rmdir "$prefix/share/bash-completion" 2>/dev/null || true
    rmdir "$prefix/share/zsh/site-functions" 2>/dev/null || true
    rmdir "$prefix/share/zsh" 2>/dev/null || true
    rmdir "$prefix/share/fish/vendor_completions.d" 2>/dev/null || true
    rmdir "$prefix/share/fish" 2>/dev/null || true
    rmdir "$prefix/share/solar" 2>/dev/null || true
    rmdir "$prefix/bin" 2>/dev/null || true
    rmdir "$prefix/lib" 2>/dev/null || true
    rmdir "$prefix/share" 2>/dev/null || true
}

rollback_installation()
{
    [ -n "$installed_list" ] || return 0
    if [ -f "$installed_list" ]; then
        while IFS= read -r relative; do
            safe_relative_path "$relative" || continue
            rm -f "$prefix/$relative"
        done < "$installed_list"
    fi
    if [ -n "$backup_list" ] && [ -f "$backup_list" ]; then
        while IFS= read -r relative; do
            [ -f "$work_directory/backup/$relative" ] || continue
            mkdir -p "$(dirname "$prefix/$relative")"
            cp -p "$work_directory/backup/$relative" "$prefix/$relative"
        done < "$backup_list"
    fi
    if [ -f "$work_directory/previous-manifest" ]; then
        mkdir -p "$(dirname "$prefix/$SOLAR_MANIFEST_RELATIVE")"
        cp -p "$work_directory/previous-manifest" \
            "$prefix/$SOLAR_MANIFEST_RELATIVE"
    else
        rm -f "$prefix/$SOLAR_MANIFEST_RELATIVE"
    fi
    remove_empty_directories
}

cleanup()
{
    status=$?
    if [ "$rollback_required" = true ] && [ "$status" -ne 0 ]; then
        printf 'solar installer: restoring the previous installation\n' >&2
        rollback_installation
    fi
    if [ -n "$work_directory" ] && [ -d "$work_directory" ]; then
        rm -rf "$work_directory"
    fi
    exit "$status"
}

trap cleanup EXIT HUP INT TERM

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            [ "$#" -ge 2 ] || fail "--version requires a value"
            requested_version=$2
            shift 2
            ;;
        --prefix)
            [ "$#" -ge 2 ] || fail "--prefix requires a value"
            prefix=$2
            shift 2
            ;;
        --uninstall)
            uninstall=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

[ -n "${prefix:-}" ] || fail "HOME is unset; pass --prefix PATH"
case "$prefix" in
    /*) ;;
    *) fail "installation prefix must be an absolute path" ;;
esac
[ "$prefix" != "/" ] || fail "refusing to use / as the installation prefix"
[ "${HOME:-}" != "$prefix" ] || fail "refusing to install directly into HOME"

manifest="$prefix/$SOLAR_MANIFEST_RELATIVE"

if [ "$uninstall" = true ]; then
    [ -f "$manifest" ] || fail "no Solar installer manifest found below $prefix"
    while IFS= read -r relative; do
        safe_relative_path "$relative" || \
            fail "unsafe path in installer manifest: $relative"
    done < "$manifest"
    while IFS= read -r relative; do
        rm -f "$prefix/$relative"
    done < "$manifest"
    rm -f "$manifest"
    remove_empty_directories
    printf 'Solar was removed from %s.\n' "$prefix"
    exit 0
fi

[ "$(uname -s)" = "Linux" ] || fail "prebuilt releases support Linux only"
case "$(uname -m)" in
    x86_64|amd64) ;;
    *) fail "prebuilt releases support x86_64 only" ;;
esac

for command_name in curl tar sha256sum mktemp find sort awk cp mv mkdir dirname; do
    require_command "$command_name"
done

case "$requested_version" in
    latest)
        release_base="https://github.com/$SOLAR_REPOSITORY/releases/latest/download"
        ;;
    v[0-9]*.[0-9]*.[0-9]*)
        release_base="https://github.com/$SOLAR_REPOSITORY/releases/download/$requested_version"
        ;;
    *)
        fail "version must be latest or a tag such as v0.5.0"
        ;;
esac

if [ -n "${SOLAR_INSTALL_BASE_URL:-}" ]; then
    release_base=$SOLAR_INSTALL_BASE_URL
fi

work_directory=$(mktemp -d "${TMPDIR:-/tmp}/solar-install.XXXXXX")
archive_path="$work_directory/$SOLAR_ARCHIVE"
checksums_path="$work_directory/SHA256SUMS"
payload="$work_directory/payload"
installed_list="$work_directory/installed"
backup_list="$work_directory/backups"

curl -fsSL "$release_base/$SOLAR_ARCHIVE" -o "$archive_path"
curl -fsSL "$release_base/SHA256SUMS" -o "$checksums_path"

expected_checksum=$(awk -v file="$SOLAR_ARCHIVE" \
    '$2 == file || $2 == "*" file { print $1; exit }' "$checksums_path")
[ -n "$expected_checksum" ] || fail "SHA256SUMS does not list $SOLAR_ARCHIVE"
actual_checksum=$(sha256sum "$archive_path" | awk '{print $1}')
[ "$actual_checksum" = "$expected_checksum" ] || fail "archive checksum mismatch"

mkdir -p "$payload"
members_path="$work_directory/archive-members"
listing_path="$work_directory/archive-listing"
tar -tzf "$archive_path" > "$members_path"
while IFS= read -r member; do
    [ -n "$member" ] || fail "release archive contains an empty path"
    safe_archive_path "$member" || \
        fail "unsafe path in release archive: $member"
done < "$members_path"
tar -tvzf "$archive_path" > "$listing_path"
awk 'substr($1, 1, 1) != "-" && substr($1, 1, 1) != "d" { exit 1 }' \
    "$listing_path" || fail "release archive contains a link or special file"
tar --no-same-owner -xzf "$archive_path" -C "$payload"
[ -x "$payload/bin/solar" ] || fail "release archive has no bin/solar"

archive_version=$($payload/bin/solar --version 2>/dev/null || true)
case "$archive_version" in
    "solar $SOLAR_RELEASE_VERSION") ;;
    *) fail "unexpected binary version in archive: ${archive_version:-unknown}" ;;
esac
if [ "$requested_version" != "latest" ] && \
   [ "$requested_version" != "v$SOLAR_RELEASE_VERSION" ]; then
    fail "installer $SOLAR_RELEASE_VERSION cannot install $requested_version"
fi

find "$payload" -type f -print | LC_ALL=C sort | while IFS= read -r source; do
    relative=${source#"$payload/"}
    safe_relative_path "$relative" || fail "unsafe path in release: $relative"
    printf '%s\n' "$relative"
done > "$installed_list"
[ -s "$installed_list" ] || fail "release archive is empty"

if [ -e "$prefix/bin/solar" ]; then
    existing_version=$($prefix/bin/solar --version 2>/dev/null || true)
    case "$existing_version" in
        solar\ *) ;;
        *) fail "$prefix/bin/solar is not a recognized Solar installation" ;;
    esac
fi

mkdir -p "$work_directory/backup"
if [ -f "$manifest" ]; then
    cp -p "$manifest" "$work_directory/previous-manifest"
fi
while IFS= read -r relative; do
    target="$prefix/$relative"
    if [ -d "$target" ]; then
        fail "release file conflicts with directory: $target"
    fi
    if [ -e "$target" ] || [ -L "$target" ]; then
        mkdir -p "$(dirname "$work_directory/backup/$relative")"
        cp -p "$target" "$work_directory/backup/$relative"
        printf '%s\n' "$relative" >> "$backup_list"
    fi
done < "$installed_list"

rollback_required=true
while IFS= read -r relative; do
    source="$payload/$relative"
    target="$prefix/$relative"
    mkdir -p "$(dirname "$target")"
    cp -p "$source" "$target.solar-new"
    mv "$target.solar-new" "$target"
done < "$installed_list"

mkdir -p "$(dirname "$manifest")"
cp "$installed_list" "$manifest.solar-new"
mv "$manifest.solar-new" "$manifest"
rollback_required=false

printf 'Solar %s was installed in %s.\n' "$SOLAR_RELEASE_VERSION" "$prefix"
if ! command -v solar >/dev/null 2>&1; then
    printf 'Add %s/bin to PATH, then run: solar doctor --all\n' "$prefix"
else
    printf 'Run solar doctor --all to inspect optional EDA tools.\n'
fi
