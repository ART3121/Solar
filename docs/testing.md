# Testing Solar

## Local workflow

Run the complete debug suite with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Unit tests create temporary directories below `/tmp` and must not modify the
user's home directory or an existing HDL project. Tests for the process runner
and tool adapters use small native helper executables rather than shell scripts.
Integration project variants are also assembled by native C fixtures inside
fresh temporary directories; the repository does not retain generated builds
or rely on mutable static project copies.

## Current coverage

The registered suite covers:

- result and diagnostic mapping;
- format-1 parsing and normalization;
- format-2 Verilog and YANC manifests, repeated profiles/tests, multiline
  arrays, duplicate names, and unsupported formats;
- transactional format-2 configuration edits, combined key updates, escaped
  names, no-op preservation, invalid-default rollback, and format-1 refusal;
- effective include/define ordering, duplicate removal, and input immutability;
- project discovery, source/include validation, and paths containing spaces;
- safe generated-directory creation, publication, and cleanup;
- public artifact registry, portable replacement fallback, and cleanup that
  preserves unregistered public files and legacy directories;
- process success, non-zero exit, child signal termination, parent interruption
  forwarding/reaping, child-only environment additions, missing executable,
  working directory, stdout/stderr logs, monotonic process duration, observed
  log tee, and periodic activity for a slow process;
- progress percentage schedules for Verilog/SAPHO, time formatting, TTY redraw,
  linear redirected output, `--no-progress`, `--verbose`, every failing stage,
  and long-running spinner activity;
- full-build simulation progress, including linear non-TTY activity, TTY redraw,
  suppression through `build full --no-progress`, and clean handoff to synthesis;
- Linux host snapshot collection for OS/kernel/architecture, CPU, memory, and
  page-size report fields;
- Icarus/Yosys argument construction and testbench exclusion from synthesis;
- Verilator argv, VCD/FST selection, non-fatal warnings, includes/defines,
  isolated build paths, explicit workspace-whitespace rejection, and a cocotb
  2.x runner bridge without shell command construction;
- detached GTKWave and Surfer launch, exact viewer argv with spaced waveform
  paths, and non-interactive, headless, missing-tool, invalid-viewer, and
  `SOLAR_NO_VIEWER` paths;
- Test Service selection, profiles, isolation, `--all`, failure summaries, and
  explicit simulation compile/runtime/logical failure classification, including
  a passing process that produces no optional VCD/FST;
- preservation and CLI presentation of VVP stdout, including Verilog
  `$display` messages;
- missing configured waveforms report the exact expected path and direct users
  to rerun `solar scan` after changing a literal `$dumpfile` path;
- CLI argument and exit-code behavior;
- persisted benchmark report sections, host metadata, raw nanosecond timing,
  per-simulation compile/runtime timing, and failed-build readability;
- format-2 Verilog/YANC initialization, overwrite refusal, and invalid template
  selection;
- recursive conventional Verilog/SystemVerilog discovery, stable ordering,
  symlink exclusion, parameterized module-instantiation graphs, synthesis and
  testbench root inference, transactional `solar scan`, top replacement,
  ambiguity rollback, idempotence, and RTL-only synchronization after removing
  the last conventionally discovered testbench;
- `solar check` presentation of synthesis top, explicit/implicit default test,
  complete configured-test inventory, RTL-only state, and generated YANC test;
- YANC resolver precedence, checkout/release layouts, required executables, and
  paths containing spaces.
- native fake YANC stage arguments/order for CMM, C++, and Assembly, supported
  locale flags, include directories, frequency/clocks, failure short-circuiting,
  missing artifacts, atomic publication, and previous-build preservation.
- negative format-2 validation for defaults, names, defines, includes,
  waveforms, YANC source/extension/frequency/clocks, and `format = 0`;
- a real Icarus/Yosys format-2 CLI flow with two tests, profiles, includes,
  defines, paths with spaces, `--all`, deterministic synthesis, and testbench
  exclusion.

The fake-YANC tests are implemented as C executables and are part of the
canonical CTest suite.

## External-tool integration

Icarus and Yosys integration tests are registered with CTest. A missing tool
returns CTest skip code `77`, and the skip reason must be visible in test output.
Unit tests do not require those tools.

