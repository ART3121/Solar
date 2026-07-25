# Solar implementation plan

## Public documentation website integration (2026-07-23)

### Scope and decisions

- Make `ART3121/Site-Solar` the public entry point for the Solar 0.4.5 user
  manual while keeping detailed versioned references beside the source.
- Reuse the landing page's monochrome visual system, animated Solar mark,
  typography, responsive navigation, theme preference, and English/PT-BR
  language preference on a dedicated `/docs/` page.
- Publish the site wordmark as a reusable SVG and display it in the Solar
  README. Point the repository homepage and primary documentation links to the
  website.
- Keep the public source tree focused on product, contributor, and release
  material by removing internal development-instruction files and their public
  references.

### Verification

- Validate landing/manual HTML, JavaScript syntax, relative assets, desktop
  rendering, mobile PT-BR rendering, and GitHub Pages deployment.
- Re-run Solar's relevant documentation/package checks through protected-branch
  CI before merging both repositories.

## Hosted launch CI compatibility corrections (2026-07-23)

### Discovery and scope

- The first hosted Ubuntu 24.04 run built Solar successfully but exposed two
  backend portability gaps hidden by newer local EDA packages: packaged Yosys
  rejected absolute `tee -o` output paths, and packaged Verilator 5.020 did
  not recognize the optional `--quiet-build` flag.
- The same run proved that installing cocotb 2.x beside the distribution's
  Verilator is not a valid real integration: cocotb 2.x requires Verilator
  5.036 or newer.
- Correct only backend command portability and CI dependency selection. Keep
  the manifest, public command surface, artifact paths, and ownership model
  unchanged.

### Milestones and acceptance

1. Generate Yosys output names relative to its already isolated
   `.solar/tmp/synth` working directory, while retaining absolute source paths
   and absolute Core artifact registration. Keep those fixed safe output names
   unquoted because older Yosys `tee` retains quote characters in option
   values instead of normalizing them.
2. Remove Verilator's cosmetic `--quiet-build` flag and add regression checks
   for both argv and Yosys script output paths.
3. Keep GCC/Clang matrices on distribution tools and add one real cocotb job
   that builds the minimum supported Verilator 5.036 from its pinned upstream
   commit. Release compatibility tests may explicitly skip optional Verilator
   and cocotb while the dedicated job supplies their real evidence.
4. Rerun focused and complete local tests, push the correction, and require
   hosted GCC, Clang, sanitizer, cocotb, Pages, and release workflows to finish
   successfully before tagging 0.4.5.

### Completion evidence

- Local focused regressions and the complete 32-test suite pass. The local
  cocotb integration is the sole explicit skip because cocotb 2.x is absent.
- Hosted Ubuntu 24.04 GCC, Clang, ASan/UBSan, package-install, and real cocotb
  2.x with pinned Verilator 5.036 jobs pass after the portability corrections.
- `actions/checkout` is pinned to v7.0.1, and CI push events are restricted to
  `main` so a pull request does not duplicate its full matrix.

## Public launch infrastructure for Solar 0.4.5 (2026-07-23)

### Goal and release boundary

- Publish Solar 0.4.5 from `ART3121/solar` as the first stable public Linux
  release, without changing the manifest format, CLI command surface, backend
  set, or Core ownership boundaries.
- Support a prebuilt GNU/Linux x86_64 archive installed below `~/.local` by an
  explicit checksum-validating script. The installer must not use `sudo`, edit
  shell startup files, or install external EDA packages.
- Keep English as the canonical manual language, add a Brazilian Portuguese
  getting-started path, and publish the Markdown manual through GitHub Pages.
- Keep Icarus, Yosys, Verilator, cocotb, GTKWave, and Surfer user-managed;
  bundle only the already supported private YANC 5.2 toolchain.

### Implementation milestones

1. Reorganize the public documentation into a concise README, navigable user
   manual, PT-BR getting started, troubleshooting, installation, support, and
   release procedures while retaining detailed contributor references.
2. Add CPack TGZ packaging and a POSIX installer with version/prefix/uninstall
   options, exact ownership records, SHA-256 verification, safe upgrades, and
   rollback. Exercise it against a locally staged release server.
3. Add GitHub community files, issue forms, pull-request guidance, dependency
   update policy, Pages configuration, CI, and a tag-gated draft release
   workflow for `v0.4.5`.
4. Improve the known stale-waveform diagnostic: a successful simulator run
   that writes a different literal `$dumpfile` path must name the configured
   expected path and direct users to `solar scan` after testbench edits.
5. Run focused regressions, all CTests, sanitizer/Clang matrices where
   available, package/install/uninstall smoke tests, and real available-tool
   example flows. Record every external-tool skip separately.

### Acceptance and publication boundary

- The archive installs the complete CMake layout, `solar --version` reports
  0.4.5, the private YANC root resolves, a second install is safe, checksum
  failure changes nothing, and uninstall removes only installer-owned files.
- Required CI workflows use minimal permissions and official GitHub actions;
  release assets are `solar-linux-x86_64.tar.gz`, `install.sh`, and
  `SHA256SUMS`. A release is created as a draft from an existing matching tag.
- GitHub administrative actions (repository visibility/protection, Pages,
  Discussions, private vulnerability reporting, immutable releases, tag, and
  publication) remain explicit maintainer steps and are never inferred from a
  local build. Local completion does not claim that those remote actions ran.

### Completion evidence

- CPack produced `solar-0.4.5-linux-x86_64.tar.gz`; the installed layout
  contains the CLI, Core headers/static library, supported private YANC tools,
  installer, notices, and manuals.
- A local release fixture verified checksum enforcement, install, repeat
  install, failure rollback, rejection of unexpected archive links,
  ownership-manifest uninstall, and an empty final prefix. Packaged Verilog and
  YANC CMM full flows passed with real available backends.
- GCC Debug, Clang 22.1.8 Debug, and ASan/UBSan each registered 32 tests,
  passed 31, and explicitly skipped only real cocotb because it is absent.
- Community templates, Pages-ready manuals, CI, and tag-gated draft-release
  automation are present. The public repository, Pages, Discussions, security
  settings, dependency automation, and hosted CI are configured and verified;
  the protected release tag and publication remain the final launch steps.

## Public release audit for Solar 0.4.5 (2026-07-22)

### Baseline and scope

- Audit the repository snapshot as it exists, without assuming earlier command,
  backend, layout, or test behavior still applies.
- Review production code, public headers, tests, examples, build/install rules,
  bundled dependencies, documentation, scripts, licensing, packaging, and
  GitHub automation. Corrections are limited to reproducible defects and
  release-blocking omissions; this milestone adds no product features.
- The `.git` directory exposed in this environment is empty. Git index and
  history checks therefore cannot be performed here. The source snapshot will
  still be scanned for secrets, personal paths, generated files, unexpected
  binaries, and oversized files, and the missing history evidence will remain
  explicit in the audit report and release recommendation.

### Verification milestones

1. Inventory dependencies, generated/local files, public surface, workflows,
   licenses, and sensitive-data indicators.
2. Audit configuration parsing, process execution, signal handling, path
   traversal controls, atomic publication, cleanup, CLI/TTY behavior, and
   backend isolation; add regressions only for defects actually reproduced.
3. Run clean GCC and Clang Debug/Release builds, the complete CTest matrix,
   external integrations where installed, ASan/UBSan/LSan where supported,
   and every available static analyzer.
4. Exercise installation under a temporary prefix, the installed executable,
   documented examples, source packaging, and a rebuild from a clean exported
   snapshot that excludes local build state.
5. Remove only confirmed local/generated debris, reconcile public docs and
   automation with observed behavior, and publish a classified audit report.

### Acceptance and release decision

- Every executed check is recorded as PASS or FAIL; unavailable tools and the
  inaccessible Git history are SKIP with a concrete reason, never implicit
  successes.
- A blocker remains open for any reproducible crash, memory corruption,
  command injection, unsafe path operation, broken build/install, essential
  failing test, license failure, or materially false primary documentation.
- The final recommendation is exactly `READY`, `READY WITH KNOWN LIMITATIONS`,
  or `NOT READY`, based on the remaining evidence after corrections.
- Mutating commands must serialize on one project-root lock. A concurrent
  operation is rejected without changing the active operation's report or
  generated state.
- Manifest input is bounded before parsing, and the limit has a negative
  regression test.

### Audit outcome

- Inventory, security review, GCC/Clang builds, real external integrations,
  sanitizer coverage, static analysis, installation, and documentation review
  were completed to the extent supported by this environment.
- A reproducible bundled-YANC stack overflow, project mutation race, unsafe
  check-then-open reads, unbounded in-memory output/state reads, and several
  compiler stream/allocation faults were fixed with regressions.
- Local build trees and editor logs are release debris and are removed from
  the source root after verification.
- Recommendation: `READY WITH KNOWN LIMITATIONS`; see
  `docs/release-audit-0.4.5.md` for the exact scope and residual risks.

## Solar 0.4.5 release metadata (2026-07-22)

### Baseline and decision

- Solar 0.4.0 already defined the benchmark report, Verilator hardening,
  RTL-only/optional-waveform behavior, and simulation progress boundary.
- Surfer viewing, transactional `solar config set`, and public generated YANC
  Assembly were implemented after that boundary and are currently mixed into
  the 0.4.0 changelog.
- Version 0.4.5 will identify the current implementation. Project manifest
  format remains 2; this product-version update requires no project migration.

### Milestones and acceptance

1. Change the CMake and public API version constants to 0.4.5.
2. Add an explicit 0.4.5 changelog/release note and restore 0.4.0 as historical
   evidence rather than attributing later features to it.
3. Update README, roadmap, AGENTS, testing evidence, and installed docs without
   changing command semantics, backends, or artifact paths.
4. Rebuild and run GCC, Clang, and ASan/UBSan matrices, then install the Release
   and verify that the PATH binary reports exactly `solar 0.4.5`.

No tag or hosted release is created by this milestone.

### Completion evidence

- CMake project metadata and the public `SOLAR_VERSION` constant now report
  0.4.5; manifest format remains 2.
- CHANGELOG preserves the 0.4.0 boundary and records Surfer, config editing,
  and public CMM/C++ Assembly under 0.4.5. The dedicated compatibility note
  documents that no manifest migration is required.
