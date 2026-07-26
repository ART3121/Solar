#include "commands.h"

#include "solar/build.h"
#include "solar/report_history.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *current_id;
    const char *baseline_id;
    bool summary;
} CompareOptions;

static SolarResult argument_error(const char *message, const char *hint)
{
    return solar_result_error(SOLAR_STATUS_INVALID_ARGUMENT, message, hint);
}

static SolarResult load_history_project(
    const char *start_path,
    SolarProject *project
)
{
    solar_project_init(project);
    return solar_project_find_root(start_path, &project->root);
}

static SolarResult show_latest(const char *start_path)
{
    char *text = NULL;
    SolarResult result = solar_build_report_read(start_path, &text);

    if (result.status == SOLAR_STATUS_OK) (void)printf("%s", text);
    free(text);
    return result;
}

static const char *timings_availability_name(
    SolarTimingsAvailability availability
)
{
    if (availability == SOLAR_TIMINGS_AVAILABLE) return "available";
    if (availability == SOLAR_TIMINGS_PARTIAL) return "partial";
    return "unavailable";
}

static SolarResult list_reports(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    SolarProject project;
    SolarBuildReportList list;
    SolarResult result;
    uint64_t limit = UINT64_MAX;
    size_t displayed;

    if (argument_count != 0) {
        char *end = NULL;
        unsigned long long parsed;

        if (argument_count != 2 || strcmp(arguments[0], "--limit") != 0) {
            return argument_error("invalid solar report list arguments",
                "use solar report list [--limit <count>]");
        }
        errno = 0;
        parsed = strtoull(arguments[1], &end, 10);
        if (arguments[1][0] == '\0' || arguments[1][0] == '-' ||
            errno != 0 || end == arguments[1] || *end != '\0' || parsed == 0U ||
            parsed > (unsigned long long)SIZE_MAX) return argument_error(
                "invalid report list limit",
                "use a positive integer with --limit");
        limit = (uint64_t)parsed;
    }
    solar_build_report_list_init(&list);
    result = load_history_project(start_path, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_report_history_list(
        &project, &list);
    if (result.status == SOLAR_STATUS_OK) {
        (void)printf(
            "BUILD ID       STATUS    TOP              GSS          SIM TIMINGS    TIMESTAMP\n");
        displayed = list.count;
        if ((uint64_t)displayed > limit) displayed = (size_t)limit;
        for (size_t index = 0U; index < displayed; index++) {
            const SolarBuildReportInfo *item = &list.items[index];

            (void)printf("%-14s %-9s %-16s %-12s %-14s %s\n",
                item->build_id,
                item->status == NULL ? "unknown" : item->status,
                item->top == NULL ? "-" : item->top,
                item->gss_available ? "available" : "unavailable",
                timings_availability_name(item->timings_availability),
                item->timestamp == NULL ? "-" : item->timestamp);
        }
    }
    solar_build_report_list_free(&list);
    solar_project_free(&project);
    return result;
}

static SolarResult show_report(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    SolarProject project;
    char *text = NULL;
    SolarResult result;

    if (argument_count != 1) return argument_error(
        "solar report show requires exactly one build ID",
        "use solar report show <build-id>");
    result = load_history_project(start_path, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_report_history_read_text(
        &project, arguments[0], &text);
    if (result.status == SOLAR_STATUS_OK) (void)printf("%s", text);
    free(text);
    solar_project_free(&project);
    return result;
}

static SolarResult parse_compare_options(
    int argument_count,
    char *const arguments[],
    CompareOptions *options
)
{
    int index;

    (void)memset(options, 0, sizeof(*options));
    for (index = 0; index < argument_count; index++) {
        if (strcmp(arguments[index], "--against") == 0) {
            if (options->baseline_id != NULL || index + 1 >= argument_count ||
                arguments[index + 1][0] == '-' ||
                arguments[index + 1][0] == '\0') return argument_error(
                    "invalid or duplicate --against option",
                    "use --against <build-id> exactly once");
            options->baseline_id = arguments[++index];
        } else if (strcmp(arguments[index], "--summary") == 0) {
            if (options->summary) return argument_error(
                "--summary may be specified only once",
                "remove the duplicate --summary option");
            options->summary = true;
        } else if (arguments[index][0] == '-') {
            return argument_error("unknown solar report compare option",
                "run solar report --help for usage");
        } else if (options->current_id != NULL) {
            return argument_error("only one current build ID may be provided",
                "use solar report compare <build-id> --against <build-id>");
        } else {
            options->current_id = arguments[index];
        }
    }
    return solar_result_ok();
}

static void print_metadata(const char *title, const SolarBuildMetadata *metadata)
{
    (void)printf("%s\n", title);
    (void)printf("  ID:         %s\n", metadata->build_id);
    (void)printf("  Timestamp:  %s\n",
        metadata->timestamp == NULL ? "not reported" : metadata->timestamp);
    (void)printf("  Top:        %s\n",
        metadata->top == NULL ? "not reported" : metadata->top);
    (void)printf("  Simulator:  %s%s%s\n",
        metadata->simulation_tool_name == NULL ? "not reported" :
            metadata->simulation_tool_name,
        metadata->simulation_tool_version == NULL ? "" : " ",
        metadata->simulation_tool_version == NULL ? "" :
            metadata->simulation_tool_version);
    (void)printf("  Synthesis:  %s%s%s\n",
        metadata->synthesis_tool_name == NULL ? "not reported" :
            metadata->synthesis_tool_name,
        metadata->synthesis_tool_version == NULL ? "" : " ",
        metadata->synthesis_tool_version == NULL ? "" :
            metadata->synthesis_tool_version);
}

static void format_integer_value(
    bool available,
    uint64_t value,
    char *text,
    size_t capacity
)
{
    if (available) (void)snprintf(text, capacity, "%" PRIu64, value);
    else (void)snprintf(text, capacity, "%s", "not reported");
}

static void format_change(
    const SolarMetricComparison *comparison,
    char *text,
    size_t capacity
)
{
    if (!comparison->absolute_change_available) {
        (void)snprintf(text, capacity, "%s", "-");
    } else if (comparison->absolute_change_magnitude == 0U) {
        (void)snprintf(text, capacity, "%s", "0");
    } else {
        (void)snprintf(text, capacity, "%c%" PRIu64,
            comparison->absolute_change_negative ? '-' : '+',
            comparison->absolute_change_magnitude);
    }
}

static void format_percent(
    const SolarMetricComparison *comparison,
    char *text,
    size_t capacity
)
{
    if (!comparison->baseline_available || !comparison->current_available) {
        (void)snprintf(text, capacity, "%s", "-");
    } else if (!comparison->percentage_available) {
        (void)snprintf(text, capacity, "%s", "new");
    } else {
        (void)snprintf(text, capacity, "%+.1f%%", comparison->percentage_change);
        if (comparison->percentage_change == 0.0) {
            (void)snprintf(text, capacity, "0.0%%");
        }
    }
}

static void print_integer_metric(
    const char *label,
    const SolarMetricComparison *comparison
)
{
    char baseline[32];
    char current[32];
    char change[32];
    char percent[32];

    format_integer_value(comparison->baseline_available,
        comparison->baseline, baseline, sizeof(baseline));
    format_integer_value(comparison->current_available,
        comparison->current, current, sizeof(current));
    format_change(comparison, change, sizeof(change));
    format_percent(comparison, percent, sizeof(percent));
    if (strlen(label) > 22U) {
        (void)printf("%s\n", label);
        label = "";
    }
    (void)printf("%-22s %14s %14s %12s %12s\n",
        label, baseline, current, change, percent);
}

static void print_gss(const SolarBuildReportComparison *comparison)
{
    const SolarGenericSynthesisComparison *gss = &comparison->synthesis;
    size_t index;

    (void)printf("\nGENERIC SYNTHESIS STATISTICS\n\n");
    if (!comparison->synthesis_comparison_available) {
        (void)printf("Comparison unavailable: one or both builds do not contain "
            "compatible GSS data.\n");
        return;
    }
    (void)printf("%-22s %14s %14s %12s %12s\n",
        "Metric", "Baseline", "Current", "Change", "Percent");
    print_integer_metric("Modules", &gss->modules);
    print_integer_metric("Wires", &gss->wires);
    print_integer_metric("Wire bits", &gss->wire_bits);
    print_integer_metric("Public wires", &gss->public_wires);
    print_integer_metric("Public wire bits", &gss->public_wire_bits);
    print_integer_metric("Memories", &gss->memories);
    print_integer_metric("Memory bits", &gss->memory_bits);
    print_integer_metric("Processes", &gss->processes);
    print_integer_metric("Cells", &gss->cells);
    (void)printf("\nCELL USAGE\n\n");
    (void)printf("%-22s %14s %14s %12s %12s\n",
        "Cell type", "Baseline", "Current", "Change", "Percent");
    for (index = 0U; index < gss->cell_type_count; index++) {
        print_integer_metric(gss->cell_types[index].cell_type,
            &gss->cell_types[index].usage);
    }
}

typedef struct {
    const char *suffix;
    long double divisor;
} TimeUnit;

static TimeUnit time_unit_for(const SolarMetricComparison *comparison)
{
    uint64_t maximum = comparison->baseline > comparison->current
        ? comparison->baseline : comparison->current;

    if (maximum >= UINT64_C(1000000000)) return (TimeUnit){"s", 1000000000.0L};
    if (maximum >= UINT64_C(1000000)) return (TimeUnit){"ms", 1000000.0L};
    if (maximum >= UINT64_C(1000)) return (TimeUnit){"us", 1000.0L};
    return (TimeUnit){"ns", 1.0L};
}

static void format_time_value(
    bool available,
    uint64_t value,
    TimeUnit unit,
    char *text,
    size_t capacity
)
{
    if (!available) (void)snprintf(text, capacity, "%s", "not reported");
    else if (unit.divisor == 1.0L) (void)snprintf(text, capacity,
        "%" PRIu64 " %s", value, unit.suffix);
    else (void)snprintf(text, capacity, "%.1Lf %s",
        (long double)value / unit.divisor, unit.suffix);
}

static void print_time_metric(
    const char *label,
    const SolarMetricComparison *comparison
)
{
    TimeUnit unit = time_unit_for(comparison);
    char baseline[32];
    char current[32];
    char change[32];
    char percent[32];

    format_time_value(comparison->baseline_available, comparison->baseline,
        unit, baseline, sizeof(baseline));
    format_time_value(comparison->current_available, comparison->current,
        unit, current, sizeof(current));
    if (!comparison->absolute_change_available) {
        (void)snprintf(change, sizeof(change), "%s", "-");
    } else {
        char magnitude[24];

        format_time_value(true, comparison->absolute_change_magnitude,
            unit, magnitude, sizeof(magnitude));
        (void)snprintf(change, sizeof(change), "%s%s",
            comparison->absolute_change_magnitude == 0U ? "" :
                (comparison->absolute_change_negative ? "-" : "+"), magnitude);
    }
    format_percent(comparison, percent, sizeof(percent));
    (void)printf("%-18s %16s %16s %14s %12s\n",
        label, baseline, current, change, percent);
}

static void print_timings(const SolarBuildReportComparison *comparison)
{
    const SolarSimulationTimingComparison *timings = &comparison->simulation;

    (void)printf("\nSIMULATION TIMINGS\n\n");
    if (!comparison->simulation_comparison_available) {
        (void)printf("Comparison unavailable: one or both builds do not contain "
            "compatible simulation timing data.\n");
        return;
    }
    (void)printf("%-18s %16s %16s %14s %12s\n",
        "Metric", "Baseline", "Current", "Change", "Percent");
    print_time_metric("Compilation", &timings->simulation_compile);
    print_time_metric("Elaboration", &timings->simulation_elaboration);
    print_time_metric("Execution", &timings->simulation_execution);
    print_time_metric("Total", &timings->simulation_total);
    print_time_metric("Simulated HDL time", &timings->simulated_duration);
}

static void print_warnings(const SolarBuildReportComparison *comparison)
{
    size_t index;

    if (comparison->warning_count == 0U) return;
    (void)printf("\nCONTEXT WARNINGS\n\n");
    for (index = 0U; index < comparison->warning_count; index++) {
        (void)printf("  Warning: %s.\n", comparison->warnings[index]);
    }
    (void)printf("  Timing and percentage changes may not represent a direct "
        "regression when contexts differ.\n");
}

static void print_summary(const SolarBuildReportComparison *comparison)
{
    char baseline[32];
    char current[32];
    char change[32];
    char percent[32];

    (void)printf("\nCOMPARISON SUMMARY\n\n");
    (void)printf("Synthesis\n");
    if (comparison->synthesis_comparison_available) {
        const SolarMetricComparison *cells = &comparison->synthesis.cells;

        format_integer_value(cells->baseline_available, cells->baseline,
            baseline, sizeof(baseline));
        format_integer_value(cells->current_available, cells->current,
            current, sizeof(current));
        format_change(cells, change, sizeof(change));
        format_percent(cells, percent, sizeof(percent));
        (void)printf("  Cells:              %s -> %s\n", baseline, current);
        (void)printf("  Change:             %s (%s)\n", change, percent);
        (void)printf("  Cell types added:   %zu\n",
            comparison->synthesis.added_cell_types);
        (void)printf("  Cell types removed: %zu\n",
            comparison->synthesis.removed_cell_types);
    } else {
        (void)printf("  Comparison unavailable\n");
    }
    (void)printf("\nSimulation\n");
    if (comparison->simulation_comparison_available) {
        const SolarMetricComparison *execution =
            &comparison->simulation.simulation_execution;
        const SolarMetricComparison *total =
            &comparison->simulation.simulation_total;
        TimeUnit unit = time_unit_for(execution);

        format_time_value(execution->baseline_available, execution->baseline,
            unit, baseline, sizeof(baseline));
        format_time_value(execution->current_available, execution->current,
            unit, current, sizeof(current));
        format_percent(execution, percent, sizeof(percent));
        if (execution->absolute_change_available) {
            char magnitude[24];
            format_time_value(true, execution->absolute_change_magnitude,
                unit, magnitude, sizeof(magnitude));
            (void)snprintf(change, sizeof(change), "%s%s",
                execution->absolute_change_magnitude == 0U ? "" :
                    (execution->absolute_change_negative ? "-" : "+"),
                magnitude);
        } else (void)snprintf(change, sizeof(change), "-");
        (void)printf("  Execution:          %s -> %s\n", baseline, current);
        (void)printf("  Change:             %s (%s)\n", change, percent);

        unit = time_unit_for(total);
        format_time_value(total->baseline_available, total->baseline,
            unit, baseline, sizeof(baseline));
        format_time_value(total->current_available, total->current,
            unit, current, sizeof(current));
        format_percent(total, percent, sizeof(percent));
        if (total->absolute_change_available) {
            char magnitude[24];
            format_time_value(true, total->absolute_change_magnitude,
                unit, magnitude, sizeof(magnitude));
            (void)snprintf(change, sizeof(change), "%s%s",
                total->absolute_change_magnitude == 0U ? "" :
                    (total->absolute_change_negative ? "-" : "+"),
                magnitude);
        } else (void)snprintf(change, sizeof(change), "-");
        (void)printf("\n  Total:              %s -> %s\n", baseline, current);
        (void)printf("  Change:             %s (%s)\n", change, percent);
    } else {
        (void)printf("  Comparison unavailable\n");
    }
}

static void print_comparison(
    const SolarBuildReportComparison *comparison,
    bool summary_only
)
{
    (void)printf("BUILD REPORT COMPARISON\n\n");
    if (summary_only) {
        (void)printf("Current:   %s\nBaseline:  %s\nTop:       %s\n",
            comparison->current_metadata.build_id,
            comparison->baseline_metadata.build_id,
            comparison->current_metadata.top == NULL ? "not reported" :
                comparison->current_metadata.top);
        print_summary(comparison);
        print_warnings(comparison);
        return;
    }
    print_metadata("Current build", &comparison->current_metadata);
    (void)printf("\n");
    print_metadata("Baseline build", &comparison->baseline_metadata);
    print_gss(comparison);
    print_timings(comparison);
    print_summary(comparison);
    print_warnings(comparison);
}

static SolarResult compare_reports(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    CompareOptions options;
    SolarProject project;
    SolarStoredBuildReport current;
    SolarStoredBuildReport baseline;
    SolarBuildReportComparison comparison;
    SolarResult result = parse_compare_options(
        argument_count, arguments, &options);

    if (result.status != SOLAR_STATUS_OK) return result;
    solar_project_init(&project);
    solar_stored_build_report_init(&current);
    solar_stored_build_report_init(&baseline);
    solar_build_report_comparison_init(&comparison);
    result = load_history_project(start_path, &project);
    if (result.status == SOLAR_STATUS_OK) {
        result = options.current_id == NULL
            ? solar_report_history_load_latest(&project, &current)
            : solar_report_history_load(&project, options.current_id, &current);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = options.baseline_id == NULL
            ? solar_report_history_find_previous_comparable(
                &project, &current, &baseline)
            : solar_report_history_load(&project, options.baseline_id, &baseline);
    }
    if (result.status == SOLAR_STATUS_OK &&
        options.baseline_id != NULL &&
        !baseline.has_synthesis_statistics && !baseline.has_timings) {
        result = solar_result_error(SOLAR_STATUS_CONFIG_ERROR,
            "the selected baseline does not contain comparable report data",
            "choose a build with GSS or simulation timings");
    }
    if (result.status == SOLAR_STATUS_OK &&
        !current.has_synthesis_statistics && !current.has_timings) {
        result = solar_result_error(SOLAR_STATUS_CONFIG_ERROR,
            "the current build does not contain comparable synthesis or "
            "simulation data",
            "choose a build with GSS or simulation timings");
    }
    if (result.status == SOLAR_STATUS_OK) result = solar_build_reports_compare(
        &baseline, &current, &comparison);
    if (result.status == SOLAR_STATUS_OK) print_comparison(
        &comparison, options.summary);
    solar_build_report_comparison_free(&comparison);
    solar_stored_build_report_free(&baseline);
    solar_stored_build_report_free(&current);
    solar_project_free(&project);
    return result;
}

SolarResult solar_cli_command_report(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    if (argument_count == 0) return show_latest(start_path);
    if (strcmp(arguments[0], "list") == 0) return list_reports(
        start_path, argument_count - 1, arguments + 1);
    if (strcmp(arguments[0], "show") == 0) return show_report(
        start_path, argument_count - 1, arguments + 1);
    if (strcmp(arguments[0], "compare") == 0) return compare_reports(
        start_path, argument_count - 1, arguments + 1);
    return argument_error("unknown solar report command",
        "use show, list, compare, or no subcommand");
}
