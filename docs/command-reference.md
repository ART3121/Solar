# Command reference

Run `solar <command> --help` for the grammar compiled into the installed
version. All normal success output uses stdout; warnings, errors, and external
tool failure context use stderr.

## Project commands

| Command | Behavior |
| --- | --- |
| `solar init [--template verilog\|sapho]` | Create a new project without overwriting existing target files. |
| `solar scan` | Discover conventional RTL/tests and transactionally synchronize managed manifest fields. |
| `solar config set --name N --top T --test X` | Update any combination of the managed settings in one validated transaction. |
| `solar check` | Parse and validate without executing an EDA tool or changing the project. |
| `solar doctor [--all]` | Inspect required project tools or every supported/optional tool. |
| `solar clean [--cache\|--all]` | Remove only registered artifacts and selected `.solar/` state. |

`--test` receives a test name shown by `solar build sim --list`, not a filename
or module top. Format-1 projects must run `solar scan` before configuration
editing.

## Build commands

| Command | Behavior |
| --- | --- |
| `solar build rtl` | Elaborate Verilog or run the configured compiler service. |
| `solar build sim [name]` | Run RTL and one explicit/default/sole simulation. |
| `solar build sim --all` | Run every simulation sequentially and summarize all failures. |
| `solar build sim --list` | List tests without executing a backend. |
| `solar build synth` | Run RTL and profile-aware synthesis without testbench sources. |
| `solar build full` | Check, RTL, simulations in manifest order, then synthesis. |

Executable build targets accept `--profile NAME` and `--dry-run`. Simulation
also accepts `--no-progress` and `--verbose`. Dry-run validates and reports
planned stages without starting external tools.

## Artifact commands

| Command | Behavior |
| --- | --- |
| `solar view [test] [--viewer gtkwave\|surfer]` | Open the registered waveform for a test. |
| `solar view --waveform FILE [--viewer ...]` | Open a registered project waveform explicitly. |
| `solar report` | Display `.solar/state/last-report.txt` without building. |

Automatic GUI launch is never part of a build command.

### Generic Synthesis Statistics

When the last operation completed Yosys synthesis, `solar report` includes a
**Generic Synthesis Statistics** section. It records the producing Yosys
version and top, every available design-summary counter, and cell types sorted
by descending count then alphabetically. A reported zero is distinct from
`not reported`.

The section is normalized and persisted when the build report is written.
`solar report` never starts Yosys or reparses a live backend log. The original
`statistics.txt` remains a registered public artifact and the complete Yosys
stdout/stderr remain below `.solar/logs/`.

The data does not estimate FPGA LUTs, ALMs, DSPs, utilization percentages,
timing closure, placement, routing, or bitstreams.

## Exit status classes

| Exit | Meaning |
| --- | --- |
| `0` | Success |
| `1` | I/O or internal failure |
| `2` | Invalid CLI argument |
| `3` | Project/configuration/not-found error |
| `4` | Required external tool missing |
| `5` | External process or logical test failure |
