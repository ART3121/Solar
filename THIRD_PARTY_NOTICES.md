# Third-party notices

## YANC

Solar vendors the YANC compiler toolchain sources from
<https://github.com/nipscernlab/yanc>.

- Upstream revision: `ea6135af18735e66a2bd23445749d6a244990fcd`
- Reported toolchain version: 5.2
- License: MIT
- Copyright: 2026 NIPSCERN

The unmodified upstream license is installed beside this notice. Solar builds
the five required command-line compilers (`cmmcomp`, `cpppp`, `cppcomp`,
`appcomp`, and `asmcomp`) from a staged copy and invokes them through the same
shell-free process runner used for other EDA tools. Solar also builds and
installs `gen_gtkw` for Assembly-only GTKWave presentation. The optional
complex-value helper `comp2gtkw` is not built or installed.

The vendored snapshot carries local integration, compatibility, and hardening
patches. They include:

- renaming the internal CMM flag `fsqrt` to `use_fsqrt` for current glibc;
- portable named CLI arguments, bilingual diagnostics, and deterministic
  creation/validation of generated output directories;
- Verilator-compatible generated HDL and trace guards;
- an opt-in `gen_gtkw --assembly-only` mode used by Solar without changing the
  helper's existing full-layout interface;
- compatibility sidecars derived from validated `appcomp` output for direct
  Assembly input;
- bounded complex-literal parsing, checked compiler streams, deterministic
  simulator metadata, fail-fast allocation handling, and explicit rejection
  of symbols that exceed legacy fixed-buffer capacities.

These patches preserve the documented YANC command-line pipeline. The exact
integration constraints and observed upstream differences are documented in
`docs/yanc.md`.
