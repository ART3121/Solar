# Stored build reports

Solar preserves the human report and structured comparison data for every
build that reaches report publication, including failed builds.

```text
.solar/reports/
├── build-000001/
│   ├── report.txt
│   ├── metadata.dat
│   ├── synthesis-stats.dat
│   └── timings.dat
├── build-000002/
├── latest
└── sequence
```

Sidecars are line-oriented, percent-escaped, and carry `schema=1`. GSS presence
markers distinguish zero from an absent Yosys counter. Timing values use
unsigned 64-bit nanoseconds. Metadata records the project, top, profile,
backends, available tool versions, source/options fingerprints, waveform mode,
and a small execution-environment fingerprint.

## Commands

```sh
solar report                         # latest human TXT report
solar report list --limit 10         # stored records
solar report show build-000041       # one human TXT report
solar report compare                 # latest vs previous comparable
solar report compare --against build-000041
solar report compare build-000052 --against build-000041
solar report compare --summary
```

The installed Bash, Zsh, and Fish definitions obtain build IDs from the
structured history service, so `report show`, both explicit comparison sides,
and `--against` support TAB completion without parsing this command's table.

An explicit baseline is never substituted. Automatic selection walks backward
from `latest` and accepts the first record with comparable synthesis statistics,
simulation timings, or both. Synthesis and simulation compatibility are
evaluated separately, so one section can remain useful when the other is not.

## Timing meaning

`Simulation execution time` is host wall time measured with a monotonic clock
around the simulator process. `Simulation total` is independently measured
around the Solar simulation service and includes its orchestration overhead.

`Simulated HDL duration` is time advanced inside the HDL model. It is not a
performance measurement and is not inferred from wall time. Solar only compares
it when both records provide safely normalizable units.

Changes are descriptive. An increased duration is not automatically labelled a
regression, and a reduction is not automatically labelled an improvement.
Different tool versions, inputs, options, waveform modes, Solar versions, or
execution environments produce visible warnings.

Old records containing only `report.txt` remain listable and displayable. Solar
does not reconstruct structured data by scraping their formatted tables.

## Sidecar schemas

The deterministic `synthesis-stats.dat` format records every GSS field with an
explicit presence marker. Cell types use indexed keys and retain the names
reported by Yosys:

```text
schema=1
kind=generic-synthesis-statistics
metric.cells.present=1
metric.cells.value=24
cell.count=1
cell.0.type=$dff
cell.0.value=8
```

`timings.dat` stores wall-clock durations as integers, never as preformatted
decimal seconds:

```text
schema=1
kind=build-timings
timer.simulation_compile.present=1
timer.simulation_compile.nanoseconds=18245123
timer.simulation_execution.present=1
timer.simulation_execution.nanoseconds=48621432
timer.simulation_total.present=1
timer.simulation_total.nanoseconds=67429610
simulation.status=success
simulation.simulated_duration.present=0
```

`metadata.dat` uses the same `schema=1` key/value envelope. It records the
build ID, timestamp, status, project identity, top, profile, backend/tool
versions when available, input and option fingerprints, waveform mode, and a
limited OS/architecture/CPU environment fingerprint. Values are percent
encoded, bounded while reading, and written atomically as part of the record.

`latest` contains one build ID. `sequence` reserves the monotonically
increasing numeric portion, so a failed publication can leave a harmless gap
but cannot reuse an ID. `solar clean --all` explicitly removes all internal
`.solar/` state, including this history; ordinary `solar clean` preserves the
history while removing disposable state and registered public artifacts.

## Comparison rules

Project identity, GSS kind/schema, and synthesis top determine whether GSS can
be compared. Project identity, timings schema, simulator backend, testbench,
and normalizable units determine whether simulation timings can be compared.
Other differences remain visible as context warnings rather than silently
invalidating all results.

Missing counters remain `not reported`. A real zero remains zero. Cell rows are
sorted by absolute change magnitude and then name; report tables use adaptive
but consistent units within each timing row. Percentage output uses `new` when
the baseline is zero and the current value is nonzero, avoiding infinity and
NaN.