- GCC Debug, Clang Debug, and ASan/UBSan each registered 32 CTests, passed 31,
  and explicitly skipped only the real cocotb integration because cocotb 2.x
  is unavailable. ASan/UBSan used `ASAN_OPTIONS=detect_leaks=0` because
  LeakSanitizer cannot initialize under this executor's `ptrace` policy.
- Real Icarus, Verilator, Yosys, and bundled YANC v5.2 CMM/C++/Assembly
  integrations passed in all three matrices.
- The Release build was installed to `~/.local`; the PATH-resolved executable
  is byte-identical to `build-release/solar` and reports `solar 0.4.5`.

## Public YANC Assembly artifact (2026-07-22)

### Current state

- The YANC backend validates and retains `<processor>.asm` below its private
  `.solar/tmp/yanc/` publication workspace.
- Public YANC publication currently includes processor RTL, memory images, and
  the generated testbench, but omits the useful compiler-generated Assembly.
- The artifact registry currently accepts only `sim/`, `synth/`, `hardware/`,
  and `simulation/`, so `clean` cannot safely own a generated file below
  `software/`.

### Decisions

1. CMM and C++ builds publish the validated generated Assembly as
   `software/<processor>.asm` in the same transactional artifact set as the
   Verilog, MIF, and testbench outputs.
2. The registry accepts `software/` only for a record whose type is
   `assembly`, stage is `generation`, and backend is `yanc`. This narrow rule
   keeps the user-source directory outside generic artifact ownership.
3. Direct Assembly projects do not publish or register an Assembly artifact:
   their `[compiler].source` remains user-authored input and must not be
   replaced or removed by Solar.
4. An existing unregistered `software/<processor>.asm` is treated as a manual
   file and blocks publication with an actionable diagnostic. A previously
   registered generated Assembly may be replaced atomically.

### Acceptance

- Fake CMM and C++ pipelines expose and register the public Assembly; fake and
  real Assembly pipelines prove that the original source remains unchanged.
- Failure before or during publication preserves the previous generated
  Assembly, and `solar clean` removes only its registered copy while retaining
  unrelated files and the `software/` directory.
- Documentation describes Assembly as a visible generated artifact and keeps
  fake-tool evidence distinct from the real YANC runs executed here.

### Completion evidence

- CMM/C++ publication now returns and registers
  `software/<processor>.asm`; the artifact record is accepted below
  `software/` only for YANC `assembly`/`generation` metadata.
- Unit regressions cover invalid ownership metadata, manual-file conflict,
  registry lookup, registered cleanup, rebuild failure preservation, and
  direct Assembly source isolation.
- The real bundled YANC v5.2 matrix at commit
  `ea6135af18735e66a2bd23445749d6a244990fcd` passed CMM, C++, and Assembly.
  CMM/C++ required the public Assembly; Assembly required no generated public
  copy and preserved its source.
- GCC Debug, Clang Debug, and ASan/UBSan each passed 31 tests with the real
  cocotb integration explicitly skipped. Leak detection was disabled only for
  the sanitizer run because LeakSanitizer cannot initialize under this
  executor's `ptrace` policy.
- A Release smoke test and the installed `~/.local/bin/solar` 0.4.5 published
  `software/sapho_demo.asm`, displayed it in `solar build rtl` and
  `solar report`, then removed it with `solar clean --all` while preserving
  `software/processor.cmm`.

## CLI v0.3, RTL build, scan, and complete report (2026-07-16)

### Baseline

- The public CLI currently exposes overlapping direct operations and build
  targets. `test` and `sim` share one service, while YANC generation may be
  triggered from several commands.
- `SolarBuildContext` already reuses one compiler result inside a pipeline, but
  has no RTL-elaboration result, tool snapshot, or owned collection of
  simulation results.
- Conventional discovery is deterministic and non-mutating, recognizes only
  `.v`, and the deprecated `up` command merely prints its in-memory inventory.
- The operation report is a pre-rendered last-report file with step durations
  and the whole artifact registry, not an operation-specific build record.

### Decisions

1. The v0.3 public surface is `init`, `scan`, `check`, `doctor`, `clean`,
   `build rtl|sim|synth|full`, `view`, and `report`. Old operation names are
   removed rather than aliased.
2. Build targets are dependency-complete. Simulation and synthesis first run
   RTL; full runs check, RTL, simulations in manifest order, then synthesis.
   `sim --all` continues after failures, while full stops at the first failed
   simulation.
3. Verilog RTL build dispatches a typed elaboration request to the configured
   simulation backend. YANC RTL build remains the compiler pipeline. No
   incremental cache is introduced.
4. Discovery gains `.sv`. `scan` synchronizes conventional RTL and Verilog
   test tables directly into the Solar TOML subset using a validated,
   transactional candidate. Project loading remains automatically discovering,
   so scan is never a build prerequisite.
5. The last report remains human-readable and atomic, but is expanded from the
   owned build context: project/configuration snapshot, timestamps, tools,
   steps, per-simulation results, operation artifacts, logs, and bounded stderr
   tails on failures.

### Milestones and acceptance

1. New CLI grammar and build-context collection with focused parser/CLI tests.
2. Icarus compile-only and Verilator lint-only RTL elaboration.
3. Transactional scan, v1-to-v2 conversion, `.v`/`.sv`, ambiguity rollback,
   and idempotence tests.
4. Complete report, docs/AGENTS/CHANGELOG v0.3, full integration matrix, and
   Release installation smoke tests through the PATH-resolved binary.

No milestone changes the current public artifact directories, backend set,
project templates, or safe runner policy.

### Completion evidence

- The public CLI contains only the planned project/build/artifact commands;
  removed direct-operation implementations are no longer compiled or present.
- Verilog `build rtl` is covered for Icarus compile-only and Verilator
  lint-only requests, including `.sv` synthesis through Yosys `-sv` mode.
- Scan tests cover explicit-source preservation, stale managed removal,
  `.sv`, preserved test options, idempotence, ambiguity rollback, and guarded
  v1 migration.
- CLI regressions cover mandatory build targets, no-tool dry-run, no automatic
  viewer, view without simulation, full-pipeline failure short-circuiting,
  report persistence after a broken manifest, and bounded stderr evidence.
- GCC Debug: 26 pass and one explicit Verilator/cocotb skip out of 27 CTests.
  Real Icarus, Yosys, and bundled YANC CMM/C++/Assembly integrations passed.
- ASan/UBSan passed the same matrix with leak detection disabled because
  LeakSanitizer cannot initialize under this executor's `ptrace` policy.
  Clang is unavailable and is not claimed.
- Release was installed to `$HOME/.local`; the PATH binary reports `0.3.0`,
  completed a real Verilog full build/report, and a real installed-bundle YANC
  CMM RTL build.

## Public artifact layout and state manifest (2026-07-14)

### Current-state findings

- Test executables, waveforms, YANC results, and synthesis outputs are currently
  grouped below `solar-build/`; cleanup owns and removes that whole tree.
- Icarus/Verilator and Yosys currently receive final public output paths, so an
  external tool can overwrite the previous successful result before validation.
- YANC stages already execute below `.solar/tmp`, but their normalized candidate
  is promoted as one directory using Linux `RENAME_EXCHANGE` only.
- `view` derives a waveform from configuration, while `report` stores its own
  artifact paths. Neither is backed by a shared generated-file inventory.

These findings describe the pre-change state inspected at the start of this
milestone. The implementation below replaces those behaviors.

### Architecture decisions

1. Add a small Core artifact registry persisted as
   `.solar/state/artifacts.tsv`. Each safe project-relative generated file has
   type, stage, test, backend, and profile fields. Paths cannot be absolute or
   contain control characters, `.` components, or `..` components.
2. External backends write only below `.solar/tmp`. After output validation,
   Core publishes individual regular files into `sim/`, `synth/`, `hardware/`,
   or `simulation/`, then updates the registry atomically.
3. Individual publication uses a same-directory candidate and backup. Where
   `RENAME_EXCHANGE` is unsupported, `final -> backup`, `candidate -> final`,
   and rollback restore the previous file on failure. Directory exchange is no
   longer required for public output.
4. Public directories are never recursively owned or removed. `clean` reads the
   registry and removes only matching regular files without following symlinks;
   it then removes the requested internal `.solar` subtrees.
5. `view` resolves the latest matching waveform from the registry. `report`
   remains human-readable state under `.solar/state/last-report.txt` and lists
   paths sourced from the same registry/results.
6. Existing `solar-build/` is detected and warned about but never adopted,
   migrated, written, or removed.

### Milestones and acceptance

1. Artifact registry, safe file publication with portable rollback, legacy
   layout warning, and focused filesystem tests.
2. Internal test/synthesis workspaces and public Verilog waveform/netlist/report
   publication with registry-backed view/report.
3. YANC publication into `hardware/` and `simulation/`, keeping native
   Assembly staging, metadata, support files, executables, and scripts internal.
4. Template layouts, `.gitignore`, README files, clean modes, integration tests,
   documentation, GCC/ASan/Clang verification where available.

Acceptance requires that failed publication preserves every previous valid
public file, manual files survive every clean mode, no new operation creates
`solar-build/`, and manifest-v1/v2 projects run without edits.

### Completion evidence

- Milestones 1-4 are implemented. Verilog publishes waveforms in `sim/` and
  synthesis outputs in `synth/`; YANC publishes RTL/MIF in `hardware/` and its
  testbench/waveform in `simulation/`.
- `.solar/state/artifacts.tsv` drives `clean`, `view`, and report artifact
  listings. Native Assembly staging, VVP images, Yosys scripts, workspaces,
  logs, and metadata remain below `.solar/`; the current CMM/C++ public
  Assembly policy is defined by the newer milestone above.
- The portable fallback is forced by a unit test through
  `SOLAR_TEST_DISABLE_RENAME_EXCHANGE`; registered replacements and manual-file
  preservation are verified.
- Debug CTest passes, including real bundled YANC CMM/C++/Assembly, Icarus, and
  Yosys flows. The 26-test matrix completes under GCC Debug and ASan/UBSan:
  25 tests pass and Verilator/cocotb is one explicit skip. Release builds and
  installs successfully. The
  installed CLI completed Verilog and YANC generation, simulation, and
  synthesis with the public layout. Clang is unavailable in this environment.
- Generated YANC testbench sidecars such as `output_0.txt` remain below
  `.solar/tmp/tests/generated`; only the registered testbench and waveform are
  published in `simulation/`.

