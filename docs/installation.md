# Installing Solar

## Supported release platform

The Solar 0.4.6 prebuilt release supports GNU/Linux x86_64 with glibc 2.35 or
newer. Ubuntu 22.04+, Debian 12+, current Fedora, and current Arch Linux are the
release validation targets. ARM64, Alpine/musl, macOS, and Windows require a
future port and do not receive a 0.4.6 binary.

## Recommended installation

```sh
curl -fsSL https://github.com/ART3121/solar/releases/latest/download/install.sh | sh
```

The installer downloads `solar-linux-x86_64.tar.gz` and `SHA256SUMS` from the
same GitHub Release, verifies SHA-256, validates the binary version, and copies
the complete release below `~/.local`. It never uses `sudo`, edits shell files,
or installs external tools.

If needed, add the binary directory to your shell environment:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Persist that line yourself in the startup file appropriate for your shell.

## Shell completion

Release and CMake installations include completion definitions for Bash, Zsh,
and Fish. Static commands/options and dynamic tests, profiles, registered
waveforms, and stored build IDs are completed without executing EDA tools.

Bash and Fish normally discover files installed under their standard data
directories. For a Zsh installation below `~/.local`, add the local completion
directory before initializing completion:

```zsh
fpath=(~/.local/share/zsh/site-functions $fpath)
autoload -Uz compinit
compinit
```

Persist those lines yourself when desired. Solar deliberately does not modify
`.bashrc`, `.zshrc`, Fish configuration, or framework-specific directories.
System-prefix installations place `_solar` in the conventional Zsh site
functions directory and normally require no extra `fpath` entry.

## Pin a version or prefix

```sh
curl -fsSL https://github.com/ART3121/solar/releases/download/v0.4.6/install.sh \
  | sh -s -- --version v0.4.6
```

```sh
curl -fsSL https://github.com/ART3121/solar/releases/latest/download/install.sh \
  | sh -s -- --prefix /absolute/writable/prefix
```

An existing recognized Solar installation may be upgraded. If any replacement
fails, the installer restores the previous files. A non-Solar `bin/solar`
causes a refusal instead of being overwritten.

## Manual verification

Download the archive and `SHA256SUMS` from the release page, then run:

```sh
sha256sum -c SHA256SUMS --ignore-missing
tar -xzf solar-linux-x86_64.tar.gz
```

When GitHub CLI is available, an immutable published release can also be
verified with:

```sh
gh release verify v0.4.6 --repo ART3121/solar
```

## External EDA tools

Solar installs only its own files and the private YANC 5.2 bundle. Install
Icarus Verilog and Yosys through your distribution when using the default
Verilog flow. Verilator, Python+cocotb, GTKWave, and Surfer are optional.
cocotb 2.x requires Verilator 5.036 or newer; distribution packages older than
that remain useful for Solar's native Verilator simulation but cannot run the
cocotb 2.x driver.

Examples:

```sh
# Debian and Ubuntu
sudo apt install iverilog yosys verilator gtkwave

# Fedora
sudo dnf install iverilog yosys verilator gtkwave

# Arch Linux
sudo pacman -S iverilog yosys verilator gtkwave
```

Package availability varies by distribution. Solar does not execute these
commands. Confirm the actual environment with:

```sh
solar doctor --all
```

## Build from source

Source builds additionally require CMake 3.20+, a C17 compiler, GNU Make, Flex,
and Bison because the bundled YANC toolchain is compiled with Solar.

```sh
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-release
cmake --install build-release
```

## Uninstall

Use the installed copy of the installer:

```sh
"$HOME/.local/libexec/solar/install.sh" --uninstall
```

For another prefix, pass the same `--prefix`. Uninstall reads Solar's ownership
manifest, removes only listed files, and preserves unrelated files and
non-empty directories.
