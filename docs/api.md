# Solar Core public API

Solar Core is the reusable C17 boundary used by the CLI. Installed public
headers live below `include/solar/`, and `solar/solar.h` includes the complete
supported surface.

## Generic Synthesis Statistics

`solar/synthesis_statistics.h` exposes normalized generic statistics from the
Yosys `stat` artifact. The structure owns its metadata strings and cell table.
Always initialize it before use and free it when finished:

```c
SolarGenericSynthesisStatistics statistics;
SolarResult result;

solar_synthesis_statistics_init(&statistics);
result = solar_synthesis_statistics_load_last_report(".", &statistics);
if (result.status == SOLAR_STATUS_OK && statistics.available) {
    size_t index;

    if (solar_synthesis_statistics_has_field(
            &statistics, SOLAR_SYNTHESIS_FIELD_CELLS)) {
        /* statistics.cells may legitimately be zero. */
    }
    for (index = 0U;
         index < solar_synthesis_statistics_cell_count(&statistics);
         index++) {
        const SolarSynthesisCellUsage *cell =
            solar_synthesis_statistics_cell_at(&statistics, index);
        /* cell is borrowed until statistics is freed. */
    }
}
solar_synthesis_statistics_free(&statistics);
```

`SolarSynthesisResult.statistics` provides the same owned structure directly
after a synthesis operation. `statistics_result` reports optional collection
errors separately from the synthesis result, so a valid netlist remains a
successful build even when a Yosys version emits an unrecognized statistics
format.

The `reported_fields` bitmask and
`solar_synthesis_statistics_has_field()` distinguish a real zero from a value
that Yosys did not report. `completeness` is `NONE`, `PARTIAL`, or `COMPLETE`.
Cells are ordered by descending count and alphabetically for equal counts.

`solar_synthesis_statistics_report_path()` returns a borrowed path to the
untouched text artifact. The caller does not free individual strings, cell
records, or values returned by accessor functions.

This API represents **Generic Synthesis Statistics** only. It does not map
generic cells to any FPGA device resources and does not expose placement,
routing, utilization percentage, or bitstream information.

## Stored build reports

`solar/report_history.h` exposes the persisted report model independently of
the CLI. Initialize and free every owned output with its matching function:

```c
SolarProject project;
SolarStoredBuildReport current;
SolarStoredBuildReport baseline;
SolarBuildReportComparison comparison;

solar_project_init(&project);
solar_stored_build_report_init(&current);
solar_stored_build_report_init(&baseline);
solar_build_report_comparison_init(&comparison);

/* Load the project first, then: */
solar_report_history_load_latest(&project, &current);
solar_report_history_find_previous_comparable(&project, &current, &baseline);
solar_build_reports_compare(&baseline, &current, &comparison);

solar_build_report_comparison_free(&comparison);
solar_stored_build_report_free(&baseline);
solar_stored_build_report_free(&current);
solar_project_free(&project);
```

`SolarStoredMetric.present` distinguishes an unavailable metric from a real
zero. `solar_metric_compare()` computes unsigned magnitudes without converting
near-`UINT64_MAX` values to a signed difference. GSS, timing, and aggregate
comparison functions allocate owned cell tables, metadata, units, and warning
strings in their output structures.

History discovery, sidecar parsing, context validation, mathematical
comparison, and terminal formatting are separate layers. None of the public
history read/compare functions executes an external process.
