# YANC integration

## Status

YANC is the compiler toolchain used for the SAPHO soft processor. Solar vendors
its MIT-licensed sources, builds them from a private staging copy, and installs
the resulting tools and read-only HDL/macros/headers with Solar. Solar never
downloads YANC at runtime and never writes into the vendored snapshot.

As of 2026-07-26, Solar's format-2 model, Compiler Service, YANC resolver, YANC
execution backend, atomic publication, generated Icarus test, and generated
Yosys synthesis are implemented. Generated simulations also prepare an
Assembly-decoded GTKWave layout. CMM, C++, and Assembly have each passed a
complete real flow through Solar with the official YANC v5.2 release from a
project root containing spaces. This is concrete integration evidence, not a
stable-release or broad language-compatibility claim.

The bundled snapshot and external comparison were performed against:

- upstream `main` commit
  [`ea6135af18735e66a2bd23445749d6a244990fcd`](https://github.com/nipscernlab/yanc/commit/ea6135af18735e66a2bd23445749d6a244990fcd);
- the published [YANC v5.2 release](https://github.com/nipscernlab/yanc/releases/tag/v5.2),
  whose tag points to commit
  `792521b699a1f1e6f23a258653a9cf55e251abe1`.

The five compiler executables in that revision report version `5.2`. The
version output is not uniform, so Solar must preserve the complete first line
instead of parsing one fixed prefix.

## Build and discovery

The Solar CMake build requires Make, Flex, and Bison and builds YANC as the
mandatory `solar_yanc_toolchain` target. Grammar-generated C files are produced
only below `build/yanc-stage`; the source snapshot remains unchanged. The
bundle is installed below `libexec/solar/yanc`. Solar never runs upstream
`Scripts/setup.sh`.

The resolver uses this precedence:

1. `SOLAR_YANC_ROOT`;
2. `[yanc].root` from `solar.toml`;
3. the bundle adjacent to the running Solar executable;
4. YANC executables found on `PATH`, followed by safe root derivation.

An explicit root is selected and validated rather than silently replaced by a
lower-precedence candidate. `SOLAR_YANC_ROOT` and `[yanc].root` must be absolute
paths. For `PATH` discovery, Solar must resolve
symlinks and accept a derived root only when the executable directory and all
required sibling assets form a recognized layout. Solar must not change the
process-wide `PATH`.

The first two forms are development/test overrides. Normal users need no YANC
root configuration. `SOLAR_TEST_YANC_ROOT` selects an alternate root only for
the integration test executable.

The pinned upstream commit is carried with narrowly scoped integration,
Verilator-compatibility, and safety patches. These include the internal CMM
flag rename from `fsqrt` to `use_fsqrt`, checked output streams, bounded
complex-literal parsing, deterministic simulator metadata, and fail-fast
allocation handling. The verified CLI pipeline and generated-artifact contract
are unchanged. The patch classes are listed in `THIRD_PARTY_NOTICES.md`.

### Source checkout layout

A built checkout has this relevant structure:

```text
yanc/
|-- bin/
|   |-- cmmcomp
|   |-- cpppp
|   |-- cppcomp
|   |-- appcomp
|   |-- asmcomp
|   `-- gen_gtkw             # optional presentation helper
|-- HDL/
`-- Compilers/
    |-- CMMComp/Includes/   # CMM and assembler macros
    `-- CPPComp/Includes/   # C++ header shims
```

The upstream checkout does not necessarily contain `bin/` until the user has
built YANC. Solar reads this tree but never writes into it.

### Release layout

The official Linux v5.2 archive contains:

```text
yanc/
|-- bin/
|-- HDL/
|-- Macros/
`-- Header/
```

`Macros/` corresponds to `Compilers/CMMComp/Includes/` in a checkout, and
`Header/` corresponds to `Compilers/CPPComp/Includes/`. Release archives do not
contain `Scripts/`. This layout is defined by the upstream
[release workflow](https://github.com/nipscernlab/yanc/blob/ea6135af18735e66a2bd23445749d6a244990fcd/.github/workflows/release.yml#L57-L77).

## Project configuration

A YANC project uses one source and one processor:

```toml
[project]
language = "cmm" # or "cpp" or "asm"
default_test = "generated"

[compiler]
backend = "yanc"
source = "software/processor.cmm"
processor = "processor"
frequency_mhz = 100
simulation_clocks = 100000

[yanc]
# root = "/opt/yanc"
diagnostics = "pt" # or "en"
```

`[compiler].include_dirs` is used by the YANC C++ preprocessor. Profile defines
belong to the generated Verilog simulation and synthesis stages; they are not
forwarded to YANC compilers. Arbitrary toolchain flags are not accepted.

The source must agree with the configured processor name. CMM and Assembly use
the `#PRNAME` directive, while C++ may use `#pragma yanc prname`. These source
directives can determine the names recorded by the assembler. A backend must
verify that the produced assembly and `app_log.txt` name match
`[compiler].processor`; it must not search for an unexpected name and publish
it as success.

For CMM projects, `solar scan` can synchronize that identity before a build.
It requires one `.cmm` source, reads `#PRNAME`, and atomically updates the
project name, source path, processor, and synthesis top only after validating
the complete candidate manifest. It does not execute YANC. C++ and direct
Assembly source identities remain explicit in `solar.toml`.

## Verified command lines

Every invocation is an executable plus an argument vector. The examples below
show logical arguments; they must never be joined into a shell command.

### CMM

YANC expects the CMM input name inside `<project>/Software`, not an arbitrary
source path. Solar therefore has to copy the source into its private staging
tree as `Software/<processor>.cmm` and pass only that basename:

```text
cmmcomp -pt \
  -i <processor>.cmm \
  -n <processor> \
  -p <staged-project> \
  -m <macros> \
  -t <temp>

appcomp -pt -i <staged-project>/Software/<processor>.asm -t <temp>

asmcomp -pt \
  -i <staged-project>/Software/<processor>.asm \
  -p <staged-project> \
  -d <HDL> \
  -m <macros> \
  -t <temp> \
  -f <frequency_mhz> \
  -c <simulation_clocks>
```

Use `-en` instead of `-pt` when `[yanc].diagnostics = "en"`.

### C++

```text
cpppp \
  -i <source.cpp> \
  -o <temp>/preprocessed.cpp \
  -I <YANC headers> \
  -I <source directory> \
  -I <each configured include directory>

cppcomp \
  -i <temp>/preprocessed.cpp \
  -n <processor> \
  -p <staged-project> \
  -t <temp>

appcomp -pt -i <staged-project>/Software/<processor>.asm -t <temp>
asmcomp -pt -i <assembly> -p <staged-project> -d <HDL> -m <macros> \
  -t <temp> -f <frequency_mhz> -c <simulation_clocks>
```

Each `-I` and its directory are separate arguments. `cpppp` is the YANC
frontend preprocessor; Solar must not substitute the system C++ preprocessor.
Neither `cpppp` nor `cppcomp` accepts `-pt` or `-en`.

### Assembly

The requested Solar flow stages the original source without modifying it and
would otherwise invoke:

```text
appcomp -pt -i <staged-project>/Software/<processor>.asm -t <temp>
asmcomp -pt -i <assembly> -p <staged-project> -d <HDL> -m <macros> \
  -t <temp> -f <frequency_mhz> -c <simulation_clocks>
```

Upstream documents Assembly mainly as an intermediate representation.
`asmcomp` expects `cmm_log.txt` and a PC/source map normally emitted by
`cmmcomp` or `cppcomp`. In YANC v5.2, a direct `appcomp -> asmcomp` run without
`cmm_log.txt` terminated with `SIGSEGV` because the assembler reads that file
without checking a failed `fopen`.

Solar implements a bounded compatibility bridge after successful `appcomp`:

1. Open `app_log.txt` without following a symlink.
2. Parse and validate its positive `n_ins` instruction count.
3. Write staging-only `cmm_log.txt` containing `num_ins <count>`.
4. Write `pc_<processor>_mem.txt` with exactly `<count>` neutral 20-bit entries.
5. Invoke `asmcomp` with the same documented argv as the other languages.

The bridge is derived from upstream output and writes only to Solar staging. It
passed real compile, Icarus/VVP/VCD, and Yosys execution. The neutral PC map is
not a fabricated high-level debug map: it deliberately provides no correlation
to CMM/C++ source lines because no high-level frontend ran.

## Upstream outputs

A successful CMM, C++, or bridged Assembly pipeline produces these files before Solar
normalization:

```text
<staged-project>/
|-- Software/
|   `-- <processor>.asm
|-- Hardware/
|   |-- <processor>.v
|   |-- <processor>_data.mif
|   `-- <processor>_inst.mif
`-- Simulation/              # runtime input_N.txt/output_N.txt location

<temp>/
|-- <processor>_tb.v
|-- app_log.txt
|-- cmm_log.txt
|-- pc_<processor>_mem.txt
|-- trad_cmm.txt
`-- trad_opcode.txt
```

`cpppp` also produces `preprocessed.cpp`. The processor top is
`<processor>` and the generated testbench top is `<processor>_tb`.

A zero exit status is not sufficient. The Compiler Service must validate at
least the assembly, generated processor Verilog, both MIF files, and generated
testbench before publication. Logs and useful sidecars should be preserved as
auxiliary artifacts.

The useful normalized Solar outputs are published below:

```text
software/
`-- <processor>.asm       # generated by CMM/C++ frontends
hardware/
|-- <processor>.v
|-- <processor>_data.mif
`-- <processor>_inst.mif
simulation/
`-- <processor>_tb.v
```

Per-stage stdout/stderr logs use
`.solar/logs/yanc/<processor>/<stage>.stdout.log` and
`<stage>.stderr.log`. Native execution stays under
`.solar/tmp/yanc/<processor>/`. The generated Assembly is published only for
CMM/C++; direct Assembly input remains a user-owned source. The program-counter
map remains internal. Reproducibility metadata is stored at
`.solar/state/yanc/<processor>/build-info.txt`. This publication layer is a
Solar contract and is not the native YANC output layout.

YANC v5.2 also ships a generic HDL module named `processor`. When the configured
processor is literally named `processor`, that module name collides with the
generated wrapper. Solar resolves the collision only in its published build:
it renames the generic core to `solar_yanc_processor_core` and updates the
wrapper instantiation. The YANC installation and private source project remain
unchanged.

`build-info.txt` records Solar/YANC versions, resolved root, language, source,
processor, frequency, clocks, executable paths, and UTC timestamp. It contains
no environment dump and is not used as a cache.

## Simulation and synthesis consumers

The upstream single-processor Icarus scripts compile:

```text
<temp>/<processor>_tb.v
<staged-project>/Hardware/<processor>.v
HDL/addr_dec.v
HDL/instr_dec.v
HDL/processor.v
HDL/core.v
HDL/ula.v
```

They do not include `myFIFO.v` in the observed single-processor flow. VVP runs
with `<temp>` as its working directory because generated RTL reads
`pc_<processor>_mem.txt` relatively. The generated testbench writes
`<processor>_tb.vcd` in that working directory. MIFs must be available at the
paths referenced by the generated processor RTL.

Yosys receives the generated processor RTL and the required HDL modules, never
the generated testbench. Paths containing spaces passed through Solar's real
Icarus and Yosys flows when kept as distinct argv entries and properly quoted
inside the Yosys script.

Icarus and Yosys must consume a structured generated-artifact result from the
Compiler Service. They must not scan a YANC workspace or infer filenames on
their own.

### GTKWave Assembly presentation

The generated VCD already contains a changing numeric `valr2` signal. Showing
the corresponding instruction text requires the `trad_opcode.txt` emitted by
`asmcomp` and a GTKWave save layout. After publishing a successful generated
waveform, Solar invokes the optional YANC helper through the shared runner:

```text
gen_gtkw --assembly-only \
  simulation/<processor>_tb.vcd \
  <staging>/layout.gtkw \
  .solar/state/waveforms/generated/trad_opcode.txt \
  .solar/state/waveforms/generated/layout.gtkw
```

Solar stages the translation and layout together, validates the helper output,
and atomically replaces `.solar/state/waveforms/generated/`. The public
`simulation/` directory still receives only the registered VCD/FST. The
default layout contains only the decoded Assembly track; CMM/C++ source-line,
variables, I/O, and flags remain raw VCD signals that users may add manually.

`solar view generated` launches GTKWave with the project root as its working
directory and applies `-a <layout.gtkw>`. Surfer continues to open the raw
waveform. A missing or incompatible `gen_gtkw` in an explicitly selected
external root removes stale layout state, keeps the simulation successful, and
produces an actionable warning. The bundled YANC always builds and installs the
compatible helper. Formatter logs use the selected test log directory.

## Publication constraint

The inspected `asmcomp` embeds the path passed through `-p` into generated RTL
for the instruction/data MIFs and into the testbench for simulation input and
output files. A simple rename from a temporary staging directory to the public
build directory therefore leaves stale paths in otherwise valid artifacts.

Atomic publication must account for this behavior explicitly. The backend must
normalize only the verified generated path fields, validate the normalized
outputs, and then replace the previous build. A failed pipeline must leave the
previous valid build untouched. Keeping the temporary directory forever is not
an acceptable substitute for a valid published build.

## Diagnostics and safety

- Invoke YANC only through the shared POSIX runner; do not use a shell,
  `system()`, `popen()`, or an upstream runner script.
- Pass paths, includes, frequency, clocks, and locale as separate arguments.
- Pass `-pt` or `-en` only to `cmmcomp`, `appcomp`, and `asmcomp`.
- Validate the processor name before using it as a directory or artifact name.
- Write staging data, logs, and published artifacts only below project-local
  `.solar/`.
- Treat the YANC root and its HDL/macro/header assets as read-only.
- Stop after the first failed compiler stage, preserve that stage's original
  stdout/stderr, and report its log path.
- Validate every required artifact after `asmcomp`, even when it exits zero.
- Generated VCD/FST is opened only through `solar view`. Solar bundles
  `gen_gtkw` for the optional GTKWave Assembly layout but does not bundle or
  invoke `comp2gtkw`; neither helper launches a GUI during a build.

## Observed documentation differences

The upstream source and Linux runner scripts are authoritative where the README
differs:

- The README standalone example places `<processor>_tb.v` in `Simulation/`;
  `asmcomp` and `Scripts/single_proc.sh` actually put it in the temp directory.
- The generated `$dumpfile` name is `<processor>_tb.vcd`. Running VVP with
  `-fst` can change the file contents to FST, but does not change that filename
  to the `.fst` path shown by the README example.
- `cmmcomp -i` is a basename inside `<proc-dir>/Software`, despite the
  standalone example looking like a general input path.
- The README says every short option has a long form. `cpppp` accepts short
  `-i`, `-o`, and `-I` forms only. Its `--help` and `cppcomp --help` also exit
  with status 1, while their `--version` operations exit successfully.
- The README describes six built binaries but lists and packages seven. Solar
  requires the five compiler executables for hardware generation and treats
  `gen_gtkw` as an optional presentation capability in external roots.
- Direct Assembly lacks the frontend sidecars required by `asmcomp`; Solar's
  tested staging bridge supplies the minimal instruction metadata and a neutral
  PC map derived from `appcomp`, while preserving this upstream divergence.

On the validation host, the default source build at upstream `main` failed with
GCC 16.1.1 because YANC's internal `fsqrt` variable collided with a libc math
declaration. Building with strict C17 plus `_POSIX_C_SOURCE=200809L` succeeded,
and the official v5.2 Linux binaries ran successfully. This is an observed
build-from-source compatibility issue, not evidence that Solar should patch or
build YANC at runtime.

## Troubleshooting

`YANC toolchain status: not found`:

- install or build YANC separately;
- set `SOLAR_YANC_ROOT` to the directory containing `bin/` and `HDL/`, or set
  `[yanc].root` in the project;
- verify that the selected checkout/release layout contains the language's
  required executables and assets.

`YANC stage "<name>" failed`:

- inspect `.solar/logs/yanc/<processor>/<name>.stderr.log` first;
- Solar stops before later stages and preserves every stage log already written;
- check that the configured source processor directive agrees with
  `[compiler].processor`.

`expected artifact missing` after exit status zero:

- inspect the stage logs and private staging tree under `.solar/tmp/yanc/`;
- Solar intentionally rejects a zero-exit pipeline without assembly, processor
  Verilog, both MIF files, testbench, and required runtime sidecars;
- a previous published build remains separate from the failed candidate.

Assembly compatibility failure mentioning `n_ins`:

- the real YANC v5.2 bridge requires a positive `n_ins` emitted by `appcomp`;
- Solar does not guess an instruction count or accept malformed metadata;
- the neutral PC map enables simulation but is not source-level debug data.

`decoded SAPHO Assembly layout is unavailable`:

- the raw VCD remains a valid successful simulation artifact;
- inspect `.solar/logs/tests/generated/gen_gtkw.stderr.log` when the helper ran;
- use Solar's bundled YANC or provide an external root with a compatible
  `gen_gtkw --assembly-only` implementation.

## Current limitations

- The three language flows were validated with selected official fixtures on
  YANC v5.2; this is not a claim that Solar accepts arbitrary CMM/C++/Assembly.
- Assembly's neutral PC map has no high-level source-line correlation.
- The complete real-toolchain matrix runs against Solar's built bundle;
  `SOLAR_TEST_YANC_ROOT` can explicitly substitute another root.
- Only one generated processor and one implicit `generated` test are supported
  in the current single-processor flow.
- Multi-processor YANC, source-line/variable waveform curation, complex-value
  `comp2gtkw` filtering, and incremental caching remain outside the current
  implementation.