## CLI operations and shared build context (2026-07-14)

### Requested outcome

- Canonical commands become `generate`, `test`, `sim`, `synth`, `view`,
  `build`, and `report`, alongside the project commands.
- `compile`, `simulate`, and `synthesize` remain deprecated aliases that call
  the same implementation. They are omitted from primary help.
- `test` never opens a viewer; only `sim --view` and `view` may launch GTKWave.
- `build full` validates, generates once when needed, tests, and synthesizes.
- The last operation report is stored below `.solar/` without changing any
  existing artifact path or manifest field.

### Current-state finding

`solar up` is a non-mutating discovery inventory, not a complete pipeline.
Discovery already runs during project load and `solar check`. To preserve its
useful detailed inventory and existing tests, `up` remains a deprecated command
whose documented replacement is `check`; it is not mapped to `build full`.

### Architecture decisions

1. Add an owned `SolarBuildContext` in Core with the project, effective profile,
   one compiler result, test/synthesis results, step timings, and diagnostics.
2. Add Test and Synthesis entry points that consume optional pre-generated
   artifacts. Existing public entry points retain their behavior by creating a
   local compiler result when no artifacts are supplied.
3. Direct commands and pipelines call the same context operations. The context
   is the only layer allowed to ensure YANC generation during orchestration.
4. Store a deterministic human-readable last-operation report at
   `.solar/state/last-report.txt`; `solar report` reads it without executing
   tools.
5. `--dry-run` loads and validates the project and records planned steps, but
   never invokes a compiler, simulator, synthesizer, or viewer.
6. Project-scoped doctor filtering belongs in the backend registry/Core, not in
   CLI string conditionals. `--all` retains the complete diagnostic matrix.

### Milestones

1. Context model, generated-artifact reuse APIs, report persistence, unit tests.
2. Canonical direct commands, aliases, per-command help, and no automatic viewer.
3. Build pipelines, dry-run, single YANC generation evidence, and report CLI.
4. Project-aware doctor, deprecated `up`, docs, complete toolchain verification.

### Progress

- 2026-07-14: added the owned Core `SolarBuildContext`, artifact-aware Test and
  Synthesis entry points, atomic `.solar/state/last-report.txt`, and report
  reading.
- 2026-07-14: canonical commands, hidden deprecated aliases, grouped main help,
  command help, explicit viewer commands, build pipelines, and project-aware
  doctor are implemented.
- 2026-07-14: fake-tool regressions prove that `view` invokes no simulator,
  ordinary `test`/`sim` invoke no viewer, dry-run invokes no operation backend,
  and one YANC generation is reused by test and synthesis.
- 2026-07-14: GCC Debug and ASan/UBSan each passed 25 CTests, including real
  bundled-YANC CMM/C++/Assembly, Icarus, Yosys, CLI, and format-v1/v2 flows.
  Real Verilator/cocotb remained one explicit skip because those runtimes are
  absent. Clang is not installed in this environment and was not claimed.
- 2026-07-14: real `examples/yanc-cmm` and `examples/counter` `build full`
  pipelines passed with reports, selected profiles, visible existing artifact
  paths, and no implicit viewer launch.

### Acceptance and risks

- No command changes `solar-build/`, `.solar/` sub-layouts already in use,
  waveform/netlist/YANC paths, `solar init`, or `solar.toml`.
- A failed step must be recorded before the context returns; later full-pipeline
  steps do not run after failure.
- Report persistence failure is an I/O failure and must not silently replace the
  operation result.
- Real YANC stage records must prove one compiler execution in `build full`.

## Visible project build artifacts (2026-07-14)

### Requested outcome

Final generated artifacts must be directly visible from the project root
instead of being hidden below `.solar/build`.

### Decision

- Use the dedicated visible directory `solar-build/`. A generic `build/` could
  collide with CMake or another project tool and cannot be safely owned by
  `solar clean`.
- Publish format-2 test artifacts under `solar-build/tests/<test>/`, synthesis
  artifacts under `solar-build/synth/`, and compiler outputs under
  `solar-build/yanc/<processor>/`.
- Keep logs and transient workspaces below `.solar/logs` and `.solar/tmp`.
- Retain format-1 `.solar/build/sim` behavior only when an old manifest
  explicitly depends on that path.
- Extend generated-path traversal and cleanup to exactly two owned roots:
  `.solar/` and `solar-build/`. Both reject symlink traversal.

### Acceptance

1. Test, synthesis, and YANC commands return visible `solar-build/` paths.
2. Atomic YANC publication still preserves the previous valid build.
3. `solar clean` removes both owned generated roots and no other project path.
4. Unit, integration, sanitizer, and installed example flows pass.

### Progress

- 2026-07-14: filesystem ownership now recognizes exactly `.solar/` and
  `solar-build/`, with descriptor-relative no-follow creation, reset,
  publication, and cleanup.
- 2026-07-14: Test Service, Yosys, and YANC final paths moved to the visible
  root. Focused filesystem/backend/service tests and real Icarus, Yosys,
  Verilog-v2, YANC CMM, C++, and Assembly integrations passed.
- 2026-07-14: the CMM example visibly produced compiler RTL/MIF/Assembly,
  simulation VVP/VCD, and synthesis script/netlist/report under `solar-build/`;
  `solar clean` then removed both owned generated roots while retaining sources.

## Integrated toolchain and simulation expansion (2026-07-14)

The automatic-viewer behavior recorded in this historical milestone was
superseded by the CLI reorganization above. Viewer launch is now explicit.

### Requested outcome

1. Solar ships the YANC compiler sources and builds a pinned toolchain as part
   of Solar; a normal installation no longer depends on a separately cloned or
   installed YANC root.
2. Verilator becomes a registered simulation backend and cocotb becomes an
   explicit test driver that can use Verilator.
3. Successful interactive single-test simulation launches GTKWave immediately
   for the produced VCD/FST. Headless/non-interactive runs never attempt a GUI.

### Architecture decisions

- Vendor upstream YANC under `third_party/yanc/` at one recorded commit, retain
  its MIT license and notices, and build its standalone compiler executables.
  YANC remains a compiler toolchain backend invoked through the safe runner;
  its command-line programs are not rewritten or linked into `solar_core`.
- Install bundled YANC executables and read-only HDL/macros/headers below
  Solar's private data/libexec directories. Resolution prefers the bundled
  toolchain. `SOLAR_YANC_ROOT` remains only as a development/test override so
  upstream revisions can be evaluated without replacing the bundled copy.
- Register `verilator` beside `iverilog`. Existing Verilog testbenches use
  Verilator's `--binary --timing` mode. VCD and FST are explicit artifact
  formats and never inferred from an arbitrary command string.
- Model cocotb as a test driver, not as a simulator backend. A cocotb test names
  one Python test file and a Verilog DUT top; the selected simulation backend
  remains Verilator. Python is an external cocotb runtime dependency, never a
  Solar Core/CLI implementation language.
- Add a Core waveform-viewer service. It receives a validated artifact path and
  launches `gtkwave` through native process APIs. Automatic opening occurs only
  for successful individual `test`/`simulate` runs attached to an interactive
  graphical session. `test --all`, CTest, missing GTKWave, and headless sessions
  do not open windows. `SOLAR_NO_VIEWER=1` is the explicit automation override.

### Milestones

1. **YANC vendoring:** inspect/pin upstream, add license inventory, integrate
   its build/install layout, change resolver/doctor, and run CMM/C++/Assembly
   flows against the bundled result.
2. **Verilator backend:** extend configuration and backend registry, construct
   shell-free argv, support VCD/FST artifacts, add fake-tool tests, and run a
   real integration when Verilator is available.
3. **cocotb driver:** extend the project/test model, validate Python test paths,
   implement a structured runner bridge, add fake-runtime tests, and run a real
   integration when cocotb plus Verilator are available.
4. **Waveform launch:** implement interactive viewer policy, asynchronous safe
   launch, CLI integration after successful single tests, and tests proving
   launch/skip/error behavior without starting a real GUI.
5. **Hardening:** Debug/Release, ASan/UBSan, examples, installation, dependency
   diagnostics, documentation, and safety review.

### Risks and acceptance boundaries

- Vendoring YANC materially increases repository and build size and adds Flex,
  Bison, GCC/G++ and Make as build-time requirements. This is accepted by the
  explicit product decision but must be documented honestly.
- Verilator and cocotb remain external simulation dependencies; Solar does not
  download or install them. Support is not claimed as real until their
  end-to-end integrations run on a machine where they are installed.
- A GUI process must not block Solar, leak zombies, or make a successful
  simulation fail merely because no graphical session exists.
- The upstream YANC revision and any local build adaptations must be recorded;
  generated parser/lexer files are not silently regenerated with untracked
  versions.

### Progress

- 2026-07-14: vendored YANC commit `ea6135af18735e66a2bd23445749d6a244990fcd`,
  added the mandatory staged build/private install bundle and license notice,
  fixed the upstream glibc `fsqrt` identifier collision, and passed real CMM,
  C++, and Assembly integrations against the bundle.
- 2026-07-14: added the Verilator backend, VCD/FST selection, cocotb 2.x test
  driver, parser/validation fields, example, fake process tests, and an
  explicit real integration skip because Verilator/cocotb are not installed.
- 2026-07-14: added detached GTKWave launch for successful interactive single
  tests plus headless/automation policy and a fake executable test. The local
  GTKWave binary cannot open a real window because the executor cannot access
  its graphical session.
- 2026-07-14: Debug passed 24 of 25 tests with only the real
  Verilator/cocotb integration explicitly skipped. Real bundled-YANC CMM, C++,
  and Assembly flows passed. ASan/UBSan passed the same exercised matrix with
  LeakSanitizer disabled because the executor runs under `ptrace`.
- 2026-07-14: a Release install resolved YANC from its own private `libexec`
  directory without environment configuration, and the installed CLI completed
  a real CMM compile. Milestones 1-5 are complete within the external-tool
  validation limits recorded above.

## Automatic Verilog discovery (2026-07-13)

### Current problem

Format-2 projects can describe arbitrary RTL and tests, but the normal entry
point still looks counter-specific because `solar init` writes explicit
`rtl/counter.v` and `tb/counter_tb.v` entries. Adding another authored file
requires editing `solar.toml`, even though the `rtl/` and `tb/` directory roles
are already unambiguous.

### Decision

