#include "solar/report_history.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static SolarResult comparison_error(const char *message, const char *hint)
{
    return solar_result_error(SOLAR_STATUS_CONFIG_ERROR, message, hint);
}

void solar_gss_comparison_init(SolarGenericSynthesisComparison *comparison)
{
    if (comparison != NULL) (void)memset(comparison, 0, sizeof(*comparison));
}

void solar_gss_comparison_free(SolarGenericSynthesisComparison *comparison)
{
    size_t index;

    if (comparison == NULL) return;
    for (index = 0U; index < comparison->cell_type_count; index++) {
        free(comparison->cell_types[index].cell_type);
    }
    free(comparison->cell_types);
    solar_gss_comparison_init(comparison);
}

void solar_simulation_timing_comparison_init(
    SolarSimulationTimingComparison *comparison
)
{
    if (comparison != NULL) (void)memset(comparison, 0, sizeof(*comparison));
}

void solar_simulation_timing_comparison_free(
    SolarSimulationTimingComparison *comparison
)
{
    if (comparison == NULL) return;
    free(comparison->simulated_duration_unit);
    solar_simulation_timing_comparison_init(comparison);
}

static SolarResult copy_text(char **destination, const char *source)
{
    if (source == NULL) return solar_result_ok();
    *destination = strdup(source);
    if (*destination == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate report comparison data",
        "free memory and try again"
    );
    return solar_result_ok();
}

static bool text_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) return left == right;
    return strcmp(left, right) == 0;
}

static SolarResult metadata_copy(
    SolarBuildMetadata *destination,
    const SolarBuildMetadata *source
)
{
    SolarBuildMetadata copy;
    SolarResult result = solar_result_ok();
    char **targets[] = {
        &copy.build_id, &copy.timestamp, &copy.project_id, &copy.status,
        &copy.solar_version, &copy.top, &copy.profile,
        &copy.synthesis_backend, &copy.synthesis_tool_name,
        &copy.synthesis_tool_version, &copy.synthesis_source_fingerprint,
        &copy.synthesis_defines_fingerprint, &copy.synthesis_options_fingerprint,
        &copy.synthesis_script_fingerprint, &copy.simulation_backend,
        &copy.simulation_tool_name, &copy.simulation_tool_version,
        &copy.simulation_testbench, &copy.simulation_testbench_fingerprint,
        &copy.simulation_source_fingerprint, &copy.simulation_options_fingerprint,
        &copy.simulation_input_fingerprint, &copy.environment_os,
        &copy.environment_arch, &copy.environment_host_fingerprint
    };
    char *const sources[] = {
        source->build_id, source->timestamp, source->project_id, source->status,
        source->solar_version, source->top, source->profile,
        source->synthesis_backend, source->synthesis_tool_name,
        source->synthesis_tool_version, source->synthesis_source_fingerprint,
        source->synthesis_defines_fingerprint, source->synthesis_options_fingerprint,
        source->synthesis_script_fingerprint, source->simulation_backend,
        source->simulation_tool_name, source->simulation_tool_version,
        source->simulation_testbench, source->simulation_testbench_fingerprint,
        source->simulation_source_fingerprint, source->simulation_options_fingerprint,
        source->simulation_input_fingerprint, source->environment_os,
        source->environment_arch, source->environment_host_fingerprint
    };
    size_t index;

    solar_build_metadata_init(&copy);
    copy.environment_cpu_count = source->environment_cpu_count;
    copy.environment_cpu_count_present = source->environment_cpu_count_present;
    copy.waveform_enabled = source->waveform_enabled;
    copy.waveform_enabled_present = source->waveform_enabled_present;
    for (index = 0U; index < sizeof(targets) / sizeof(targets[0]); index++) {
        result = copy_text(targets[index], sources[index]);
        if (result.status != SOLAR_STATUS_OK) break;
    }
    if (result.status == SOLAR_STATUS_OK) {
        solar_build_metadata_free(destination);
        *destination = copy;
    } else {
        solar_build_metadata_free(&copy);
    }
    return result;
}

