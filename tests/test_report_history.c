#define _POSIX_C_SOURCE 200809L

#include "solar/build.h"
#include "solar/filesystem.h"
#include "solar/report_history.h"
#include "solar/runner.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static char *join_path(const char *left, const char *right)
{
    char *path = NULL;
    return solar_filesystem_join(left, right, &path).status == SOLAR_STATUS_OK
        ? path : NULL;
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF || fclose(file) != 0;
    return failed ? -1 : 0;
}

static int run_cli(
    const char *solar_path,
    const char *root,
    const char *const arguments[],
    int expected_exit,
    const char *expected_output,
    const char *expected_error
)
{
    SolarProcessSpec specification = {
        solar_path, arguments, root, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    if ((expected_exit == 0 ? result.status != SOLAR_STATUS_OK :
            result.status != SOLAR_STATUS_PROCESS_FAILED) ||
        process.exit_code != expected_exit ||
        (expected_output != NULL && (process.stdout_text == NULL ||
            strstr(process.stdout_text, expected_output) == NULL)) ||
        (expected_error != NULL && (process.stderr_text == NULL ||
            strstr(process.stderr_text, expected_error) == NULL))) {
        failed = fail("report CLI output or exit code is incorrect");
        if (process.stdout_text != NULL) (void)fprintf(stderr,
            "stdout: %s\n", process.stdout_text);
        if (process.stderr_text != NULL) (void)fprintf(stderr,
            "stderr: %s\n", process.stderr_text);
    }
    solar_process_result_free(&process);
    return failed;
}

static int set_cells(
    SolarGenericSynthesisStatistics *statistics,
    const char *first,
    uint64_t first_count,
    const char *second,
    uint64_t second_count
)
{
    size_t count = second == NULL ? 1U : 2U;

    statistics->cell_types = calloc(count, sizeof(*statistics->cell_types));
    if (statistics->cell_types == NULL) return -1;
    statistics->cell_types[0].type = strdup(first);
    statistics->cell_types[0].count = first_count;
    if (statistics->cell_types[0].type == NULL) return -1;
    if (second != NULL) {
        statistics->cell_types[1].type = strdup(second);
        statistics->cell_types[1].count = second_count;
        if (statistics->cell_types[1].type == NULL) return -1;
    }
    statistics->cell_type_count = count;
    statistics->available = true;
    statistics->completeness = SOLAR_SYNTHESIS_STATISTICS_PARTIAL;
    statistics->reported_fields = SOLAR_SYNTHESIS_FIELD_CELLS |
        SOLAR_SYNTHESIS_FIELD_WIRES;
    statistics->cells = first_count + second_count;
    statistics->wires = 4U;
    statistics->tool = strdup("yosys");
    statistics->tool_version = strdup("Yosys test=1/path with spaces");
    statistics->top = strdup("counter");
    return statistics->tool == NULL || statistics->tool_version == NULL ||
        statistics->top == NULL ? -1 : 0;
}

static int test_metric_math(void)
{
    SolarMetricComparison comparison;

    comparison = solar_metric_compare(true, 0U, true, 8U);
    if (comparison.status != SOLAR_COMPARISON_INCREASED ||
        comparison.percentage_available ||
        comparison.absolute_change_magnitude != 8U) {
        return fail("zero baseline comparison is incorrect");
    }
    comparison = solar_metric_compare(true, 8U, true, 0U);
    if (comparison.status != SOLAR_COMPARISON_DECREASED ||
        !comparison.absolute_change_negative ||
        comparison.percentage_change != -100.0) {
        return fail("zero current comparison is incorrect");
    }
    comparison = solar_metric_compare(true, 0U, true, 0U);
    if (comparison.status != SOLAR_COMPARISON_UNCHANGED ||
        !comparison.percentage_available || comparison.percentage_change != 0.0) {
        return fail("two zero values are incorrect");
    }
    comparison = solar_metric_compare(
        true, UINT64_MAX, true, UINT64_MAX - UINT64_C(7));
    if (comparison.absolute_change_magnitude != 7U ||
        !comparison.absolute_change_negative) {
        return fail("UINT64_MAX comparison overflowed");
    }
    comparison = solar_metric_compare(false, 0U, true, 0U);
    return comparison.status == SOLAR_COMPARISON_NOT_COMPARABLE &&
        !comparison.absolute_change_available ? 0 :
        fail("missing metric was treated as zero");
}

static int test_gss_comparison(void)
{
    SolarGenericSynthesisStatistics baseline;
    SolarGenericSynthesisStatistics current;
    SolarGenericSynthesisComparison comparison;
    SolarResult result;
    int failed = 0;

    solar_synthesis_statistics_init(&baseline);
    solar_synthesis_statistics_init(&current);
    solar_gss_comparison_init(&comparison);
    if (set_cells(&baseline, "$dff", 8U, "$not", 4U) != 0 ||
        set_cells(&current, "$dff", 10U, "$mul", 1U) != 0) {
        failed = fail("could not allocate GSS fixtures");
        goto cleanup;
    }
    result = solar_gss_compare(&baseline, &current, &comparison);
    if (result.status != SOLAR_STATUS_OK || comparison.added_cell_types != 1U ||
        comparison.removed_cell_types != 1U || comparison.cell_type_count != 3U ||
        !comparison.cells.absolute_change_negative ||
        comparison.cells.absolute_change_magnitude != 1U) {
        failed = fail("GSS union or totals are incorrect");
        goto cleanup;
    }
    if (strcmp(comparison.cell_types[0].cell_type, "$not") != 0 ||
        comparison.cell_types[0].usage.status != SOLAR_COMPARISON_REMOVED ||
        strcmp(comparison.cell_types[1].cell_type, "$dff") != 0 ||
        strcmp(comparison.cell_types[2].cell_type, "$mul") != 0) {
        failed = fail("cell differences are not sorted by magnitude and name");
    }

cleanup:
    solar_gss_comparison_free(&comparison);
    solar_synthesis_statistics_free(&current);
    solar_synthesis_statistics_free(&baseline);
    return failed;
}

static int test_timing_comparison(void)
{
    SolarSimulationTimings baseline;
    SolarSimulationTimings current;
    SolarSimulationTimingComparison comparison;
    SolarResult result;
    int failed = 0;

    solar_simulation_timings_init(&baseline);
    solar_simulation_timings_init(&current);
    solar_simulation_timing_comparison_init(&comparison);
    baseline.simulation_execution = (SolarStoredMetric){true, UINT64_C(48600000)};
    current.simulation_execution = (SolarStoredMetric){true, UINT64_C(55200000)};
    baseline.simulation_total = (SolarStoredMetric){true, UINT64_C(67400000)};
    current.simulation_total = (SolarStoredMetric){true, UINT64_C(75100000)};
    baseline.simulated_duration = (SolarStoredMetric){true, 1U};
    current.simulated_duration = (SolarStoredMetric){true, 1000U};
    baseline.simulated_duration_unit = strdup("us");
    current.simulated_duration_unit = strdup("ns");
    result = solar_simulation_timings_compare(&baseline, &current, &comparison);
    if (result.status != SOLAR_STATUS_OK ||
        comparison.simulation_execution.absolute_change_magnitude !=
            UINT64_C(6600000) ||
        comparison.simulation_total.status != SOLAR_COMPARISON_INCREASED ||
        comparison.simulated_duration.status != SOLAR_COMPARISON_UNCHANGED ||
        comparison.simulation_compile.status != SOLAR_COMPARISON_NOT_COMPARABLE) {
        failed = fail("simulation timing comparison is incorrect");
    }
    solar_simulation_timing_comparison_free(&comparison);
    free(baseline.simulated_duration_unit);
    baseline.simulated_duration_unit = strdup("ticks");
    result = solar_simulation_timings_compare(&baseline, &current, &comparison);
    if (failed == 0 && (result.status != SOLAR_STATUS_OK ||
        comparison.simulated_duration.status !=
            SOLAR_COMPARISON_NOT_COMPARABLE)) {
        failed = fail("incompatible HDL duration units were compared");
    }
    solar_simulation_timing_comparison_free(&comparison);
    solar_simulation_timings_free(&current);
    solar_simulation_timings_free(&baseline);
    return failed;
}

static int test_context_warnings(void)
{
    SolarStoredBuildReport baseline;
    SolarStoredBuildReport current;
    SolarBuildReportComparison comparison;
    SolarResult result;
    int failed = 0;

    solar_stored_build_report_init(&baseline);
    solar_stored_build_report_init(&current);
    solar_build_report_comparison_init(&comparison);
    baseline.metadata.project_id = strdup("project");
    current.metadata.project_id = strdup("project");
    baseline.metadata.simulation_backend = strdup("iverilog");
    current.metadata.simulation_backend = strdup("iverilog");
    baseline.metadata.simulation_testbench = strdup("basic");
    current.metadata.simulation_testbench = strdup("basic");
    baseline.metadata.simulation_tool_version = strdup("11");
    current.metadata.simulation_tool_version = strdup("12");
    baseline.metadata.simulation_source_fingerprint = strdup("source-a");
    current.metadata.simulation_source_fingerprint = strdup("source-b");
    baseline.metadata.simulation_options_fingerprint = strdup("options-a");
    current.metadata.simulation_options_fingerprint = strdup("options-b");
    baseline.metadata.simulation_input_fingerprint = strdup("input-a");
    current.metadata.simulation_input_fingerprint = strdup("input-b");
    baseline.metadata.environment_host_fingerprint = strdup("host-a");
    current.metadata.environment_host_fingerprint = strdup("host-b");
    baseline.metadata.solar_version = strdup("0.4.4");
    current.metadata.solar_version = strdup("0.4.5");
    baseline.metadata.waveform_enabled_present = true;
    current.metadata.waveform_enabled_present = true;
    current.metadata.waveform_enabled = true;
    baseline.has_timings = true;
    current.has_timings = true;
    baseline.timings.simulation_execution = (SolarStoredMetric){true, 10U};
    current.timings.simulation_execution = (SolarStoredMetric){true, 20U};
    baseline.timings.simulated_duration = (SolarStoredMetric){true, 10U};
    current.timings.simulated_duration = (SolarStoredMetric){true, 20U};
    baseline.timings.simulated_duration_unit = strdup("ns");
    current.timings.simulated_duration_unit = strdup("ns");
    baseline.timings.simulation_status_present = true;
    current.timings.simulation_status_present = true;
    baseline.timings.simulation_succeeded = true;
    if (baseline.metadata.project_id == NULL ||
        current.metadata.project_id == NULL ||
        baseline.timings.simulated_duration_unit == NULL ||
        current.timings.simulated_duration_unit == NULL) {
        failed = fail("could not allocate context warning fixtures");
        goto cleanup;
    }
    result = solar_build_reports_compare(&baseline, &current, &comparison);
    if (result.status != SOLAR_STATUS_OK ||
        !comparison.simulation_comparison_available ||
        !comparison.simulation.environment_changed ||
        comparison.warning_count < 8U) {
        failed = fail("simulation context differences were not reported");
    }

cleanup:
    solar_build_report_comparison_free(&comparison);
    solar_stored_build_report_free(&current);
    solar_stored_build_report_free(&baseline);
    return failed;
}

static int test_incompatible_contexts_are_section_local(void)
{
    SolarStoredBuildReport baseline;
    SolarStoredBuildReport current;
    SolarBuildReportComparison comparison;
    SolarResult result;
    int failed = 0;

    solar_stored_build_report_init(&baseline);
    solar_stored_build_report_init(&current);
    solar_build_report_comparison_init(&comparison);
    baseline.metadata.project_id = strdup("same-project");
    current.metadata.project_id = strdup("same-project");
    baseline.metadata.top = strdup("old_top");
    current.metadata.top = strdup("new_top");
    baseline.metadata.simulation_backend = strdup("iverilog");
    current.metadata.simulation_backend = strdup("verilator");
    baseline.metadata.simulation_testbench = strdup("basic");
    current.metadata.simulation_testbench = strdup("basic");
    baseline.metadata.solar_version = strdup("0.4.5");
    current.metadata.solar_version = strdup("0.4.5");
    baseline.has_synthesis_statistics = true;
    current.has_synthesis_statistics = true;
    baseline.has_timings = true;
    current.has_timings = true;
    baseline.timings.simulation_execution = (SolarStoredMetric){true, 10U};
    current.timings.simulation_execution = (SolarStoredMetric){true, 20U};
    if (baseline.metadata.project_id == NULL ||
        current.metadata.project_id == NULL || baseline.metadata.top == NULL ||
        current.metadata.top == NULL ||
        baseline.metadata.simulation_backend == NULL ||
        current.metadata.simulation_backend == NULL ||
        baseline.metadata.simulation_testbench == NULL ||
        current.metadata.simulation_testbench == NULL ||
        baseline.metadata.solar_version == NULL ||
        current.metadata.solar_version == NULL ||
        set_cells(&baseline.synthesis_statistics, "$dff", 1U, NULL, 0U) != 0 ||
        set_cells(&current.synthesis_statistics, "$dff", 2U, NULL, 0U) != 0) {
        failed = fail("could not allocate incompatible context fixtures");
        goto cleanup;
    }
    result = solar_build_reports_compare(&baseline, &current, &comparison);
    if (result.status != SOLAR_STATUS_OK ||
        comparison.synthesis_comparison_available ||
        comparison.simulation_comparison_available ||
        comparison.warning_count < 2U) {
        failed = fail("incompatible contexts failed the aggregate comparison");
    }

cleanup:
    solar_build_report_comparison_free(&comparison);
    solar_stored_build_report_free(&current);
    solar_stored_build_report_free(&baseline);
    return failed;
}

static int test_failed_invalid_context_is_stored(void)
{
    char template[] = "/tmp/solar-report-failed-context-XXXXXX";
    char *root = mkdtemp(template);
    SolarBuildContext context;
    SolarProject project;
    SolarStoredBuildReport report;
    SolarResult result;
    int failed = 0;

    solar_build_context_init(&context);
    solar_project_init(&project);
    solar_stored_build_report_init(&report);
    if (root == NULL) return fail("mkdtemp failed for invalid build history");
    context.project.root = strdup(root);
    context.project.manifest_path = join_path(root, "solar.toml");
    context.operation = strdup("build full");
    context.started_at = time(NULL);
    context.finished_at = context.started_at;
    context.result = solar_result_error(
        SOLAR_STATUS_CONFIG_ERROR, "invalid project fixture", "fixture");
    if (context.project.root == NULL || context.project.manifest_path == NULL ||
        context.operation == NULL) {
        failed = fail("could not allocate invalid build context");
        goto cleanup;
    }
    result = solar_build_report_write(&context);
    project.root = strdup(root);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_report_history_load_latest(&project, &report);
    }
    if (result.status != SOLAR_STATUS_OK || report.metadata.status == NULL ||
        strcmp(report.metadata.status, "failed") != 0 ||
        report.has_synthesis_statistics || report.has_timings) {
        failed = fail("a failed build with partial context was not stored");
    }

cleanup:
    solar_stored_build_report_free(&report);
    solar_project_free(&project);
    solar_build_context_free(&context);
    return failed;
}