Add a Project Service discovery pass for format-2 Verilog projects. It scans
`rtl/` and `tb/` recursively on every project load, ignores symlinks and hidden
generated directories, sorts paths deterministically, and merges new `.v`
files into the owned in-memory configuration without rewriting the manifest.
Explicit manifest entries remain authoritative and continue to support sources
outside the conventional directories.

For an undeclared file below `tb/`, Solar extracts Verilog `module`
declarations with a small lexical scanner. A module matching the file stem is
preferred; otherwise a file containing exactly one module is accepted. An
ambiguous file requires an explicit `[[test]]` entry. The discovered test name
comes from its safe file stem and the waveform comes from a literal `$dumpfile`
when present, falling back to `<test>.vcd`.

`solar up` exposes the same Core discovery result as a user-facing inventory
command. It does not mutate `solar.toml`; `check`, `test`, `simulate`, and
`synthesize` already consume the refreshed in-memory model automatically.

### Milestones and acceptance

1. Add deterministic filesystem discovery and Verilog module/waveform
   extraction with ownership-safe Core APIs and focused temporary-directory
   tests.
2. Merge discovered RTL/testbench sources and named tests during project load,
   preserving explicit entries and format-1/YANC behavior.
3. Add the thin `solar up` command, update help/docs, and verify a project whose
   manifest contains no RTL array or `[[test]]` tables.
4. Run the complete Debug suite and a real Icarus/Yosys flow using newly added
   files that never appear in `solar.toml`.

### Risks and limits

- Verilog elaboration cannot be inferred perfectly without a full language
  parser. Ambiguous testbench files fail with an actionable request for an
  explicit top instead of guessing.
- Synthesis top is inferred only for a single discovered RTL module or an exact
  `[project].name` module match. Other multi-module designs still require an
  explicit top because their root cannot be derived reliably without full
  elaboration.
- Discovery does not remove stale explicit entries. A missing path written by
  the user remains an error rather than being silently forgotten.

### Progress

- 2026-07-13: Core discovery, recursive sorted enumeration, Verilog module and
  waveform inference, `solar up`, ambiguity warnings, and focused tests were
  implemented. The default Verilog template now contains no explicit source or
  test entries.
- 2026-07-13: all 21 Debug CTests passed except the three explicitly skipped
  opt-in YANC integrations. A separate manifest with no RTL array and no test
  tables passed `solar up`, `solar check`, real Icarus/VVP testing, and real
  Yosys synthesis.

## v0.2.0 implementation plan (2026-07-12)

### Baseline audit

The existing v0.1.0 implementation is functional and is being evolved rather
than replaced. A fresh GCC debug configure/build passed all 9 registered CTests,
including real Icarus Verilog and Yosys integrations.

The repository initially differed from the prompt's expected baseline in these
concrete ways:

- `CHANGELOG.md`, `docs/testing.md`, and CMake install rules did not exist;
  all are now implemented.
- The counter example initially used format 1 and now demonstrates format 2.
- Clang is not installed in this environment.
- No YANC executable was initially on `PATH`; an official v5.2 release was then
  supplied explicitly for manual integration validation.
- The directory is still not a valid Git worktree, so no commit or diff history
  can be used as a safety net.

The YANC repository, README, Linux scripts, executable interfaces, output
layout, current commit, and v5.2 release were inspected before finalizing
backend arguments. Solar does not download, build, install, or modify YANC at
runtime.

### v0.2 architecture

The current Core/backend/CLI boundary remains. New responsibilities are added
as small services rather than folded into the CLI:

1. The Project Service loads format 1 or format 2 and always exposes one
   normalized in-memory model containing profiles and named tests.
2. The Effective Configuration service combines global, selected-profile, and
   selected-test include/define lists without mutating the manifest model.
3. The Test Service owns selection, list/all behavior, isolated artifact paths,
   summaries, and reuse by `solar simulate`.
4. The Compiler Service owns compiler-backend dispatch and the structured
   result contract. The YANC backend owns its verified tool-specific staging,
   validation, and publication lifecycle through shared Core helpers.
5. Compiler backends are separate from simulation/synthesis backends. YANC is
   a compiler backend; Icarus and Yosys consume structured source/artifact sets.

The intended data flow is:

```text
CLI -> Project/Test/Compiler services -> backend adapters -> shared runner
```

### Decisions and alternatives

- Extend the documented Solar TOML subset with integer values and repeated
  `[[profile]]`/`[[test]]` tables. A general TOML dependency is still rejected:
  the required syntax is bounded and no new production dependency is needed.
- Normalize format 1 immediately after parsing. It receives one implicit test
  named `default`; the manifest is never rewritten. The CLI may print one
  migration warning from normalized project metadata.
- Store v2 arrays as owned dynamic arrays with matching init/free functions.
  All merge functions return new lists, remove duplicates, and preserve first
  occurrence order.
- Represent a backend invocation through request objects. Icarus and Yosys will
  no longer derive all inputs directly from legacy config fields, which allows
  generated YANC artifacts to be supplied without filesystem probing.
- Keep test execution sequential. `--all` continues after individual test
  failures and returns a summary plus a non-zero aggregate status.
- Use fixed, validated test/profile/processor names as directory components.
  Waveforms are safe relative paths under each isolated test directory.
- `SolarProcessSpec` exposes validated child-only environment additions while
  current backends leave them empty. Global `PATH` or `chdir` mutation remains
  prohibited.
- YANC root resolution order is environment, manifest root, then safe `PATH`
  derivation. Checkout and release layouts are represented explicitly and the
  toolchain root is always read-only from Solar's perspective.
- Compiler publication uses a sibling staging directory below `.solar/tmp` and
  Linux `RENAME_EXCHANGE` only after artifact validation. A prior valid build
  remains continuously visible until the complete candidate replaces it.
- Real YANC support will be reported only for languages exercised with an
  actual toolchain. Fake executables validate ordering, arguments, failure
  handling, layout resolution, and publication but do not prove language
  semantics or upstream compatibility.

### v0.2 milestones and acceptance

#### Milestone 1: project model v2

Status: complete.

- Parse format 2, tests, profiles, includes, defines, compiler/YANC sections,
  and positive integer settings.
- Normalize format 1 and expose selection/effective-config APIs.
- Validate names, defaults, paths, defines, language/extension compatibility,
  and duplicate tables.
- Acceptance: format-1 tests stay green and focused v2 parser/effective-config
  tests pass without invoking EDA tools.

#### Milestone 2: Test Service

Status: complete in implementation and focused tests.

- Implement list, default selection, named selection, `--all`, profiles, and
  isolated `solar-build/tests/<test>` plus per-test logs.
- Refactor Icarus to accept a structured test request with sources, includes,
  defines, top, working directory, and expected waveform.
- Make `simulate` delegate to the same service.
- Acceptance: the required two-test Verilog scenario passes, including paths
  with spaces, profiles, ordered `-I`/`-D` arguments, and aggregate summaries.

#### Milestone 3: Compiler Service

Status: complete. Native fake-backend tests cover failure/publication paths and
are registered in the canonical CTest suite.

- Add compiler request/result/artifact types and an opaque compiler-backend
  registry independent from simulation/synthesis capabilities.
- Add staging, required-artifact validation, and atomic publication helpers.
- Acceptance: a fake compiler backend proves success/failure publication and
  prior-build preservation without any YANC dependency.

#### Milestones 4-5: YANC compiler backend

Status: complete and validated with real YANC v5.2 for CMM, C++, and Assembly.
Native fake-stage and opt-in real-tool tests are registered in CTest.

- Resolve and validate checkout/release layouts and required tools.
- Implement inspected CMM, C++, and Assembly stage vectors and diagnostics.
- Preserve per-stage logs and stop after the first failing stage.
- Acceptance: C tests with fake executables verify exact stage order, arguments,
  includes, locale flags actually supported upstream, spaces, and missing
  artifacts. Real flows run only when `SOLAR_TEST_YANC_ROOT` is available.

#### Milestones 6-7: generated simulation and synthesis

Status: complete and validated through Solar with Icarus/VVP and Yosys for all
three YANC languages, including automated opt-in real-tool integration.

- Expose the implicit `generated` test from compiler artifacts.
- Feed generated RTL/testbench/HDL/MIF sets to Icarus and RTL-only sets to Yosys.
- Acceptance: fake generated artifacts prove input selection and isolation;
  actual end-to-end flows are run or explicitly skipped for absent YANC.

#### Milestone 8: templates, doctor, install, and documentation

Status: complete. Doctor, documentation, format-2/YANC templates, examples, and
CMake install rules are implemented and exercised.

- Generate format-2 Verilog and YANC templates without inventing upstream
  source syntax; add only examples supported by inspected official fixtures.
- Extend doctor, install rules, README, changelog, architecture, testing,
  profiles, compiler-backend, YANC, and migration documentation.
- Acceptance: installed binary works from a temporary prefix and docs clearly
  separate tested behavior, mocks, skips, and future scope.

#### Milestone 9: hardening

Status: complete for available tools. GCC Debug/Release, all 20 CTests, real
YANC, ASan/UBSan, installation, `-fanalyzer`, and shell/path safety review pass.
Clang is unavailable and LeakSanitizer is blocked by `ptrace`.

- Run GCC debug/release, CTest, sanitizers, external examples, safety scans, and
  Clang when available.
- Acceptance: all available tests pass, YANC integrations are explicitly
  skipped when `SOLAR_TEST_YANC_ROOT` is unset, no generated data escapes
  `.solar`, and no shell/runtime download path exists.

### v0.2 risks

- Upstream YANC CLI/documentation differs from the prompt in several places.
  The inspected interface wins; divergences and compatibility logic are
  recorded rather than hidden.
- YANC output paths may be coupled to its expected project layout. Staging must
  reproduce only the minimum verified layout without writing into YANC itself.
- Standalone Assembly lacks frontend-created metadata expected by YANC v5.2.
  Solar derives a validated instruction count from `app_log.txt`, writes the
  minimal staging-only `cmm_log.txt`, and creates a neutral PC map. This enables
  hardware generation and simulation but cannot provide high-level source-line
  correlation.
- The current parser is section-oriented and format 2 adds repeated tables.
  Parser state, ownership, and duplicate detection require focused regression
  tests before CLI work.
- Artifact publication is destructive if paths are confused. Only validated
  processor/test names and descriptor-relative `.solar` operations may reach
  recursive removal or rename.
