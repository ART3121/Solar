# Solar project format

## Scope

A Solar project is identified by `solar.toml`. Solar implements the documented
subset below; it is not a complete TOML parser and does not claim TOML
conformance.

Paths written in the manifest are relative to its directory. Solar performs no
shell glob expansion and does not interpret path values through a shell.
Format-2 Verilog projects may instead use conventional automatic discovery.

The manifest must be a readable regular file no larger than 16 MiB. This limit
keeps malformed or accidental input from consuming unbounded parser memory;
normal Solar projects are expected to be substantially smaller.

Automatically indexed Verilog/SystemVerilog files below `rtl/` and `tb/` are
limited to 64 MiB each. Explicit paths remain available for exceptional files
that do not need Solar's module/testbench inference.

## Format marker

New manifests declare:

```toml
[solar]
format = 2
```

Only format `2` is accepted when the marker is present. If `[solar].format` is
absent, Solar treats the manifest as format 1 and normalizes it in memory. See
[Migration from v1](migration-v1-to-v2.md).

## Verilog format 2

```toml
[solar]
format = 2

[project]
name = "counter"
language = "verilog"
default_test = "basic"
default_profile = "debug"

[sources]
rtl = ["rtl/counter.v", "rtl/counter_control.v"]
include_dirs = ["rtl/include"]
defines = ["DATA_WIDTH=8"]

[simulation]
backend = "iverilog"

[synthesis]
backend = "yosys"
top = "counter"

[[profile]]
name = "debug"
defines = ["DEBUG"]
include_dirs = ["profiles/debug/include"]

[[profile]]
name = "release"
defines = ["SYNTHESIS"]

[[test]]
name = "basic"
top = "counter_tb"
sources = ["tb/counter_tb.v"]
include_dirs = ["tb/include"]
defines = ["TEST_BASIC"]
waveform = "counter_basic.vcd"

[[test]]
name = "overflow"
top = "counter_overflow_tb"
sources = ["tb/counter_overflow_tb.v"]
defines = ["TEST_OVERFLOW"]
waveform = "counter_overflow.vcd"
```

Required common values are `[project].name`, `[project].language`,
`[simulation].backend`, and `[synthesis].backend`. A Verilog
project also needs at least one effective RTL source. Tests may come from
explicit entries, automatic discovery, or both; an RTL-only project with no
testbench is valid for checking, RTL elaboration, and synthesis. `build sim`
still requires a selectable test and reports an actionable error when none
exists.

`[synthesis].top` is required after discovery. Solar indexes module declarations
and common module instantiations without invoking an external HDL frontend. The
single RTL module not instantiated by another RTL module is the inferred top.
When several roots exist, one exact match with `[project].name` resolves the
selection; otherwise the hierarchy is ambiguous and Solar reports the candidate
names. Explicit tops remain effective during ordinary loading and checking.

`default_test` and `default_profile` are optional, but must reference effective
items after parsing and discovery. If a test omits `waveform`, Solar derives
`<test-name>.vcd` during format
normalization as the expected output name. Each Verilog test otherwise requires
`name`, `top`, and at least one source. A simulator exit of zero remains success
when that expected VCD/FST is absent; Solar warns and does not publish or
register a waveform. A cocotb-driven test replaces that Verilog testbench source
with a validated Python module path, as described below.

### Verilator and cocotb tests

`[simulation].backend` accepts `iverilog` or `verilator`. A normal Verilog
testbench needs no extra field; with Verilator, `.vcd` selects VCD tracing and
`.fst` selects FST tracing.

A Python-driven test uses the cocotb driver:

```toml
[simulation]
backend = "verilator"

[[test]]
name = "python"
top = "adder"
driver = "cocotb"
cocotb = "tb/test_adder.py"
waveform = "adder.fst"
```

`cocotb` is a safe project-relative `.py` path. `sources` may be omitted
because `top` names the DUT, while global RTL still comes from `[sources].rtl`
or discovery. cocotb tests currently require the Verilator backend and cocotb
2.x. Solar exposes no arbitrary Python, cocotb, or Verilator flags.

The Verilator backend cannot run when the project's absolute path contains
whitespace because Verilator's generated GNU Make build rejects that workspace.
Solar diagnoses this before compilation. Use a whitespace-free project path or
select `iverilog`; source and include paths remain separate safe arguments.

### Conventional automatic discovery

On every format-2 Verilog project load, Solar recursively enumerates `.v` and
`.sv` files
below `rtl/` as authored RTL and below `tb/` as testbench sources. Results are
sorted, symlinks and hidden entries are ignored, and duplicates are removed.
This is filesystem enumeration, not shell globbing.

For undeclared testbench files, Solar treats modules not instantiated by another
module below `tb/` as testbench roots. A unique root is selected; when a file has
several roots, an exact match with the file stem resolves the selection. The
file stem becomes the test name. A literal `$dumpfile("name.vcd")` supplies its
waveform; otherwise `<test>.vcd` is used. Ambiguous files require correction or
an explicit unmanaged test. Instantiated modules and files without an
identifiable top are shared as support sources.