static int test_order_independent_sidecars(void)
{
    static const char metadata_text[] =
        "status=success\nproject_id=fixture-project\n"
        "build_id=build-000001\nschema=1\ntop=counter\n";
    static const char statistics_text[] =
        "cell.0.value=3\nmetric.cells.value=3\ncell.count=1\n"
        "metric.wires.present=0\nmetric.modules.present=0\n"
        "metric.wire_bits.present=0\nmetric.public_wires.present=0\n"
        "metric.public_wire_bits.present=0\nmetric.memories.present=0\n"
        "metric.memory_bits.present=0\nmetric.processes.present=0\n"
        "cell.0.type=$mux\nmetric.cells.present=1\n"
        "kind=generic-synthesis-statistics\nschema=1\n";
    static const char timings_text[] =
        "simulation.simulated_duration.present=0\n"
        "timer.simulation_total.nanoseconds=30\n"
        "timer.simulation_execution.present=1\n"
        "timer.simulation_compile.nanoseconds=10\n"
        "timer.simulation_elaboration.present=0\n"
        "simulation.status=success\n"
        "timer.simulation_total.present=1\n"
        "timer.simulation_compile.present=1\n"
        "timer.simulation_execution.nanoseconds=20\n"
        "kind=build-timings\nschema=1\n";
    char template[] = "/tmp/solar-report-sidecars-XXXXXX";
    char *root = mkdtemp(template);
    char *directory = NULL;
    char *report_path = NULL;
    char *metadata_path = NULL;
    char *statistics_path = NULL;
    char *timings_path = NULL;
    SolarProject project;
    SolarStoredBuildReport report;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    solar_stored_build_report_init(&report);
    if (root == NULL) return fail("mkdtemp failed for sidecar fixtures");
    result = solar_filesystem_prepare_generated_directory(
        root, ".solar/reports");
    if (result.status == SOLAR_STATUS_OK) {
        directory = join_path(root, ".solar/reports/build-000001");
    }
    if (directory == NULL || mkdir(directory, 0700) != 0) {
        failed = fail("could not create sidecar fixture directory");
        goto cleanup;
    }
    report_path = join_path(directory, "report.txt");
    metadata_path = join_path(directory, "metadata.dat");
    statistics_path = join_path(directory, "synthesis-stats.dat");
    timings_path = join_path(directory, "timings.dat");
    if (report_path == NULL || metadata_path == NULL ||
        statistics_path == NULL || timings_path == NULL ||
        write_text(report_path, "fixture report\n") != 0 ||
        write_text(metadata_path, metadata_text) != 0 ||
        write_text(statistics_path, statistics_text) != 0 ||
        write_text(timings_path, timings_text) != 0) {
        failed = fail("could not write order-independent sidecars");
        goto cleanup;
    }
    project.root = strdup(root);
    result = solar_report_history_load(&project, "build-000001", &report);
    if (result.status != SOLAR_STATUS_OK ||
        !report.has_synthesis_statistics || !report.has_timings ||
        report.synthesis_statistics.cells != 3U ||
        report.synthesis_statistics.cell_type_count != 1U ||
        report.timings.simulation_compile.value != 10U ||
        report.timings.simulation_execution.value != 20U ||
        report.timings.simulation_total.value != 30U) {
        failed = fail("sidecar key order affected parsing");
        goto cleanup;
    }
    if (write_text(statistics_path,
            "schema=1\nkind=generic-synthesis-statistics\n"
            "metric.modules.present=1\nmetric.modules.value=invalid\n") != 0) {
        failed = fail("could not create malformed sidecar fixture");
        goto cleanup;
    }
    result = solar_report_history_load(&project, "build-000001", &report);
    if (result.status == SOLAR_STATUS_OK) {
        failed = fail("malformed numeric sidecar value was accepted");
    }

cleanup:
    solar_stored_build_report_free(&report);
    solar_project_free(&project);
    free(timings_path);
    free(statistics_path);
    free(metadata_path);
    free(report_path);
    free(directory);
    return failed;
}