SolarMetricComparison solar_metric_compare(
    bool baseline_available,
    uint64_t baseline,
    bool current_available,
    uint64_t current
)
{
    SolarMetricComparison comparison;

    (void)memset(&comparison, 0, sizeof(comparison));
    comparison.baseline_available = baseline_available;
    comparison.current_available = current_available;
    comparison.baseline = baseline;
    comparison.current = current;
    if (!baseline_available || !current_available) {
        comparison.status = SOLAR_COMPARISON_NOT_COMPARABLE;
        return comparison;
    }
    comparison.absolute_change_available = true;
    if (current > baseline) {
        comparison.status = SOLAR_COMPARISON_INCREASED;
        comparison.absolute_change_magnitude = current - baseline;
    } else if (current < baseline) {
        comparison.status = SOLAR_COMPARISON_DECREASED;
        comparison.absolute_change_negative = true;
        comparison.absolute_change_magnitude = baseline - current;
    } else {
        comparison.status = SOLAR_COMPARISON_UNCHANGED;
    }
    comparison.percentage_available = true;
    if (baseline == 0U) {
        comparison.percentage_available = current == 0U;
        comparison.percentage_change = 0.0;
    } else {
        long double magnitude = (long double)comparison.absolute_change_magnitude;
        long double percent = magnitude * 100.0L / (long double)baseline;

        comparison.percentage_change = (double)(
            comparison.absolute_change_negative ? -percent : percent
        );
    }
    return comparison;
}

static uint64_t statistic_value(
    const SolarGenericSynthesisStatistics *statistics,
    SolarSynthesisStatisticsField field
)
{
    switch (field) {
    case SOLAR_SYNTHESIS_FIELD_MODULES: return statistics->modules;
    case SOLAR_SYNTHESIS_FIELD_WIRES: return statistics->wires;
    case SOLAR_SYNTHESIS_FIELD_WIRE_BITS: return statistics->wire_bits;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES: return statistics->public_wires;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS: return statistics->public_wire_bits;
    case SOLAR_SYNTHESIS_FIELD_MEMORIES: return statistics->memories;
    case SOLAR_SYNTHESIS_FIELD_MEMORY_BITS: return statistics->memory_bits;
    case SOLAR_SYNTHESIS_FIELD_PROCESSES: return statistics->processes;
    case SOLAR_SYNTHESIS_FIELD_CELLS: return statistics->cells;
    }
    return 0U;
}

static void count_comparison(
    const SolarMetricComparison *metric,
    SolarGenericSynthesisComparison *result
)
{
    if (metric->status == SOLAR_COMPARISON_INCREASED) result->increased_metrics++;
    else if (metric->status == SOLAR_COMPARISON_DECREASED) result->decreased_metrics++;
    else if (metric->status == SOLAR_COMPARISON_UNCHANGED) result->unchanged_metrics++;
    else result->not_comparable_metrics++;
}

static const SolarSynthesisCellUsage *find_cell(
    const SolarGenericSynthesisStatistics *statistics,
    const char *type
)
{
    size_t index;

    for (index = 0U; index < statistics->cell_type_count; index++) {
        if (strcmp(statistics->cell_types[index].type, type) == 0) {
            return &statistics->cell_types[index];
        }
    }
    return NULL;
}

static int cell_comparison_order(const void *left_pointer, const void *right_pointer)
{
    const SolarCellComparison *left = left_pointer;
    const SolarCellComparison *right = right_pointer;

    if (left->usage.absolute_change_magnitude < right->usage.absolute_change_magnitude) {
        return 1;
    }
    if (left->usage.absolute_change_magnitude > right->usage.absolute_change_magnitude) {
        return -1;
    }
    return strcmp(left->cell_type, right->cell_type);
}

static SolarResult append_cell_comparison(
    SolarGenericSynthesisComparison *result,
    const char *type,
    bool baseline_present,
    uint64_t baseline,
    bool current_present,
    uint64_t current
)
{
    SolarCellComparison *items;
    SolarCellComparison *item;

    items = realloc(
        result->cell_types, (result->cell_type_count + 1U) * sizeof(*items)
    );
    if (items == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate cell comparison data",
        "free memory and try again"
    );
    result->cell_types = items;
    item = &items[result->cell_type_count];
    (void)memset(item, 0, sizeof(*item));
    item->cell_type = strdup(type);
    if (item->cell_type == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not copy a compared cell type",
        "free memory and try again"
    );
    item->usage = solar_metric_compare(true, baseline, true, current);
    if (!baseline_present && current_present) {
        item->usage.status = SOLAR_COMPARISON_ADDED;
        result->added_cell_types++;
    } else if (baseline_present && !current_present) {
        item->usage.status = SOLAR_COMPARISON_REMOVED;
        result->removed_cell_types++;
    }
    result->cell_type_count++;
    return solar_result_ok();
}