- The requested scope is much larger than v0.1. Each milestone must remain
  buildable and testable; unverified YANC language claims are a release blocker,
  not a reason to weaken tests or documentation.

### v0.2 progress log

- 2026-07-12: baseline audited and 9/9 existing tests passed. YANC is absent
  locally and official-interface research started. Milestone 1 is next.
- 2026-07-12: official YANC `main` commit
  `ea6135af18735e66a2bd23445749d6a244990fcd` and release v5.2 at commit
  `792521b699a1f1e6f23a258653a9cf55e251abe1` were inspected. Initial direct
  CMM/C++ experiments established the real argv, outputs, and path behavior.
- 2026-07-12: Milestone 1 completed. Format-2 Verilog/YANC parsing, multiline
  arrays, repeated profiles/tests, format-1 normalization, safe validation,
  selection, and immutable effective configuration were added. All 11 CTests
  passed; Milestone 2 started.
- 2026-07-12: Test, Compiler, and Synthesis Services, structured generated
  artifacts, YANC resolver/backend, generated Icarus requests, profile-aware
  Yosys requests, doctor output, atomic publication, and per-stage logs were
  implemented.
- 2026-07-12: real YANC v5.2 CMM passed `solar compile`, generated test through
  Icarus/VVP/VCD, and profile-aware Yosys synthesis from a root path containing
  spaces.
- 2026-07-12: real YANC v5.2 C++ passed the same complete Solar path using the
  official `test21` fixture plus a local header in an include directory whose
  path contains spaces.
- 2026-07-12: real YANC v5.2 Assembly passed compile, simulation/VCD, and Yosys
  after Solar implemented a bounded compatibility bridge: validate `n_ins` from
  `app_log.txt`, emit staging-only `num_ins` metadata, and create a neutral PC
  map. The upstream unchecked `cmm_log.txt` assumption remains documented.
- 2026-07-12: `solar init` gained format-2 Verilog, YANC CMM, YANC C++, and YANC
  Assembly templates. The repository gained the two-test counter plus small
  YANC examples traced to official v5.2 fixtures, each with its own README.
- 2026-07-12: native fake YANC tests passed standalone for resolver precedence,
  checkout/release layouts, all language stage argv/order, supported locale
  flags, spaces, failure stop, missing artifacts, atomic publication, prior-build
  preservation, and Assembly compatibility sidecars.
- 2026-07-12: canonical Debug and Release suites passed 20/20 with real YANC
  CMM/C++/Assembly enabled. Without `SOLAR_TEST_YANC_ROOT`, those three tests
  report explicit CTest skips.
- 2026-07-12: ASan/UBSan passed all exercised tests with leak detection disabled
  because LeakSanitizer cannot initialize under this executor's `ptrace` policy.
  GCC `-fanalyzer` is clean for production modules except intentional
  stdout/stderr descriptors inherited across `execvp`.
- 2026-07-12: CMake Release installation to `$HOME/.local` installed the CLI,
  static Core library, public headers, and documentation; the installed binary
  reported `solar 0.2.0`.
- 2026-07-12: the required literal `processor` scenario passed real YANC v5.2
  compilation, generated Icarus/VVP simulation, and release-profile Yosys
  synthesis. Solar published a renamed `solar_yanc_processor_core` locally to
  avoid YANC's generic `processor` module collision without modifying YANC.
- 2026-07-12: final hardening rejected explicit `format = 0`, moved every
  fallible YANC artifact check before publication, replaced rollback swaps with
  Linux `RENAME_EXCHANGE`, classified test failures, forwarded/reaped parent
  interruptions, and added child-only environment additions.
- 2026-07-12: the suite grew to 20 tests with focused negative format-2
  validation and a full two-test Verilog CLI integration. Real YANC tests now
  cover check/list, named profiled tests, local C++ headers, CMM `simulate`, and
  Assembly source immutability.

## Historical v0.1 foundation plan

The initial project snapshot contained only its implementation brief. There
was no existing build, source, test, example, or documentation tree to
preserve, and the directory was not yet a Git worktree.

Local tools discovered on 2026-07-11:

- CMake 4.4.0 and GCC 16.1.1 are available.
- Icarus Verilog (`iverilog` and `vvp`), Yosys, and GTKWave are available.
- Clang is not installed, so GCC is the compiler exercised in this iteration.

## Proposed architecture

Solar is split into three layers:

1. `solar_core` owns project discovery, the documented Solar manifest subset,
   semantic validation, diagnostics, safe filesystem operations, process
   execution, backend lookup, and high-level workflow entry points.
2. `src/backends/` owns every Icarus- and Yosys-specific argument, artifact,
   script, and tool check. Backends invoke only the shared runner.
3. `src/cli/` owns argument parsing, presentation, and centralized mapping from
   `SolarResult` to process exit codes.

Public values use explicit ownership: constructors/loaders allocate their
returned strings and arrays, and a matching `*_free()` function releases them.
Diagnostics use caller-owned fixed-capacity text fields so error reporting does
not introduce a second allocation failure while handling an existing failure.

## Decisions and alternatives

- Implement only the documented Solar TOML subset. A small line-oriented parser
  accepts known tables, quoted strings, and arrays of quoted strings. Pulling in
  a full TOML dependency was rejected for the first milestone because it adds
  packaging and licensing surface without a current requirement.
- Keep parsing and validation as separate public operations. Loading produces a
  configuration; validation checks required values, supported values, and files.
- Use dynamically allocated paths and source arrays. Fixed `PATH_MAX` buffers
  were rejected because valid Linux paths need not fit that interface reliably.
- Use `fork`, `execvp`, descriptor redirection, an exec-error pipe, and `waitpid`.
  No shell is involved, so spaces and untrusted arguments remain distinct.
- The runner writes stdout and stderr to explicit log files or captures up to
  1 MiB per stream when no log path is supplied. `SolarProcessResult` reports
  normal exit, signal termination, and exec failure; backends keep the original
  logs and return a structured Solar result.
- Backend registration is a small static registry of backend descriptors. The
  Core asks by name and capability; tool-specific branching stays in backend
  modules. Runtime plugin loading is outside the current scope.
- Generated paths are always derived from a loaded project root. Layout,
  artifact reset, and cleanup use descriptor-relative `fstatat`, `openat`, and
  `unlinkat` operations with no-follow flags. Symlinks are never followed.
- CLI exit codes are centralized: success 0, usage 2, project/configuration 3,
  missing tool 4, external process 5, and I/O/internal errors 1.

## Verifiable milestones

### Milestone 0: foundation

Status: complete.

- C17 CMake project, `solar_core`, `solar`, warnings, sanitizer option.
- Result and diagnostic types, `--help`, `--version`, initial CTest.
- Acceptance: clean configure, build, and CTest pass.

### Milestone 1: projects and manifests

Status: complete.

- Project-root discovery, manifest parser, semantic/path validation.
- Functional `solar init` and thin `solar check` commands.
- Parser, validation, discovery, and CLI behavior tests.
- Acceptance: valid counter passes; syntax, missing-key, and missing-file cases
  produce actionable diagnostics without invoking EDA tools.

### Milestone 2: filesystem and runner

Status: complete.

- Project-local build layout, safe recursive cleanup, POSIX process runner.
- Functional `solar clean`; success, non-zero, missing-executable, output, and
  cleanup safety tests.
- Acceptance at that milestone: only the resolved project `.solar` tree could
  be removed. The current visible-build milestone extends this safely to the
  project-owned `solar-build` tree, and a
  process can be run with arguments and logs without a shell.

### Milestone 3: doctor and Icarus

Status: complete.

- Executable discovery/version probing, backend registry, Icarus compile/run.
- Functional `solar doctor`, `solar simulate`, and optional `sim` alias.
- Acceptance: the counter example produces its executable, VCD, and logs below
  `.solar`; unavailable tools are reported rather than crashing.

### Milestone 4: Yosys

Status: complete.

- Deterministic Yosys script, RTL-only synthesis, netlist/report/log artifacts.
- Functional `solar synthesize` and optional `synth` alias.
- Acceptance: the counter example synthesizes without including its testbench.

### Milestone 5: hardening and documentation

Status: complete with the environment limitations recorded below.

- Complete unit/integration tests, sanitizer run, ownership/path/shell review.
- README, architecture, project-format, backend-contract, and honest roadmap.
- Acceptance: debug and sanitizer suites pass, real available-tool flows pass,
  and documentation matches exercised behavior.

## Risks and mitigations

- A partial parser can accidentally imply TOML compatibility. Documentation and
  diagnostics will call it the Solar TOML subset and reject unsupported syntax.
- Failures between `fork` and `exec` are easy to misclassify. A close-on-exec
  pipe communicates `errno` from the child to the parent.
- Recursive deletion can cross trust boundaries. Cleanup validates the basename
  and parent, refuses a symlink at the root, and never follows entry symlinks.
- Tool version flags and output streams vary. Doctor probes each known tool with
  backend-owned arguments and treats version text as informational.
- External tools can be absent in other environments. Unit tests remain tool
  independent; integration tests explicitly report skips.
- Clang portability cannot be exercised locally. C17/POSIX interfaces and strict
  warnings are used, and the untested compiler is documented rather than claimed.

## Global acceptance criteria

- All six canonical commands exist and keep the CLI thin.
- No shell execution, implicit installation, global mutation, or GUI launch.
- All generated artifacts remain inside the active project's `.solar` tree.
- Normal and failure paths have tests and actionable diagnostics.
- The example is built and exercised with every available required backend.
- Build, CTest, sanitizer results, skips, and remaining limits are reported.

## Progress log

- 2026-07-11: repository audited; implementation plan and initial decisions
  recorded before source creation. Milestone 0 started.
- 2026-07-11: Milestone 0 completed. GCC debug configure/build succeeded and
  the initial result/diagnostic CTest passed. Milestone 1 started.
- 2026-07-11: Milestones 1 and 2 completed. Manifest/project, runner,
  filesystem, backend-construction, and CLI tests cover syntax, semantics,
  discovery, safe cleanup, shell-free execution, captured output, missing
  executables, non-zero exit, signals, symlinks, and exit-code mapping.
- 2026-07-11: Milestones 3 and 4 completed. `doctor` found Icarus 13.0, VVP
  13.0, Yosys 0.66, and GTKWave. The counter example simulated to a VCD and
  synthesized to a netlist/statistics report. The generated Yosys script was
  inspected and contained RTL only. Both failure flows returned exit 5,
  displayed original tool stderr context, and retained complete logs.