static int prepare_context(
    SolarBuildContext *context,
    const char *root,
    uint64_t cells,
    uint64_t execution,
    bool success
)
{
    solar_build_context_init(context);
    context->project.root = strdup(root);
    context->project.manifest_path = join_path(root, "solar.toml");
    context->project.config.project.name = strdup("history-test");
    context->project.config.project.language = strdup("verilog");
    context->project.config.synthesis.backend = strdup("yosys");
    context->project.config.synthesis.top = strdup("counter");
    context->operation = strdup("build full");
    context->profile_name = strdup("debug");
    context->started_at = time(NULL);
    context->finished_at = context->started_at;
    context->duration_ns = UINT64_C(90000000);
    context->result = success ? solar_result_ok() : solar_result_error(
        SOLAR_STATUS_PROCESS_FAILED, "intentional test failure", "fixture");
    context->has_synthesis_result = true;
    context->synthesis_result.result = solar_result_ok();
    if (set_cells(&context->synthesis_result.statistics, "$dff", cells,
            NULL, 0U) != 0) return -1;
    context->has_test_result = true;
    context->test_result.name = strdup("basic");
    context->test_result.compile_duration_ns = UINT64_C(18000000);
    context->test_result.simulation_duration_ns = execution;
    context->test_result.result = success ? solar_result_ok() :
        solar_result_error(SOLAR_STATUS_PROCESS_FAILED,
            "intentional simulation failure", "fixture");
    context->step_count = 1U;
    (void)snprintf(context->steps[0].name, sizeof(context->steps[0].name), "sim");
    context->steps[0].duration_ns = execution + UINT64_C(20000000);
    context->steps[0].result = context->test_result.result;
    return context->project.root == NULL || context->project.manifest_path == NULL ||
        context->project.config.project.name == NULL ||
        context->project.config.project.language == NULL ||
        context->project.config.synthesis.backend == NULL ||
        context->project.config.synthesis.top == NULL ||
        context->operation == NULL || context->profile_name == NULL ||
        context->test_result.name == NULL ? -1 : 0;
}