Discovery records the literal waveform path present when it runs. After
changing the name or directory in `$dumpfile(...)`, run `solar scan` again so
the managed `[[test]].waveform` remains synchronized. `solar config set --test`
only selects an existing test and never rescans a testbench.

`solar scan` writes the conventional inventory to managed `[sources].rtl`,
`[synthesis].top`, and `[[test]]` fields. Invoking it explicitly authorizes
recalculation of an existing conventional synthesis top and managed test tops.
It preserves explicit paths outside `rtl/` and unmanaged tests, validates a
candidate, and atomically replaces the manifest only on success. Scan is
idempotent and not required for builds because project loading performs
discovery in memory. An unambiguous format-1 project is migrated to format 2;
ambiguous changes leave the original file untouched. If the last managed test
disappears, scan removes a `default_test` that referred to that managed test.
Defaults for explicit unmanaged tests remain user-owned.

For format-2 CMM projects, `solar scan` instead locates exactly one regular
`.cmm` file below `software/` (or an existing safe explicitly configured path),
without following symlinks. It reads the active `#PRNAME` outside comments and
strings, then synchronizes `[project].name`, `[compiler].source`,
`[compiler].processor`, and `[synthesis].top`. The exact `#PRNAME` identifier is
the source of truth. Zero or multiple sources and missing, duplicate,
malformed, or unsafe directives are errors; the original manifest remains
unchanged. C++ and direct Assembly configuration is not changed by scan.

## YANC format 2

```toml
[solar]
format = 2

[project]
name = "sapho-counter"
language = "cmm"
default_test = "generated"
default_profile = "debug"

[compiler]
backend = "yanc"
source = "software/counter.cmm"
processor = "counter"
frequency_mhz = 100
simulation_clocks = 100000

[yanc]
# root = "/opt/yanc"
diagnostics = "pt"

[simulation]
backend = "iverilog"

[synthesis]
backend = "yosys"
top = "counter"

[[profile]]
name = "debug"
defines = ["YANC_DEBUG"]
```

Supported languages are `verilog`, `cmm`, `cpp`, and `asm`. The latter three
require `[compiler].backend = "yanc"`, one source, one processor, positive
frequency/clocks, and the implicit `generated` test. Explicit `[[test]]` tables
are rejected for YANC projects in the current single-processor flow.

Source extensions are validated as follows:

| Language | Accepted extension |
| --- | --- |
| `cmm` | `.cmm` |
| `cpp` | `.cpp`, `.cc`, `.cxx` |
| `asm` | `.asm` |

`[compiler].include_dirs` is forwarded to the YANC C++ frontend. `[yanc].root`
is optional. `[yanc].diagnostics` accepts `pt` or `en` and defaults to `pt` for
YANC languages. Solar applies the locale option only to tools that support it.

The configured processor uses letters, numbers, and underscores, beginning
with a letter or underscore. It becomes a safe generated path and module/file
name. For CMM, project validation also requires this value to match the source
`#PRNAME`; run `solar scan` after changing the directive or source path.

## Supported syntax

The subset accepts:

- ordinary tables `[solar]`, `[project]`, `[sources]`, `[simulation]`,
  `[synthesis]`, `[compiler]`, and `[yanc]`;
- repeated array tables `[[profile]]` and `[[test]]`;
- double-quoted strings;
- positive decimal integers for format, frequency, and simulation clocks;
- arrays of double-quoted strings on one or multiple lines;
- blank lines, whitespace, and `#` comments outside strings;
- string escapes `\n`, `\t`, `\\`, and `\"`.

Unknown sections/keys, duplicate scalar assignments, malformed values,
single-quoted/multiline strings, dotted keys, inline tables, booleans, dates,
and unsupported TOML constructs are errors. Unknown configuration never
silently changes backend behavior.

## Field reference