SolarResult solar_gss_compare(
    const SolarGenericSynthesisStatistics *baseline,
    const SolarGenericSynthesisStatistics *current,
    SolarGenericSynthesisComparison *result
)
{
    static const SolarSynthesisStatisticsField fields[] = {
        SOLAR_SYNTHESIS_FIELD_MODULES, SOLAR_SYNTHESIS_FIELD_WIRES,
        SOLAR_SYNTHESIS_FIELD_WIRE_BITS, SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES,
        SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS, SOLAR_SYNTHESIS_FIELD_MEMORIES,
        SOLAR_SYNTHESIS_FIELD_MEMORY_BITS, SOLAR_SYNTHESIS_FIELD_PROCESSES,
        SOLAR_SYNTHESIS_FIELD_CELLS
    };
    SolarMetricComparison *outputs[] = {
        &result->modules, &result->wires, &result->wire_bits,
        &result->public_wires, &result->public_wire_bits, &result->memories,
        &result->memory_bits, &result->processes, &result->cells
    };
    SolarResult operation;
    size_t index;

    if (baseline == NULL || current == NULL || result == NULL) {
        return solar_result_error(SOLAR_STATUS_INVALID_ARGUMENT,
            "GSS comparison requires two inputs and output storage",
            "load two stored build reports first");
    }
    solar_gss_comparison_free(result);
    for (index = 0U; index < sizeof(fields) / sizeof(fields[0]); index++) {
        *outputs[index] = solar_metric_compare(
            solar_synthesis_statistics_has_field(baseline, fields[index]),
            statistic_value(baseline, fields[index]),
            solar_synthesis_statistics_has_field(current, fields[index]),
            statistic_value(current, fields[index])
        );
        count_comparison(outputs[index], result);
    }
    for (index = 0U; index < baseline->cell_type_count; index++) {
        const SolarSynthesisCellUsage *other = find_cell(
            current, baseline->cell_types[index].type
        );

        operation = append_cell_comparison(
            result, baseline->cell_types[index].type, true,
            baseline->cell_types[index].count, other != NULL,
            other == NULL ? 0U : other->count
        );
        if (operation.status != SOLAR_STATUS_OK) return operation;
    }
    for (index = 0U; index < current->cell_type_count; index++) {
        if (find_cell(baseline, current->cell_types[index].type) == NULL) {
            operation = append_cell_comparison(
                result, current->cell_types[index].type, false, 0U, true,
                current->cell_types[index].count
            );
            if (operation.status != SOLAR_STATUS_OK) return operation;
        }
    }
    if (result->cell_type_count > 1U) qsort(
        result->cell_types, result->cell_type_count,
        sizeof(*result->cell_types), cell_comparison_order
    );
    return solar_result_ok();
}

static bool normalize_duration(
    SolarStoredMetric input,
    const char *unit,
    SolarStoredMetric *output
)
{
    uint64_t multiplier;

    *output = input;
    if (!input.present) return true;
    if (unit == NULL || strcmp(unit, "ns") == 0) multiplier = 1U;
    else if (strcmp(unit, "us") == 0) multiplier = UINT64_C(1000);
    else if (strcmp(unit, "ms") == 0) multiplier = UINT64_C(1000000);
    else if (strcmp(unit, "s") == 0) multiplier = UINT64_C(1000000000);
    else return false;
    if (input.value > UINT64_MAX / multiplier) return false;
    output->value = input.value * multiplier;
    return true;
}

static void count_timing(
    const SolarMetricComparison *metric,
    SolarSimulationTimingComparison *result
)
{
    if (metric->status == SOLAR_COMPARISON_INCREASED) result->increased_timings++;
    else if (metric->status == SOLAR_COMPARISON_DECREASED) result->decreased_timings++;
    else if (metric->status == SOLAR_COMPARISON_UNCHANGED) result->unchanged_timings++;
    else result->not_comparable_timings++;
}