static int test_history(const char *solar_path)
{
    char template[] = "/tmp/solar-report-history-XXXXXX";
    char *root = mkdtemp(template);
    char *manifest = NULL;
    char *legacy_directory = NULL;
    char *legacy_report = NULL;
    char *corrupt_directory = NULL;
    char *corrupt_report = NULL;
    char *corrupt_metadata = NULL;
    SolarBuildContext context;
    SolarProject project;
    SolarStoredBuildReport latest;
    SolarStoredBuildReport baseline;
    SolarStoredBuildReport corrupt;
    SolarBuildReportList list;
    SolarResult result;
    char *text = NULL;
    int failed = 0;

    solar_build_context_init(&context);
    solar_project_init(&project);
    solar_stored_build_report_init(&latest);
    solar_stored_build_report_init(&baseline);
    solar_stored_build_report_init(&corrupt);
    solar_build_report_list_init(&list);
    if (root == NULL) return fail("mkdtemp failed");
    manifest = join_path(root, "solar.toml");
    if (manifest == NULL || write_text(manifest, "[project]\nname=\"x\"\n") != 0 ||
        prepare_context(&context, root, 8U, UINT64_C(48000000), true) != 0) {
        failed = fail("could not prepare first history record");
        goto cleanup;
    }
    result = solar_build_report_write(&context);
    solar_build_context_free(&context);
    if (result.status != SOLAR_STATUS_OK ||
        prepare_context(&context, root, 10U, UINT64_C(55000000), false) != 0) {
        failed = fail("could not write or prepare history records");
        goto cleanup;
    }
    result = solar_build_report_write(&context);
    solar_build_context_free(&context);
    project.root = strdup(root);
    result = result.status == SOLAR_STATUS_OK ? solar_report_history_load_latest(
        &project, &latest) : result;
    if (result.status != SOLAR_STATUS_OK ||
        strcmp(latest.metadata.build_id, "build-000002") != 0 ||
        strcmp(latest.metadata.status, "failed") != 0 ||
        latest.metadata.synthesis_tool_version == NULL ||
        strcmp(latest.metadata.synthesis_tool_version,
            "Yosys test=1/path with spaces") != 0 ||
        !latest.has_synthesis_statistics || !latest.has_timings ||
        latest.timings.simulation_execution.value != UINT64_C(55000000)) {
        failed = fail("latest failed build or nanosecond sidecars are incorrect");
        goto cleanup;
    }
    result = solar_report_history_list(&project, &list);
    if (result.status != SOLAR_STATUS_OK || list.count != 2U ||
        list.items[0].timings_availability != SOLAR_TIMINGS_PARTIAL) {
        failed = fail("failed simulation timings were not marked partial");
        goto cleanup;
    }
    solar_build_report_list_free(&list);
    result = solar_report_history_find_previous_comparable(
        &project, &latest, &baseline);
    if (result.status != SOLAR_STATUS_OK ||
        strcmp(baseline.metadata.build_id, "build-000001") != 0) {
        failed = fail("automatic baseline selection is incorrect");
        goto cleanup;
    }
    result = solar_report_history_read_text(
        &project, "build-000001", &text);
    if (result.status != SOLAR_STATUS_OK ||
        strstr(text, "SOLAR LAST BUILD REPORT") == NULL) {
        failed = fail("stored human report was not preserved");
        goto cleanup;
    }
    free(text); text = NULL;
    {
        const char *plain[] = {solar_path, "report", NULL};
        const char *list_arguments[] = {
            solar_path, "report", "list", "--limit", "1", NULL};
        const char *show_arguments[] = {
            solar_path, "report", "show", "build-000001", NULL};
        const char *compare_arguments[] = {
            solar_path, "report", "compare", "--summary", NULL};
        const char *explicit_arguments[] = {
            solar_path, "report", "compare", "build-000002",
            "--against", "build-000001", NULL};
        const char *invalid_arguments[] = {
            solar_path, "report", "compare", "--summary", "--summary", NULL};
        const char *missing_against[] = {
            solar_path, "report", "compare", "--against", NULL};
        const char *unknown_against[] = {
            solar_path, "report", "compare", "--against", "build-999999", NULL};
        const char *two_current[] = {
            solar_path, "report", "compare", "build-000002",
            "build-000001", NULL};
        const char *zero_limit[] = {
            solar_path, "report", "list", "--limit", "0", NULL};
        const char *invalid_limit[] = {
            solar_path, "report", "list", "--limit", "invalid", NULL};
        const char *missing_show[] = {solar_path, "report", "show", NULL};
        const char *extra_show[] = {
            solar_path, "report", "show", "build-000001", "extra", NULL};
        const char *unknown_report[] = {
            solar_path, "report", "--unknown", NULL};
        const char *report_help[] = {solar_path, "report", "--help", NULL};

        failed += run_cli(solar_path, root, plain, 0,
            "SOLAR LAST BUILD REPORT", NULL);
        failed += run_cli(solar_path, root, list_arguments, 0,
            "build-000002", NULL);
        failed += run_cli(solar_path, root, show_arguments, 0,
            "SOLAR LAST BUILD REPORT", NULL);
        failed += run_cli(solar_path, root, compare_arguments, 0,
            "COMPARISON SUMMARY", NULL);
        failed += run_cli(solar_path, root, explicit_arguments, 0,
            "GENERIC SYNTHESIS STATISTICS", NULL);
        failed += run_cli(solar_path, root, invalid_arguments, 2, NULL,
            "--summary may be specified only once");
        failed += run_cli(solar_path, root, missing_against, 2, NULL,
            "invalid or duplicate --against");
        failed += run_cli(solar_path, root, unknown_against, 3, NULL,
            "stored build report was not found");
        failed += run_cli(solar_path, root, two_current, 2, NULL,
            "only one current build ID");
        failed += run_cli(solar_path, root, zero_limit, 2, NULL,
            "invalid report list limit");
        failed += run_cli(solar_path, root, invalid_limit, 2, NULL,
            "invalid report list limit");
        failed += run_cli(solar_path, root, missing_show, 2, NULL,
            "requires exactly one build ID");
        failed += run_cli(solar_path, root, extra_show, 2, NULL,
            "requires exactly one build ID");
        failed += run_cli(solar_path, root, unknown_report, 2, NULL,
            "unknown solar report command");
        failed += run_cli(solar_path, root, report_help, 0,
            "solar report compare", NULL);
        if (failed != 0) goto cleanup;
    }

    legacy_directory = join_path(root, ".solar/reports/build-000099");
    legacy_report = join_path(legacy_directory, "report.txt");
    corrupt_directory = join_path(root, ".solar/reports/build-000098");
    corrupt_report = join_path(corrupt_directory, "report.txt");
    corrupt_metadata = join_path(corrupt_directory, "metadata.dat");
    if (legacy_directory == NULL || legacy_report == NULL ||
        corrupt_directory == NULL || corrupt_report == NULL ||
        corrupt_metadata == NULL || mkdir(legacy_directory, 0700) != 0 ||
        mkdir(corrupt_directory, 0700) != 0 ||
        write_text(legacy_report, "legacy report\n") != 0 ||
        write_text(corrupt_report, "corrupt report\n") != 0 ||
        write_text(corrupt_metadata, "schema=999\n") != 0) {
        failed = fail("could not prepare legacy/corrupt history fixtures");
        goto cleanup;
    }
    result = solar_report_history_load(&project, "build-000099", &baseline);
    if (result.status != SOLAR_STATUS_OK || !baseline.has_report ||
        baseline.has_timings || baseline.has_synthesis_statistics) {
        failed = fail("TXT-only legacy report is not readable");
        goto cleanup;
    }
    result = solar_report_history_load(&project, "build-000098", &corrupt);
    if (result.status == SOLAR_STATUS_OK) {
        failed = fail("corrupt metadata was accepted");
        goto cleanup;
    }
    result = solar_report_history_list(&project, &list);
    if (result.status != SOLAR_STATUS_OK || list.count != 4U ||
        strcmp(list.items[0].build_id, "build-000099") != 0 ||
        strcmp(list.items[1].status, "corrupt") != 0) {
        failed = fail("history listing or legacy status is incorrect");
    }

cleanup:
    free(text);
    solar_build_report_list_free(&list);
    solar_stored_build_report_free(&corrupt);
    solar_stored_build_report_free(&baseline);
    solar_stored_build_report_free(&latest);
    solar_project_free(&project);
    solar_build_context_free(&context);
    free(corrupt_metadata);
    free(corrupt_report);
    free(corrupt_directory);
    free(legacy_report);
    free(legacy_directory);
    free(manifest);
    return failed;
}

int main(int argc, char **argv)
{
    int failed = 0;

    if (argc != 2) return fail("expected the Solar executable path");

    failed += test_metric_math();
    failed += test_gss_comparison();
    failed += test_timing_comparison();
    failed += test_context_warnings();
    failed += test_incompatible_contexts_are_section_local();
    failed += test_failed_invalid_context_is_stored();
    failed += test_order_independent_sidecars();
    failed += test_history(argv[1]);
    if (failed == 0) (void)printf("report history tests passed\n");
    return failed == 0 ? 0 : 1;
}