Real YANC CMM, C++, and Assembly tests run against the bundle built in the same
CMake tree. An alternate upstream checkout or release can be tested through:

```bash
SOLAR_TEST_YANC_ROOT=/path/to/yanc ctest --test-dir build --output-on-failure
```

The environment variable is a test input, not the normal project resolver.
CTest registers separate real CMM, C++, and Assembly pipelines and supplies the
freshly built bundled root by default. With the pinned official YANC v5.2
source, all three passed check/list, compile, a named generated test with a
profile, Icarus/VVP/VCD, and profiled Yosys synthesis.
The CMM matrix also exercises `simulate`; C++ uses a local header through an
include path containing spaces; Assembly verifies that the original source is
unchanged. Assembly exercises the instruction-count and neutral-PC-map bridge
described in [YANC integration](yanc.md).

## Sanitizers

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSOLAR_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

ASan and UBSan are development options only. In the 26-test matrix, 25 passed
with `ASAN_OPTIONS=detect_leaks=0`, including the three real bundled-YANC tests;
the real Verilator/cocotb integration was skipped because those external tools
were unavailable. In this executor LeakSanitizer terminates before test logic because
it cannot run under `ptrace`; independent leak checking therefore remains an
environment limitation. GCC `-fanalyzer` was also run over the previous
production modules without diagnostics.

## Compiler portability

GCC is the currently exercised compiler. The intended Clang verification is:

```bash
CC=clang cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Debug
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure
```

Do not claim Clang support as validated until those commands pass in an
environment where Clang is available.

## Current verification evidence

On 2026-07-12, GCC 16.1.1 Debug and Release each passed all 20 registered
tests with `SOLAR_TEST_YANC_ROOT=/tmp/yanc-release`. With that variable unset,
the three real YANC tests were reported as skipped and the other 17 passed.
ASan/UBSan passed the same matrix with `ASAN_OPTIONS=detect_leaks=0`. CMake
installation to `$HOME/.local` completed and the installed CLI reported
`solar 0.2.0`. Clang and Valgrind were not installed.

On 2026-07-13, all 21 Debug CTests passed except the three explicitly skipped
real-YANC tests because `SOLAR_TEST_YANC_ROOT` was unset. A separate manifest
with no RTL array and no test tables discovered two RTL files and one
testbench, then passed real Icarus/VVP simulation and Yosys synthesis.

On 2026-07-14, the suite grew to 25 tests. Debug passed 24 tests with the single
real Verilator/cocotb integration explicitly skipped; all three real YANC
pipelines passed against commit
`ea6135af18735e66a2bd23445749d6a244990fcd` built inside Solar. ASan/UBSan
passed the same matrix with leak detection disabled for the `ptrace` limitation.
A Release install under `/tmp/solar-install-final` resolved its private YANC
bundle without environment variables, and that installed binary completed a
real CMM compile through `cmmcomp`, `appcomp`, and `asmcomp`.

Later on 2026-07-14, the suite grew to 26 tests and useful artifacts moved to project-visible,
domain-specific directories: `sim/` and `synth/` for Verilog, `hardware/` and
`simulation/` for SAPHO/YANC. Internal executables, scripts, intermediates,
logs, and state stay below `.solar/`. The suite covers the artifact registry,
portable rename fallback, real YANC CMM/C++/Assembly, Icarus, Yosys, and
manifest-driven cleanup.
The installed Release binary also completed Verilog and bundled-YANC CMM
generation, simulation, and synthesis using the new public directories. The
YANC integration asserts that testbench-created sidecars remain internal.

On 2026-07-22, the fake and real YANC CMM/C++ regressions were extended to
require `software/<processor>.asm`. The Assembly regression continues to prove
that the original source remains byte-for-byte unchanged and that Solar does
not introduce a processor-named generated copy for that language.

## Interpreting evidence

Keep these evidence levels distinct:

1. A parser or fake-executable unit test proves Solar's own model and argv.
2. A skipped integration test proves only that its dependency was unavailable.
3. A direct upstream experiment proves the external tool contract but not the
   Solar integration.
4. An end-to-end Solar run proves the exercised source, version, and flow.