| Table/key | Type | Meaning |
| --- | --- | --- |
| `[project].name` | string | Non-empty display name without control characters. |
| `[project].language` | string | `verilog`, `cmm`, `cpp`, or `asm`. |
| `[project].default_test` | string | Optional declared test name. |
| `[project].default_profile` | string | Optional declared profile name. |
| `[sources].rtl` | string array | Optional explicit RTL, merged with conventional discovery. |
| `[sources].include_dirs` | string array | Global Verilog include directories. |
| `[sources].defines` | string array | Global Verilog macros. |
| `[simulation].backend` | string | Required; `iverilog` or `verilator`. |
| `[synthesis].backend` | string | Required; currently `yosys`. |
| `[synthesis].top` | string | Explicit top, or inferred for one-module/name-matched Verilog; defaults from the YANC processor. |
| `[[profile]].name` | string | Required unique safe name. |
| `[[profile]].include_dirs` | string array | Optional profile Verilog includes. |
| `[[profile]].defines` | string array | Optional profile Verilog macros. |
| `[[test]].name` | string | Required unique safe name. |
| `[[test]].top` | string | Required simple Verilog testbench top. |
| `[[test]].sources` | string array | Required for explicit tests; discovered tests are constructed in memory. |
| `[[test]].include_dirs` | string array | Optional test-specific includes. |
| `[[test]].defines` | string array | Optional test-specific macros. |
| `[[test]].waveform` | string | Optional expected safe path below the test directory; defaults to `<name>.vcd`; its absence after a passing run is a warning. |
| `[[test]].driver` | string | Optional `verilog` (default) or `cocotb`. |
| `[[test]].cocotb` | string | Required project-relative Python module for the cocotb driver. |
| `[compiler].backend` | string | Must be `yanc` for compiler languages. |
| `[compiler].source` | string | One manifest-relative main source. |
| `[compiler].processor` | string | Safe YANC processor/module name. |
| `[compiler].frequency_mhz` | integer | Positive clock frequency. |
| `[compiler].simulation_clocks` | integer | Positive simulation cycle count. |
| `[compiler].include_dirs` | string array | C++ frontend include directories. |
| `[yanc].root` | string | Optional absolute YANC root without `.` or `..` components. |
| `[yanc].diagnostics` | string | `pt` or `en`; default `pt`. |

## Command-managed settings

Format-2 projects can update three common settings without editing TOML:

```bash
solar config set --name "benchmark-run"
solar config set --top counter
solar config set --test overflow
```

The options can appear together in one transaction. They map exactly to
`[project].name`, `[synthesis].top`, and `[project].default_test`. The command
does not rename modules, test tables, source files, directories, or generated
artifacts. In particular, `--test` receives a `[[test]].name`, not a
testbench filename or module top. A later `solar scan` recalculates
`[synthesis].top` for a conventional Verilog hierarchy.

Solar preserves text outside those keys, writes a candidate below
`.solar/tmp/config/`, parses it, applies normal discovery and semantic
validation, and only then replaces `solar.toml`. An invalid test name, unsafe
top, parse error, or I/O failure leaves the original manifest unchanged.
Format-1 projects must run `solar scan` before using this command.

## Names, defines, and paths

Test/profile names may contain letters, digits, `_`, `-`, and `.`, but cannot
be empty, begin with `.`, contain `..`, `/`, or another unsafe character.
Duplicate names are rejected.

Simple Verilog top identifiers begin with a letter or underscore and continue
with letters, numbers, `_`, or `$`. Solar validates names lexically; Icarus and
Yosys validate actual module declarations.

Defines use `NAME` or `NAME=value`. The macro name is identifier-like and the
optional value contains only ASCII letters, numbers, and underscores. Empty
entries, leading `-`, control characters, whitespace, and backend-option syntax
are rejected. Solar constructs backend arguments itself, so a define cannot
inject arbitrary flags.

Every source/include path is non-empty, relative, free of empty, `.` and `..`
components, and outside reserved `.solar/`. Source files must be readable
regular files; include entries must be directories. Spaces are supported.

A format-2 test waveform is a safe relative path below its isolated artifact
directory. Solar creates safe parent directories; absolute paths and `..` are
rejected. The field names an optional artifact, not a pass condition: only a
waveform actually produced by the current run is published.

## Effective configuration

Simulation combines includes/defines as global, selected profile, then selected
test. Synthesis combines only global and selected profile values. Exact
duplicates are removed while preserving first occurrence and input ownership.
See [Profiles](profiles.md).

## Format-1 compatibility

The legacy tables remain accepted only when `[solar].format` is absent:

```toml
[project]
name = "counter"
top = "counter"
language = "verilog"

[sources]
rtl = ["rtl/counter.v"]
testbench = ["tb/counter_tb.v"]

[simulation]
backend = "iverilog"
top = "counter_tb"
waveform = ".solar/build/sim/counter.vcd"

[synthesis]
backend = "yosys"
top = "counter"
```

Solar converts this to one implicit `default` test in memory and prints a
migration warning. It never rewrites the manifest.

## Ownership

Initialize `SolarConfig` before parsing and release it with
`solar_config_free()`. The configuration owns parsed strings, arrays, profiles,
and tests. Effective configuration owns copied lists and has an independent
cleanup function. Finder functions return borrowed pointers into `SolarConfig`.

## Built-in templates

`solar init` defaults to a functional format-2 Verilog project whose manifest
uses discovery instead of counter-specific source paths. The public templates
are:

```text
solar init --template verilog
solar init --template sapho
```

The `sapho` template creates the standard CMM project. C++ and Assembly remain
supported through explicit manifests and the repository examples, but do not
have separate public initializer names. Initialization refuses to overwrite
any target file. The SAPHO template contains only a manifest and a small source
example traced to official YANC v5.2 fixtures; initialization does not invoke
the toolchain.

The Verilog template creates `rtl/`, `tb/`, `sim/`, and `synth/`. The SAPHO
template creates `software/`, `hardware/`, and `simulation/`. Public output directories contain `.gitkeep`
files and matching `.gitignore` rules; `.solar/` is created only by an
operation that needs internal state.