- 2026-07-11: Milestone 5 completed. The debug suite passed all 9 CTests,
  including real Icarus and Yosys integrations. ASan and UBSan passed the same
  9 tests with leak detection disabled. LeakSanitizer itself cannot initialize
  under this executor's `ptrace` environment, and Valgrind is not installed, so
  independent leak checking remains an environment limitation. GCC `-fanalyzer`
  found and prompted fixes for rare descriptor/error paths; its remaining two
  warnings are the intentional stdout/stderr descriptors inherited across
  `execvp`. Clang is unavailable and was not claimed as exercised.

## Verification evidence

Commands completed during the final verification:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSOLAR_ENABLE_SANITIZERS=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-analyzer \
  -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_C_FLAGS=-fanalyzer -fdiagnostics-path-format=none"
cmake --build build-analyzer
```

The standard and ASan/UBSan runs each passed 9 of 9 tests. No integration test
was skipped because all required external tools were available. The default
sanitizer CTest was also attempted and failed solely at LeakSanitizer startup
with its documented `ptrace` incompatibility before test logic ran.
# Report de benchmark e ambiente (v0.4)

## Estado atual

- O Build Service persiste o ultimo resultado em
  `.solar/state/last-report.txt` e `solar report` apenas o le.
- As etapas de alto nivel possuem duracao em milissegundos, mas os processos
  externos individuais ainda nao expoem suas medicoes ao Core.
- Versoes das ferramentas sao consultadas com o runner seguro, enquanto dados
  do host e separacao entre compilacao da simulacao e runtime nao sao
  registrados.

## Decisoes

- Medir tempo de parede com `CLOCK_MONOTONIC` no runner, em nanossegundos. A
  medicao inclui criacao, execucao, espera e drenagem da saida do processo; nao
  representa tempo de CPU nem substitui repeticoes estatisticas.
- Propagar tempos tipados para elaboracao RTL, cada estagio YANC, compilacao da
  simulacao, runtime da simulacao e Yosys. Manter tambem o tempo total das
  etapas do pipeline para evidenciar overhead do Solar e publicacao.
- O adaptador privado cocotb gravara tempos separados de `runner.build()` e
  `runner.test()` em `.solar/tmp/`, porque ambos ocorrem dentro de um unico
  processo Python visto pelo runner POSIX.
- Coletar a fotografia do host sem executar shell: sistema operacional, kernel,
  arquitetura, hostname, modelo de CPU, CPUs logicas e memoria total. Nao
  registrar ambiente completo nem dados potencialmente secretos.
- Persistir um relatorio textual autocontido e agradavel, com valores humanos e
  nanossegundos brutos. Isso preserva `solar report` como operacao somente de
  leitura e deixa o resultado citavel sem depender do manifesto ainda valido.

## Marcos verificaveis

1. Runner mede processos e testes cobrem uma duracao nao negativa.
2. Backends e servicos propagam tempos individuais, inclusive falhas.
3. Build Service coleta host e versoes e escreve tabelas deterministicas.
4. Testes CLI validam secoes, metricas e leitura de build falho.
5. GCC, CTest, sanitizers e exemplos reais disponiveis passam.

## Riscos e limites

- Uma unica execucao sofre ruido de escalonamento, cache e carga do host; o
  relatorio deve declarar essa limitacao para uso academico honesto.
- Frequencia instantanea, temperatura, energia e afinidade de CPU nao sao
  portaveis nem estaveis e ficam fora deste marco.
- Versoes sao capturadas logo apos o build por invocacoes seguras de diagnostico;
  essas sondagens nao entram no tempo total reportado.

## Criterios de aceitacao

- O terminal mostra status, ambiente, ferramentas, configuracao e artefatos em
  secoes legiveis.
- Cada ferramenta realmente executada possui tempo proprio, e cada simulacao
  distingue compilacao, runtime e total quando o backend permite.
- Duracoes incluem unidade legivel e valor inteiro em nanossegundos.
- `solar report` nao executa ferramentas e continua exibindo builds falhos.
- O relatorio explica o escopo da medicao e nao apresenta um unico run como
  benchmark estatisticamente conclusivo.

## Resultado

- Concluido em 2026-07-20. O runner e todos os resultados tipados propagam
  nanossegundos monotonicamente medidos; cocotb possui medicao interna separada
  de build/test.
- `solar report` apresenta overview, host, toolchain, configuracao, tempos,
  artefatos, logs e diagnostico em um snapshot textual atomico.
- GCC Debug passou 27 testes e registrou um skip explicito da integracao real
  cocotb entre 28 testes. Icarus, VVP, Yosys e YANC CMM/C++/Assembly reais
  passaram, inclusive relatorios Verilog e YANC inspecionados no terminal.
- ASan/UBSan passou a mesma matriz com `ASAN_OPTIONS=detect_leaks=0`;
  LeakSanitizer permanece bloqueado pela politica `ptrace` do executor. Clang
  nao esta instalado.

# Auditoria do backend Verilator (v0.4)

## Estado e decisoes

- O backend tipado, o lint RTL, VCD/FST e a ponte cocotb ja existem, mas o teste
  do backend usa um executavel falso e a unica integracao real exige cocotb.
- Verilator 5.050 foi encontrado localmente. Um fluxo Verilog real revelou que
  warnings comuns encerram `--binary` por padrao; Solar usara `-Wno-fatal` para
  preservar o warning no log sem impedir uma simulacao valida.
- O GNU Make fornecido pelo Verilator recusa qualquer diretorio de build cujo
  caminho absoluto contenha espacos. Como temporarios devem permanecer em
  `.solar/`, o backend recusara esse caso antecipadamente com diagnostico
  acionavel, em vez de prometer suporte que a ferramenta nao oferece.
- Fontes e includes com espacos continuam sendo transmitidos por `argv` sem
  shell; a restricao se aplica ao caminho do projeto/workspace de simulacao.

## Marcos e criterios de aceitacao

1. Cobrir `-Wno-fatal`, VCD/FST e argumentos separados no teste unitario.
2. Cobrir a recusa de workspace com espacos sem executar o backend.
3. Executar um projeto Verilog real com Verilator, publicar a waveform e expor
   o `$display` pelo CLI.
4. Manter o skip cocotb explicito quando cocotb 2.x estiver ausente.
5. Passar a suite GCC e sanitizers; executar Clang somente se disponivel.

## Riscos e limitacoes

- `-Wno-fatal` nao oculta warnings: eles permanecem no stderr detalhado. Erros
  reais e retornos nao zero continuam falhando.
- Suporte real a cocotb continua condicionado ao pacote externo cocotb 2.x e
  deve ser reportado separadamente da simulacao Verilog nativa do Verilator.

## Resultado

- Concluido em 2026-07-21 contra Verilator 5.050. O fluxo Verilog real passou
  para VCD e FST, publicou a waveform e apresentou `$display` no terminal.
- O novo CTest `integration_verilator` passa sem cocotb; a integracao cocotb
  continua como skip explicito porque cocotb 2.x nao esta instalado.
- As matrizes GCC Debug, Clang 22.1.8 e ASan/UBSan com
  `ASAN_OPTIONS=detect_leaks=0` tiveram 28 passes e um skip cocotb explicito;
  nao houve regressao nos fluxos Icarus, Yosys ou YANC reais.
- O Release foi instalado em `~/.local`; o binario resolvido pelo `PATH` passou
  a integracao Verilog/Verilator real e deixou o diretorio temporario limpo.

# Projetos RTL-only e simulacoes sem waveform (v0.4)

## Estado e decisoes

- A descoberta convencional ja aceita `rtl/` sem arquivos em `tb/`, mas a
  validacao semantica exige pelo menos um teste. Isso impede `solar scan` de
  persistir um inventario RTL-only e tambem bloqueia `check`, `build rtl` e
  `build synth` para projetos legitimamente sem testbench.
- Projetos Verilog com zero testes passarao a ser validos. Uma operacao de
  simulacao sem teste selecionavel continuara falhando com o diagnostico
  especifico do Test Service; a ausencia de testes nao sera confundida com uma
  fonte RTL invalida.
- Quando `scan` remover o ultimo testbench convencional, ele removera tambem
  `project.default_test` somente se esse valor apontava para um teste gerenciado
  pelo proprio scan ou se o manifesto delegava todos os testes a descoberta.
  Defaults ligados a testes explicitos fora de `tb/` continuam preservados e
  sujeitos a validacao normal.
- O codigo de saida zero do simulador define o sucesso do teste. VCD/FST passa a
  ser um artefato opcional: se nao existir, nada sera publicado/registrado e a
  CLI emitira um warning acionavel. Erros de compilacao, runtime, permissao ou
  arquivos existentes com tipo invalido continuam sendo falhas.

## Marcos verificaveis

1. Cobrir `scan` apos a remocao de todos os testbenches e validar o manifesto
   resultante com fontes RTL e sem `[[test]]`/`default_test` orfao.
2. Cobrir Icarus/Test Service retornando sucesso sem criar a waveform, com
   resultado PASS, caminho publico nulo e nenhum registro de artefato.
3. Cobrir a apresentacao do warning no CLI e manter Icarus, Verilator e cocotb
   coerentes com a politica de waveform opcional.
4. Executar a suite completa com GCC, Clang e ASan/UBSan, alem de um fluxo real
   Icarus sem `$dumpfile` quando a ferramenta estiver disponivel.

## Riscos e criterios de aceitacao

- `solar scan` nao pode remover defaults de testes explicitamente gerenciados
  pelo usuario fora do layout convencional.
- Uma waveform antiga nao pode fazer um teste novo parecer ter gerado ondas; o
  workspace temporario continua sendo limpo antes de cada execucao.
- A falta de VCD/FST nunca encobre retorno nao zero do testbench.
- `solar view` continua dependendo de uma waveform registrada e nao executa
  simulacao para cria-la.

## Resultado

- Concluido em 2026-07-21. O manifesto criado por `solar init` foi exercitado
  apos remover `tb/basic.v`; `solar scan` publicou um inventario com RTL, zero
  testes e sem `default_test` orfao, e o projeto resultante passou na validacao.
- O Test Service deixa `waveform_path` nulo quando a execucao retorna zero sem
  VCD/FST. Icarus, Verilator e o adaptador cocotb compartilham essa politica; a
  CLI apresenta warning e o relatorio registra `not generated`.
- O novo teste de integracao Icarus executa um testbench real sem `$dumpfile`,
  confirma PASS e `$display`, preserva executavel/logs internos e confirma que
  nenhum arquivo foi publicado em `sim/`.
- GCC Debug, Clang Debug e ASan/UBSan com `ASAN_OPTIONS=detect_leaks=0`
  completaram 29 passes em 30 testes. A integracao cocotb foi o unico skip
  explicito porque cocotb 2.x nao esta instalado; Icarus, Yosys, Verilator e os
  fluxos YANC reais passaram.

# Progresso de `solar build sim` (v0.4)

## Estado atual e arquitetura proposta

- `BuildContext` ja mede etapas e o runner usa `poll()` com espera finita, mas
  nenhuma notificacao e emitida enquanto uma ferramenta esta ativa. Quando um
  log e configurado, stdout/stderr vao diretamente ao arquivo e nao podem ser
  apresentados por `--verbose` durante a execucao.
- Sera criado um contrato pequeno de observacao de progresso, sem ownership e
  sem estado global. Build, Compiler, RTL e Test Services emitirao eventos
  semanticos; o runner emitira heartbeat e chunks de saida pelo mesmo loop de
  `poll()`. A CLI sera a unica responsavel por barra, spinner, TTY e texto.
- Streams de ferramentas serao drenados por pipes e copiados para os logs pelo
  processo pai quando houver observador de saida. Isso preserva os logs, evita
  deadlock por pipe cheio e permite `--verbose` sem threads.
- O renderer reutilizavel da CLI recebera um plano Verilog ou SAPHO. Para um
  teste, usara 5/10/20/40/85/95/100; SAPHO reservara uma faixa anterior para
  YANC. Em `--all`, cada teste ocupara uma fracao deterministica da faixa de
  simulacao, impedindo regressao do percentual.

## Marcos verificaveis

1. Estender o runner com callbacks opcionais de atividade/saida, tee seguro para
   logs, preservacao de sinais e testes de processo longo/interrupcao.
2. Propagar o observador pelo `BuildContext`, RTL, Compiler/YANC, Test Service e
   backends Icarus/Verilator/cocotb sem duplicar orquestracao no CLI.
3. Implementar renderer TTY e linear, `--no-progress`, `--verbose`, finalizacao
   de erro e resumo de tempos por etapa.
4. Cobrir percentuais, tempo, TTY, nao-TTY, opcoes, falhas de estagio e
   heartbeat; depois executar GCC, Clang, ASan/UBSan e simulacao real.

## Riscos e criterios de aceitacao

- Progresso interno do simulador nao sera estimado: durante o processo o
  percentual permanece fixo e apenas tempo/spinner mudam.
- A UI nao pode alterar exit code, encaminhamento de Ctrl+C, logs preservados ou
  `$display`; callbacks ausentes devem manter o comportamento atual.
- ANSI/retorno de carro so podem aparecer quando stdout e um TTY e progresso
  esta habilitado. Saida redirecionada deve conter uma linha por transicao.
- `--verbose` deve fazer tee, nunca substituir os logs internos, e
  `--no-progress` deve manter o resumo final.

## Resultado

- Implementado em 2026-07-21 com um observador sincrono reutilizavel. Core e
  backends emitem estagios sem conhecer terminal; o renderer da CLI concentra
  TTY, barra, spinner, relogios e saida linear.
- O runner reutiliza seu loop de `poll()` para heartbeat e, somente em verbose,
  drena pipes e faz tee dos chunks para callback e logs. Nenhuma thread ou
  shell foi introduzida; encaminhamento de SIGINT/SIGTERM/SIGHUP foi preservado.
- `build sim` mede e apresenta validacao, resolucao, compilacao, execucao,
  publicacao e total. Projetos YANC reservam uma faixa propria para geracao e
  continuam reutilizando o resultado estruturado do Compiler Service.
- Testes focados cobrem percentuais, relogio, TTY/nao-TTY, opcoes, todas as
  transicoes de falha e um processo lento com atividade continua. A integracao
  real Icarus, inclusive sem waveform, passou antes da matriz completa.
- A matriz final registrou 31 CTests: GCC Debug, Clang 22.1.8 e ASan/UBSan
  tiveram 30 passes cada; apenas cocotb 2.x foi skipped por indisponibilidade.
  Icarus e YANC CMM foram executados de ponta a ponta com a nova apresentacao.
- Este conjunto, junto do report orientado a benchmark, da auditoria real do
  Verilator e do suporte RTL-only/waveform opcional, define a versao de
  desenvolvimento 0.4.0. A versao compilada, README, AGENTS, CHANGELOG,
  roadmap e o guia `migration-v0.3-to-v0.4.md` foram sincronizados; o formato
  do manifesto permanece 2 e nenhuma tag foi criada.

# Site institucional do Solar (v0.3)

## Objetivo

Refatorar a landing page estática em `../Site-Solar` para comunicar somente o
comportamento comprovado no Core, CLI, exemplos e testes de Solar 0.3. A página
deve manter o estilo monocromático técnico, explicar o fluxo SAPHO como uma
integração de primeira classe e separar o que existe hoje das direções futuras.

## Decisões

- A página continuará estática e sem dependências de framework.
- Links, versão, comandos, ferramentas e roadmap serão centralizados em um
  único módulo de dados; CTAs não usarão destinos vazios.
- Exemplos visuais serão derivados da CLI e dos arquivos do repositório, com
  rótulos claros quando representarem uma visualização de saída.
- O canvas ASCII será limitado, pausado fora da viewport/aba visível e
  simplificado para movimento reduzido.
- Não haverá afirmação de benchmark, tamanho de binário ou compatibilidade que
  não tenha evidência local reprodutível.

## Verificação planejada

1. Configuração limpa, build e CTest em diretório novo.
2. Execução dos exemplos e quick start conforme as ferramentas externas locais.
3. Validação estática de HTML/JavaScript, CTAs, responsividade e movimento
   reduzido.
4. Inspeção de problemas locais e seguros no Core/CLI sem refatoração ampla.

## Resultado

- Confirmado `Solar 0.3.0` em `CMakeLists.txt` e no cabeçalho público; Core e
  CLI compilaram em uma árvore Debug limpa em `/tmp/solar-site-verify`.
- O exemplo `examples/counter` executou `check`, `build sim --list`, `build
  full --dry-run`, `build full --profile debug` e `report`. A execução real
  publicou duas VCDs, netlist e relatório; `solar clean --all` removeu os
  quatro artefatos registrados em seguida.
- A matriz CTest confirmou 28 passes e um skip explícito de cocotb, pois
  `cocotb-config` não está instalado. Icarus/VVP, Yosys, Verilator e as
  integrações YANC CMM/C++/Assembly passaram.
- A página foi reorganizada para usar somente afirmações confirmadas e uma
  saída real rotulada do exemplo. CSS e JavaScript foram reduzidos a um arquivo
  principal cada; o canvas ASCII observa visibilidade, aba oculta e movimento
  reduzido.

# Suporte ao viewer Surfer (v0.4)

## Estado inspecionado

- `Downloads/surfer_linux.zip` possui os executaveis ELF x86-64 `surfer` e
  `surver`; o arquivo tem SHA-256
  `17c8a5f4c50ff6de3f818c5b3eec1cd89dc1dee8114f9822f4ac9e9f0c1a18d8`.
- O cliente real responde `surfer 0.7.0 (git: 4281e79)` e aceita
  `surfer <wave.vcd|wave.fst|wave.ghw>`. O segundo binario e um servidor
  headless e nao participa da abertura local de artefatos.
- O servico atual em `waveform.c` valida VCD/FST e lanca somente `gtkwave` em
  processo destacado. `solar view` resolve exclusivamente artefatos
  registrados e nunca executa simulacao.

## Decisoes

- Manter viewers como ferramentas externas opcionais; nenhum binario do Surfer
  sera copiado, empacotado ou instalado automaticamente pelo Solar.
- Preservar GTKWave como selecao padrao compativel e adicionar
  `solar view ... --viewer surfer|gtkwave`. O CLI apenas interpreta a escolha;
  validacao, argv e lancamento destacado permanecem no Core.
- Manter `solar_waveform_open()` como API compativel para GTKWave e adicionar
  uma variante tipada para escolher o viewer. Nomes ou flags arbitrarias nao
  serao aceitos.
- Registrar `surfer --version` em `solar doctor --all`. O viewer continua
  opcional e sua ausencia nao torna o diagnostico do projeto uma falha.
- O suporte publicado permanece VCD/FST porque esses sao os artefatos gerados
  pelo Solar. GHW aceito pelo Surfer nao amplia as linguagens de simulacao do
  projeto, e `surver` fica fora deste marco.

## Marcos verificaveis

1. Generalizar o servico de waveform com selecao tipada e diagnosticos por
   viewer, preservando seguranca, modo headless e `SOLAR_NO_VIEWER`.
2. Estender parser/help do `solar view` sem compilar ou simular artefatos.
3. Cobrir argv com paths contendo espacos, ambos os viewers, viewer invalido,
   ferramenta ausente, modo nao interativo e `doctor --all`.
4. Executar build/CTest GCC, sanitizers e uma verificacao real de versao/CLI
   contra o Surfer 0.7.0 encontrado; registrar qualquer limite grafico.

## Criterios de aceitacao e riscos

- `solar view basic --viewer surfer` deve executar exatamente `surfer <path>`
  sem shell e sem invocar simuladores.
- `solar view` sem opcao continua abrindo GTKWave.
- Falta de sessao grafica, artefato invalido e executavel ausente produzem
  diagnosticos que nomeiam o viewer selecionado.
- O processo destacado nao pode deixar filhos zumbis nem bloquear o CLI.
- A verificacao real do binario nao implica que Solar distribui ou garante
  compatibilidade com versoes do Surfer diferentes da inspecionada.

## Resultado

- Implementado em 2026-07-22. `solar view [test] --viewer surfer` e
  `--waveform <file> --viewer surfer` reutilizam o registro de artefatos e o
  launcher destacado do Core; sem opcao, GTKWave permanece o padrao.
- O teste nativo cobre argv separado, path FST com espacos, ambos os viewers,
  ferramenta ausente, enum invalida e modo nao interativo. A CLI cobre help,
  selecao e garantia de que nenhum backend de simulacao e executado.
- `solar doctor --all` encontrou `$HOME/.local/bin/surfer` e registrou
  `surfer 0.7.0 (git: 4281e79)`. O exemplo counter foi simulado para VCD e
  aberto realmente pelo Surfer somente via `solar view`.
- O cliente fornecido pelo usuario foi instalado em `~/.local/bin`; ele nao foi
  adicionado ao repositorio nem ao pacote do Solar. `surver` permanece fora do
  escopo.

# Edicao transacional de configuracao (v0.4)

## Estado e semantica

- O usuario pode persistir fontes descobertas com `solar scan`, mas ainda
  precisa editar TOML manualmente para mudar nome, top de sintese ou teste
  padrao.
- A interface final e `solar config set` com uma ou mais opcoes `--name`,
  `--top` e `--test`. `--top` representa `[synthesis].top`; o top do
  testbench continua pertencendo ao teste nomeado e nao sera alterado
  implicitamente.
- Manifestos formato 1 serao recusados com orientacao para `solar scan`, pois
  misturam top de projeto/simulacao e nao possuem o modelo de testes nomeados.

## Arquitetura e seguranca

- Um Config Edit Service no Core lera o texto, substituirá somente as chaves
  gerenciadas nas secoes `[project]` e `[synthesis]`, e preservara o restante
  do manifesto.
- Strings serao serializadas com escape TOML do subconjunto Solar. A edicao
  sera escrita abaixo de `.solar/tmp/config/`, sincronizada, recarregada com o
  parser e validada com descoberta antes de um `rename()` atomico.
- O CLI somente interpretara opcoes e apresentara o resultado. Valores
  invalidos, teste inexistente, chave duplicada e falhas de I/O manterao o
  manifesto original byte a byte.

## Marcos e aceitacao

1. Implementar API tipada de edicao, substituicao textual focal e publicacao
   transacional.
2. Adicionar `solar config set` e help especifico na secao Project.
3. Testar atualizacao combinada, preservacao de comentarios/conteudo, no-op,
   rollback por default inexistente, formato 1 e argumentos invalidos.
4. Atualizar README, formato/procedimentos e changelog; executar GCC, Clang e
   ASan/UBSan conforme disponibilidade.

O comando esta aceito quando um manifesto formato 2 valido pode mudar as tres
propriedades em uma operacao, `solar check` continua passando e nenhuma
tentativa invalida altera `solar.toml`.

## Resultado

- Implementado em 2026-07-22 com `config_edit.c` no Core e CLI fina em
  `command_config.c`. As tres opcoes podem ser combinadas e valores ja
  serializados produzem no-op sem substituicao.
- A edicao preserva texto alheio, escapa strings, rejeita manifesto simbolico,
  escreve em `.solar/tmp/config`, valida parsing/descoberta/semantica e so entao
  substitui `solar.toml` por rename atomico.
- O candidato pode corrigir um erro semantico antigo, como `default_test`
  inexistente. Falha do candidato preserva o manifesto original byte a byte;
  formato 1 orienta a migracao explicita com `solar scan`.
- A matriz final possui 32 CTests. GCC Debug, Clang 22.1.8 e ASan/UBSan
  passaram 31; apenas cocotb 2.x foi skipped por indisponibilidade. Os testes
  Release focados tambem passaram e o binario 0.4.0 foi instalado em
  `~/.local/bin/solar`.

# Deteccao hierarquica e configuracao automatica pelo scan (v0.4.5)

## Objetivo e contrato

- `solar scan` deixa de apenas materializar listas de arquivos e passa a
  recalcular os tops convencionais de sintese e testbench antes de publicar
  `solar.toml` de forma transacional.
- `solar check` continua somente leitura e passa a exibir o top de sintese, o
  teste selecionado por padrao e todos os testes efetivos com seus tops.
- A deteccao permanece um analisador estrutural conservador do subconjunto
  comum de Verilog/SystemVerilog; nao executa Icarus, Yosys ou outro frontend.

## Decisoes de arquitetura

- Discovery coleta declaracoes e instanciacoes de modulos. O top de sintese e
  a unica raiz do grafo RTL; se houver varias, uma correspondencia unica com o
  nome do projeto desempata. Qualquer outra ambiguidade e erro acionavel.
- Em `tb/`, modulos-raiz sao testbenches e modulos instanciados sao auxiliares.
  Uma correspondencia com o nome-base do arquivo desempata multiplas raizes no
  mesmo arquivo; sem resultado unico, scan falha sem alterar o manifesto.
- Ao ser chamado, scan possui os campos convencionais: substitui sempre
  `[synthesis].top` e os tops de testes Verilog gerenciados abaixo de `tb/`.
  Testes cocotb, gerados ou com fontes nao convencionais permanecem intactos.
- A reescrita preserva comentarios e configuracoes nao gerenciadas, valida um
  candidato em `.solar/tmp/scan` e somente entao usa substituicao atomica.

## Marcos verificaveis

1. Implementar grafo hierarquico privado em Discovery, incluindo instanciacao
   parametrizada e diagnosticos com candidatos ambiguos.
2. Persistir o top detectado em manifestos formato 2 e usar o valor recalculado
   durante a migracao guardada do formato 1.
3. Ampliar a saida de check para projeto Verilog, RTL-only, multiplos testes,
   cocotb e YANC sem mudar sua natureza somente leitura.
4. Cobrir hierarquias, ambiguidades, substituicao, idempotencia e rollback;
   executar testes focados, CTest completo e ASan/UBSan.

## Criterios de aceitacao

- Uma hierarquia `chip -> leaf` detecta `chip` mesmo sem correspondencia com o
  nome do projeto e persiste esse valor no proximo scan.
- Um top antigo e substituido pelo unico top recalculado; multiplas raizes sem
  desempate deixam `solar.toml` byte a byte inalterado.
- Check mostra `Synthesis top`, `Default test` e a lista `Configured tests`,
  marcando o teste default explicito ou o unico teste implicito.

## Resultado

- Implementado em 2026-07-22 com um indice privado de modulos e instanciacoes
  em Discovery. Instanciacoes parametrizadas, auxiliares de testbench, desempate
  pelo nome do projeto e ambiguidades de sintese possuem cobertura dedicada.
- Scan agora substitui `[synthesis].top`, recalcula tops dos testes gerenciados,
  preserva configuracao nao gerenciada e mantem rollback byte a byte quando a
  hierarquia possui mais de uma raiz sem desempate.
- Check exibe o top de sintese, default explicito/implicito e todos os testes;
  RTL-only, multiplos testes sem default e YANC gerado foram exercitados na CLI.
- GCC Debug e os 32 CTests passaram, com apenas cocotb 2.x skipped por
  indisponibilidade. Clang passou build e os testes focados de discovery, scan,
  CLI e fluxo Verilog v2. ASan/UBSan passou a mesma matriz completa com leak
  detection desativado porque LeakSanitizer nao opera sob o ptrace do ambiente.
- Uma copia temporaria de `examples/counter` executou `solar scan` e
  `solar check`; o manifesto publicou `top = "counter"`, dois testes e o check
  apresentou `basic`/`counter_tb` como selecao padrao.
# Generic Synthesis Statistics (2026-07-25)

## Current state

- Yosys already runs a deterministic `stat -top <top>` command through
  `tee -o statistics.txt`, publishes the original text report, and preserves
  complete stdout/stderr logs.
- `SolarSynthesisResult` currently exposes only paths, duration, and status.
  `.solar/state/last-report.txt` is the persistent, human-readable report and
  older reports contain no structured synthesis-statistics section.

## Decisions

- Add a focused Core module and public API for generic Yosys statistics; keep
  parsing, ownership, persistence, and ordering outside the CLI and backend.
- Track presence per numeric field with a bitmask so an absent value is never
  rendered as zero. Bound input size, line size, cell-name length, and cell
  type count, and reject overflowing or malformed reported numbers.
- Prefer Yosys's `design hierarchy` summary when present. Otherwise aggregate
  module sections exactly once. Infer the module count from unique module
  headers because ordinary Yosys `stat` output does not emit that field.
- Store a versioned normalized block in `last-report.txt`, followed by the
  readable `Generic Synthesis Statistics` section. Public persisted-state
  loading reads that block and treats older reports without it as unavailable.
- Parsing is optional enrichment after successful Yosys execution. I/O or
  format diagnostics remain queryable on the synthesis result but do not turn
  a valid netlist build into a failure.

## Milestones and acceptance

1. Implement the typed model, parser, copy/free operations, deterministic cell
   sorting, and realistic fixture tests for complete, partial, malformed,
   empty, large, multi-module, and hierarchy-summary output.
2. Attach parsed data and report metadata to `SolarSynthesisResult` only after
   successful Yosys execution, while preserving the original public report.
3. Persist and reload normalized data compatibly with old reports, and render
   the requested terminal section without executing Yosys from `solar report`.
4. Update public headers, CMake, README, command/API documentation, and
   changelog; run focused tests, the full suite, and sanitizer verification.

## Risks and limitations

- Yosys `stat` is human-readable output rather than a stable interchange
  schema. Unknown lines are intentionally ignored, but malformed values on
  recognized lines are diagnosed.
- Module count is inferred and cell totals depend on the most authoritative
  section available. No FPGA-specific resource interpretation is attempted.

## Completion evidence

- The parser accepts both the traditional `Number of wires: 4` / `$add 1`
  layout and the compact Yosys 0.66 `4 wires` / `1 $add` layout observed on
  this machine. A real counter synthesis with Yosys 0.66 produced 1 inferred
  module, 4 wires, 10 wire bits, 3 public wires, 6 public wire bits, 2 cells,
  `$add=1`, and `$sdff=1`.
- Yosys 0.66 omits some zero-valued categories from its compact text output.
  Solar therefore reports memories, memory bits, and processes as
  `not reported` rather than inventing zero values; this makes the real result
  partial by design.
- The original `synth/statistics.txt`, netlist, complete Yosys logs, producing
  version, top, and normalized schema all remain available after publication.
- The final GCC suite passed all 33 tests; the optional real cocotb integration
  was the only explicit skip. Focused Clang and ASan/UBSan tests passed. LSan
  could not initialize under the environment's ptrace restrictions and was
  rerun with leak detection disabled rather than counted as passed.