The CTest matrix builds and exercises the bundled YANC toolchain directly.
`integration_verilator` runs a normal Verilog testbench through the real
Verilator when available. `integration_verilator_cocotb` returns skip code 77
with a printed reason when Verilator or cocotb 2.x is absent. Fake executables
separately verify Verilator argv, FST selection, source/include arguments with
spaces, workspace-whitespace rejection, cocotb bridge arguments, and detached
viewer launch; these tests are not reported as real simulator evidence.

The release report must list every skip and preserve this distinction.

## v0.3 verification evidence

On 2026-07-16, the GCC Debug matrix registered 27 tests. Twenty-six passed and
`integration_verilator_cocotb` was explicitly skipped because cocotb 2.x is not
installed; Verilator itself is available. Real Icarus, Yosys, and the bundled
YANC 5.2 CMM/C++/Assembly flows passed. The same matrix passed under ASan/UBSan
with `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer cannot initialize under this
executor's `ptrace` policy, so leak detection was not exercised. Clang is not
installed and was not claimed.

The Release build installed to `$HOME/.local`. The PATH-resolved
`$HOME/.local/bin/solar` reported `0.3.0`, completed a real Verilog
`build full`, produced a complete report, and resolved its private YANC bundle
for a real CMM `build rtl` that published processor RTL, both MIF images, and
the generated testbench.

## v0.4 feature verification evidence

On 2026-07-20, the benchmark-report work increased the matrix to 28 tests.
Twenty-seven passed under GCC Debug; `integration_verilator_cocotb` remained an
explicit skip because cocotb 2.x is absent. Real Icarus/VVP/Yosys and bundled
YANC CMM/C++/Assembly flows passed. ASan/UBSan passed the same matrix with
`ASAN_OPTIONS=detect_leaks=0`; the unmodified run again confirmed that
LeakSanitizer cannot initialize under the executor's `ptrace` policy. Real
Verilog and YANC full-build reports were inspected and contained host/tool
snapshots plus per-process raw nanosecond timings. Clang remains unavailable.

On 2026-07-21, the Verilator audit increased the matrix to 29 tests and added a
real, cocotb-independent Verilog/Verilator integration. GCC Debug, Clang 22.1.8,
and ASan/UBSan with `ASAN_OPTIONS=detect_leaks=0` each completed with 28 passes;
`integration_verilator_cocotb` was the sole explicit skip because cocotb 2.x is
absent. Verilator 5.050 completed real VCD and FST simulations, preserved
`$display` output, and published the selected waveform. A direct experiment
also confirmed the upstream GNU Make workspace-whitespace restriction now
diagnosed by Solar.

Later on 2026-07-21, the RTL-only/no-waveform regressions increased the matrix
to 30 tests. GCC Debug, Clang Debug, and ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0` each completed with 29 passes and the same single
explicit cocotb skip. `integration_iverilog_no_waveform` exercised Icarus/VVP
with a real testbench lacking `$dumpfile`/`$dumpvars`; the process passed,
`$display` remained visible, the CLI warned, and no public VCD was created.

Later on 2026-07-21, the `build sim` progress work increased the matrix to 31
tests. GCC Debug, Clang 22.1.8, and ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0` each completed with 30 passes and the sole
explicit cocotb 2.x skip. The focused progress test validates the exact
`5/10/20/40/85/95/100` Verilog schedule, the redistributed SAPHO schedule,
TTY and redirected rendering, time formatting, disabled/verbose modes, every
failing stage, and heartbeat during a slow native helper process. Real Icarus
and bundled-YANC CMM simulation runs were also inspected; YANC generation was
shown and measured before Icarus compilation, and no viewer opened.

## v0.4 final verification evidence

The benchmark report, Verilator audit, RTL-only/optional-waveform support, and
simulation progress work above jointly define the 0.4 boundary. After changing
the compiled version to `0.4.0`, the canonical GCC Debug, Clang 22.1.8, and
ASan/UBSan matrices were rerun. Each registered 31 tests, passed 30, and
explicitly skipped only the real cocotb integration because cocotb 2.x is
unavailable. Real Icarus, Verilator, Yosys, and bundled YANC CMM/C++/Assembly
integrations passed. ASan/UBSan used `ASAN_OPTIONS=detect_leaks=0` because
LeakSanitizer cannot initialize under this executor's `ptrace` policy; no tag
is implied by this development evidence.

On 2026-07-22, Surfer/config-edit coverage increased the matrix to 32 tests.
GCC Debug, Clang 22.1.8, and ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0` each passed 31 tests and explicitly skipped only
the real cocotb integration because cocotb 2.x is unavailable. The viewer test
proved exact GTKWave/Surfer argv with a spaced FST path, missing-tool handling,
and compatibility-default selection. A real `surfer 0.7.0 (git: 4281e79)`
opened the counter VCD through `solar view basic --viewer surfer`; `build sim`
did not open it. The config-edit test proved combined updates, escaped names,
correction of an invalid existing default, byte-preserving no-op, rollback,
and format-1 refusal. The Release build installed both the updated Solar and
the user-provided Surfer client in `$HOME/.local`.