SolarResult solar_simulation_timings_compare(
    const SolarSimulationTimings *baseline,
    const SolarSimulationTimings *current,
    SolarSimulationTimingComparison *result
)
{
    SolarStoredMetric baseline_simulated;
    SolarStoredMetric current_simulated;
    SolarMetricComparison *metrics[5];
    size_t index;

    if (baseline == NULL || current == NULL || result == NULL) {
        return solar_result_error(SOLAR_STATUS_INVALID_ARGUMENT,
            "simulation timing comparison requires two inputs and output storage",
            "load two stored build reports first");
    }
    solar_simulation_timing_comparison_free(result);
    result->context_compatible = true;
    result->simulation_compile = solar_metric_compare(
        baseline->simulation_compile.present, baseline->simulation_compile.value,
        current->simulation_compile.present, current->simulation_compile.value);
    result->simulation_elaboration = solar_metric_compare(
        baseline->simulation_elaboration.present,
        baseline->simulation_elaboration.value,
        current->simulation_elaboration.present,
        current->simulation_elaboration.value);
    result->simulation_execution = solar_metric_compare(
        baseline->simulation_execution.present,
        baseline->simulation_execution.value,
        current->simulation_execution.present,
        current->simulation_execution.value);
    result->simulation_total = solar_metric_compare(
        baseline->simulation_total.present, baseline->simulation_total.value,
        current->simulation_total.present, current->simulation_total.value);
    if (normalize_duration(baseline->simulated_duration,
            baseline->simulated_duration_unit, &baseline_simulated) &&
        normalize_duration(current->simulated_duration,
            current->simulated_duration_unit, &current_simulated)) {
        result->simulated_duration = solar_metric_compare(
            baseline_simulated.present, baseline_simulated.value,
            current_simulated.present, current_simulated.value);
        if (baseline_simulated.present || current_simulated.present) {
            result->simulated_duration_unit = strdup("ns");
            if (result->simulated_duration_unit == NULL) return solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate simulated duration unit",
                "free memory and try again");
        }
    } else {
        result->simulated_duration = solar_metric_compare(false, 0U, false, 0U);
    }
    metrics[0] = &result->simulation_compile;
    metrics[1] = &result->simulation_elaboration;
    metrics[2] = &result->simulation_execution;
    metrics[3] = &result->simulation_total;
    metrics[4] = &result->simulated_duration;
    for (index = 0U; index < 5U; index++) count_timing(metrics[index], result);
    return solar_result_ok();
}

static SolarResult add_warning(
    SolarBuildReportComparison *result,
    const char *message
)
{
    char **items = realloc(
        result->warnings, (result->warning_count + 1U) * sizeof(*items)
    );

    if (items == NULL) return solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate comparison warnings", "free memory and try again");
    result->warnings = items;
    items[result->warning_count] = strdup(message);
    if (items[result->warning_count] == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR, "could not copy a comparison warning",
        "free memory and try again");
    result->warning_count++;
    return solar_result_ok();
}

static SolarResult warn_difference(
    SolarBuildReportComparison *result,
    const char *baseline,
    const char *current,
    const char *message
)
{
    return text_equal(baseline, current)
        ? solar_result_ok() : add_warning(result, message);
}

static bool environment_differs(
    const SolarBuildMetadata *baseline,
    const SolarBuildMetadata *current
)
{
    return !text_equal(baseline->environment_os, current->environment_os) ||
        !text_equal(baseline->environment_arch, current->environment_arch) ||
        !text_equal(baseline->environment_host_fingerprint,
            current->environment_host_fingerprint) ||
        (baseline->environment_cpu_count_present &&
         current->environment_cpu_count_present &&
         baseline->environment_cpu_count != current->environment_cpu_count);
}