## v0.4.5 verification evidence

The 0.4.5 metadata update was verified from newly configured trees rather than
inferred from the 0.4.0 results. GCC Debug, Clang Debug, and ASan/UBSan each
registered 32 tests, passed 31, and explicitly skipped only
`integration_verilator_cocotb` because cocotb 2.x is unavailable. The
sanitized matrix used `ASAN_OPTIONS=detect_leaks=0` solely because
LeakSanitizer cannot initialize under the executor's `ptrace` policy.

The three matrices ran real Icarus/VVP, Verilator, Yosys, and bundled YANC v5.2
CMM, C++, and Assembly integrations. The focused CLI test also required
`solar --version` to contain the compiled `SOLAR_VERSION` value.
The Release was installed to `~/.local`; `command -v solar` resolved that
installation, `solar --version` printed `solar 0.4.5`, and `cmp` confirmed it
was byte-identical to the verified Release binary.

## Public release audit evidence

The 2026-07-22 release audit repeated the 32-test GCC, Clang, and ASan/UBSan
matrices. Each matrix exercised real Icarus/VVP, Verilator, Yosys, and bundled
YANC CMM/C++/Assembly flows; the real cocotb test was the sole explicit skip
because cocotb 2.x was not installed. After the final input-hardening changes,
13 focused parser, discovery, runner, artifact, resolver, fake-YANC, and real
YANC tests passed again.

A 700-byte CMM identifier reproduced a stack buffer overflow under ASan in the
bundled `cmmcomp`. The compiler now rejects qualified identifiers above its
63-byte safe capacity, and the real CMM/C++/Assembly integration tests verify
graceful failure plus preservation of the previous public hardware. The ASan
CMM regression and manual ASan C++/Assembly probes produced diagnostics rather
than sanitizer findings.

LeakSanitizer could not initialize because the local executor runs under
`ptrace`; this is a skip, not a pass. `clang-tidy` and `scan-build` findings
were reviewed: actionable size arithmetic, descriptor validation, path size,
and bundled YANC issues were corrected. Remaining raw analyzer reports are
result-correlation false positives or documented style findings. The complete
classified evidence is in [Release audit 0.4.5](release-audit-0.4.5.md).

## Public launch verification evidence