SolarResult solar_build_reports_compare(
    const SolarStoredBuildReport *baseline,
    const SolarStoredBuildReport *current,
    SolarBuildReportComparison *result
)
{
    SolarResult operation;

    if (baseline == NULL || current == NULL || result == NULL) return
        solar_result_error(SOLAR_STATUS_INVALID_ARGUMENT,
            "build report comparison requires two reports and output storage",
            "load a baseline and current build first");
    solar_build_report_comparison_free(result);
    if (!text_equal(baseline->metadata.project_id, current->metadata.project_id)) {
        return comparison_error(
            "the selected builds belong to different projects",
            "choose a baseline from the current project"
        );
    }
    operation = metadata_copy(&result->baseline_metadata, &baseline->metadata);
    if (operation.status == SOLAR_STATUS_OK) {
        operation = metadata_copy(&result->current_metadata, &current->metadata);
    }
    if (operation.status != SOLAR_STATUS_OK) goto fail;

    if (baseline->has_synthesis_statistics && current->has_synthesis_statistics &&
        baseline->synthesis_statistics.available &&
        current->synthesis_statistics.available &&
        text_equal(baseline->metadata.top, current->metadata.top)) {
        operation = solar_gss_compare(&baseline->synthesis_statistics,
            &current->synthesis_statistics, &result->synthesis);
        if (operation.status != SOLAR_STATUS_OK) goto fail;
        result->synthesis_comparison_available = true;
        operation = warn_difference(result,
            baseline->metadata.synthesis_backend,
            current->metadata.synthesis_backend,
            "synthesis backends differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.synthesis_tool_version,
            current->metadata.synthesis_tool_version,
            "synthesis tool versions differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.synthesis_source_fingerprint,
            current->metadata.synthesis_source_fingerprint,
            "synthesis source fingerprints differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.synthesis_defines_fingerprint,
            current->metadata.synthesis_defines_fingerprint,
            "synthesis defines differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.synthesis_options_fingerprint,
            current->metadata.synthesis_options_fingerprint,
            "synthesis options or profiles differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.synthesis_script_fingerprint,
            current->metadata.synthesis_script_fingerprint,
            "Yosys scripts or passes differ");
    } else if (baseline->has_synthesis_statistics &&
               current->has_synthesis_statistics &&
               !text_equal(baseline->metadata.top, current->metadata.top)) {
        operation = add_warning(result,
            "synthesis comparison unavailable because top modules differ");
    }
    if (operation.status != SOLAR_STATUS_OK) goto fail;

    if (baseline->has_timings && current->has_timings &&
        text_equal(baseline->metadata.simulation_backend,
            current->metadata.simulation_backend) &&
        text_equal(baseline->metadata.simulation_testbench,
            current->metadata.simulation_testbench)) {
        operation = solar_simulation_timings_compare(
            &baseline->timings, &current->timings, &result->simulation);
        if (operation.status != SOLAR_STATUS_OK) goto fail;
        result->simulation_comparison_available = true;
        operation = warn_difference(result,
            baseline->metadata.simulation_tool_version,
            current->metadata.simulation_tool_version,
            "simulator versions differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.simulation_source_fingerprint,
            current->metadata.simulation_source_fingerprint,
            "simulation source fingerprints differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.simulation_options_fingerprint,
            current->metadata.simulation_options_fingerprint,
            "simulation options, defines, or profiles differ");
        if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
            baseline->metadata.simulation_input_fingerprint,
            current->metadata.simulation_input_fingerprint,
            "simulation input fingerprints differ");
        if (operation.status == SOLAR_STATUS_OK &&
            baseline->metadata.waveform_enabled_present &&
            current->metadata.waveform_enabled_present &&
            baseline->metadata.waveform_enabled !=
                current->metadata.waveform_enabled) {
            operation = add_warning(result,
                "waveform generation was enabled in only one build");
        }
        if (operation.status == SOLAR_STATUS_OK &&
            baseline->timings.simulated_duration.present &&
            current->timings.simulated_duration.present &&
            result->simulation.simulated_duration.status ==
                SOLAR_COMPARISON_NOT_COMPARABLE) {
            operation = add_warning(result,
                "simulated HDL duration units are incompatible or exceed the "
                "normalization range");
        } else if (operation.status == SOLAR_STATUS_OK &&
            result->simulation.simulated_duration.status !=
                SOLAR_COMPARISON_NOT_COMPARABLE &&
            result->simulation.simulated_duration.status !=
                SOLAR_COMPARISON_UNCHANGED) {
            operation = add_warning(result,
                "simulated HDL durations differ");
        }
        result->simulation.environment_changed = environment_differs(
            &baseline->metadata, &current->metadata);
        if (operation.status == SOLAR_STATUS_OK &&
            result->simulation.environment_changed) {
            operation = add_warning(result,
                "builds were measured in different execution environments");
        }
        if (operation.status == SOLAR_STATUS_OK &&
            ((baseline->timings.simulation_status_present &&
              !baseline->timings.simulation_succeeded) ||
             (current->timings.simulation_status_present &&
              !current->timings.simulation_succeeded))) {
            operation = add_warning(result,
                "one or both simulations did not complete successfully");
        }
    } else if (baseline->has_timings && current->has_timings) {
        operation = add_warning(result,
            !text_equal(baseline->metadata.simulation_backend,
                current->metadata.simulation_backend)
                ? "simulation timing comparison unavailable because backends differ"
                : "simulation timing comparison unavailable because testbenches differ");
    }
    if (operation.status == SOLAR_STATUS_OK) operation = warn_difference(result,
        baseline->metadata.solar_version, current->metadata.solar_version,
        "Solar versions differ");
    if (operation.status != SOLAR_STATUS_OK) goto fail;
    return solar_result_ok();

fail:
    solar_build_report_comparison_free(result);
    return operation;
}