On 2026-07-23, newly configured GCC Debug, Clang 22.1.8 Debug, and
ASan/UBSan trees each registered 32 tests, passed 31, and explicitly skipped
only the real cocotb integration because cocotb 2.x is absent locally. Real
Icarus/VVP, Verilator, Yosys, and bundled YANC CMM/C++/Assembly integrations
passed. ASan/UBSan used `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer
cannot initialize under the executor's `ptrace` policy.

The Release CPack archive installed below a temporary prefix through the
public installer, reported `solar 0.4.5`, resolved its private YANC bundle,
survived a repeat installation, rejected a deliberately corrupted archive
and a checksum-valid archive containing a symbolic link without changing the
valid installation, and uninstalled to an empty prefix.
The installed binary completed `scan`, `check`, `build full`, and `report` for
the Verilog counter example and completed the real bundled-YANC CMM full flow.
Hosted Ubuntu 24.04 CI then passed GCC, Clang, ASan/UBSan, package installation,
and a real cocotb 2.x integration with Verilator 5.036. The Ubuntu 22.04
release gate remains tag-triggered and is verified separately during release
publication.

The hosted policy keeps GCC and Clang portability matrices on Ubuntu's
distribution EDA packages. A separate real cocotb job builds Verilator 5.036
from the pinned upstream commit
`eca2b4c9603554a6a4596befd485c3dd4f550769`, because cocotb 2.x does not
support Ubuntu 24.04's packaged Verilator 5.020. The Ubuntu 22.04 release job
intentionally treats Verilator and cocotb as optional skips; it validates the
release archive against Icarus, Yosys, and the bundled YANC toolchain.

## Stored report comparison evidence

The report-history test exercises unsigned comparison at zero and near
`UINT64_MAX`, missing values, added and removed Yosys cells, deterministic
cell ordering, normalized and incompatible HDL-duration units, failed and
partial simulation timings, context incompatibilities/warnings, escaped
versioned sidecar round trips, unique IDs, atomic `latest`, TXT-only legacy
records, corrupt metadata, and the `list`, `show`, `compare`, `--against`, and
`--summary` CLI forms including invalid arguments. These tests use no EDA
executable.

On 2026-07-25, a clean GCC Debug build registered and passed all 34 tests.
That run also exercised the installed real Icarus/VVP, Yosys,
Verilator/cocotb, and bundled YANC CMM/C++/Assembly integration flows. A clean
Clang build passed the same 34 tests. ASan/UBSan also passed all 34 tests with
`ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer is explicitly skipped because it
cannot initialize under the executor's `ptrace` policy.

Focused `clang-tidy` analysis of the history, comparison, report CLI, and
related regression tests produced no diagnostics. A broader analyzer build
still reports existing findings in unrelated Core and bundled YANC sources;
those are not presented as a clean repository-wide static-analysis result.

Two real Yosys synthesis builds and two real Icarus simulations created four
ordered snapshots. The installed Release binary (`solar 0.4.5`) successfully
listed them, selected the prior compatible simulation baseline, and compared
two explicit synthesis builds. These report operations only read persisted
files and did not invoke an EDA backend.

## Shell completion evidence

The completion regression creates a format-2 project in a path containing
spaces with two tests, two profiles, two stored builds, and a registered
waveform whose filename contains a space. It checks candidate discovery from a
child directory, empty output outside a project, hidden protocol isolation,
and functional Bash/Zsh completion for the full public CLI contexts. Its child
environment has no EDA executable on `PATH`, proving candidate lookup does not
start a backend.

On 2026-07-25, clean GCC, Clang, and ASan/UBSan builds each registered 38 tests
and passed 37, including all real integrations. Fish syntax was the only
explicit skip because Fish is not installed on this machine. Bash and Zsh
syntax and functional checks passed, and focused `clang-tidy` analysis of the
new C code produced no findings.

The Release installation placed the definitions in the standard Bash, Zsh,
and Fish data directories below `~/.local`; each installed file matched its
verified source. Bash and Zsh smoke tests loaded those installed definitions
and completed the four build IDs from a real report history.

## v0.4.6 release evidence

Fresh GCC, Clang 22.1.8, and ASan/UBSan builds of the compiled `0.4.6` identity
each registered 39 tests. In each matrix, 38 passed and only Fish syntax was
explicitly skipped because Fish is unavailable locally. The passing set
includes real Icarus/VVP, Yosys, native Verilator, Verilator/cocotb, and bundled
YANC CMM/C++/Assembly integrations. ASan/UBSan used
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot initialize under the
executor's `ptrace` policy.

The new installer regression builds a checksummed archive around the tested
Solar binary, installs Bash/Zsh/Fish completion paths, uninstalls to an empty
prefix, and confirms that an archive member containing
`include/solar/../../evil` is rejected. Focused `clang-tidy` checks on report
history, comparison, completion, progress, and their tests produced no finding.

A complete CPack archive installed as Solar 0.4.6 into a temporary prefix. The
installed binary ran `init`, `scan`, `check`, a real Icarus/Yosys `build full`,
GSS reporting, and report-history listing before uninstalling to an empty
prefix. `AGENTS.md`, local build trees, and workspace artifacts are absent from
the package.

The local Arch Linux package is functional evidence, not the portable release
asset: its locally built bundled YANC binaries require GLIBC 2.38. The hosted
tag workflow rebuilds on Ubuntu 22.04 and refuses publication unless Solar and
every bundled YANC executable stay at or below the declared GLIBC 2.35 ceiling.
