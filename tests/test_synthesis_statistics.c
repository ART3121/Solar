#define _POSIX_C_SOURCE 200809L

#include "solar/build.h"
#include "solar/filesystem.h"
#include "solar/synthesis_statistics.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "synthesis statistics: %s\n", message);
    return 1;
}

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static char *fixture_path(const char *name)
{
    return join_path(SOLAR_TEST_FIXTURES, name);
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int expect_cell(
    const SolarGenericSynthesisStatistics *statistics,
    size_t index,
    const char *type,
    uint64_t count
)
{
    const SolarSynthesisCellUsage *cell =
        solar_synthesis_statistics_cell_at(statistics, index);

    return cell == NULL || strcmp(cell->type, type) != 0 ||
        cell->count != count;
}

static int test_typical_and_copy(void)
{
    char *path = fixture_path("typical.txt");
    SolarGenericSynthesisStatistics statistics;
    SolarGenericSynthesisStatistics copy;
    SolarResult result;
    int failed = 0;

    solar_synthesis_statistics_init(&statistics);
    solar_synthesis_statistics_init(&copy);
    if (path == NULL) return fail("could not allocate typical fixture path");
    result = solar_synthesis_statistics_analyze(path, &statistics);
    if (result.status != SOLAR_STATUS_OK || !statistics.available ||
        statistics.completeness != SOLAR_SYNTHESIS_STATISTICS_COMPLETE ||
        statistics.modules != 1U || statistics.wires != 38U ||
        statistics.wire_bits != 126U || statistics.public_wires != 12U ||
        statistics.public_wire_bits != 34U || statistics.memories != 1U ||
        statistics.memory_bits != 256U || statistics.processes != 0U ||
        statistics.cells != 24U || statistics.cell_type_count != 5U ||
        expect_cell(&statistics, 0U, "$dff", 8U) ||
        expect_cell(&statistics, 1U, "$mux", 6U) ||
        expect_cell(&statistics, 2U, "$and", 4U) ||
        expect_cell(&statistics, 3U, "$not", 4U) ||
        expect_cell(&statistics, 4U, "$add", 2U)) {
        failed = fail("typical output or deterministic cell ordering was incorrect");
        goto cleanup;
    }
    result = solar_synthesis_statistics_copy(&copy, &statistics);
    if (result.status != SOLAR_STATUS_OK || copy.cell_type_count != 5U ||
        copy.cell_types[0].type == statistics.cell_types[0].type ||
        expect_cell(&copy, 0U, "$dff", 8U)) {
        failed = fail("statistics deep copy was incorrect");
    }

cleanup:
    solar_synthesis_statistics_free(&copy);
    solar_synthesis_statistics_free(&statistics);
    free(path);
    return failed;
}

static int test_multiple_modules(void)
{
    char *hierarchy_path = fixture_path("multiple_modules.txt");
    char *aggregate_path = fixture_path("multiple_no_summary.txt");
    SolarGenericSynthesisStatistics statistics;
    SolarResult result;
    int failed = 0;

    solar_synthesis_statistics_init(&statistics);
    if (hierarchy_path == NULL || aggregate_path == NULL) {
        failed = fail("could not allocate multi-module fixture paths");
        goto cleanup;
    }
    result = solar_synthesis_statistics_analyze(hierarchy_path, &statistics);
    if (result.status != SOLAR_STATUS_OK || statistics.modules != 2U ||
        statistics.wires != 18U || statistics.cells != 7U ||
        statistics.cell_type_count != 3U ||
        expect_cell(&statistics, 0U, "$dff", 4U)) {
        failed = fail("design hierarchy summary was double counted");
        goto cleanup;
    }
    solar_synthesis_statistics_free(&statistics);
    result = solar_synthesis_statistics_analyze(aggregate_path, &statistics);
    if (result.status != SOLAR_STATUS_OK || statistics.modules != 2U ||
        statistics.wires != 6U || statistics.memory_bits != 16U ||
        statistics.cells != 3U || statistics.cell_type_count != 2U ||
        expect_cell(&statistics, 0U, "$and", 2U) ||
        expect_cell(&statistics, 1U, "$or", 1U)) {
        failed = fail("per-module statistics were not accumulated correctly");
    }

cleanup:
    solar_synthesis_statistics_free(&statistics);
    free(aggregate_path);
    free(hierarchy_path);
    return failed;
}

static int test_zero_unknown_partial_and_large(void)
{
    const char *names[] = {
        "no_memories.txt", "no_cells.txt", "partial.txt", "large_counts.txt",
        "modern.txt"
    };
    size_t index;

    for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        char *path = fixture_path(names[index]);
        SolarGenericSynthesisStatistics statistics;
        SolarResult result;

        solar_synthesis_statistics_init(&statistics);
        if (path == NULL) return fail("could not allocate fixture path");
        result = solar_synthesis_statistics_analyze(path, &statistics);
        if (result.status != SOLAR_STATUS_OK) {
            free(path);
            solar_synthesis_statistics_free(&statistics);
            return fail("valid zero, partial, or large output was rejected");
        }
        if (index == 0U &&
            (statistics.memories != 0U || statistics.memory_bits != 0U ||
             expect_cell(&statistics, 0U, "custom_unknown_cell", 1U))) {
            free(path);
            solar_synthesis_statistics_free(&statistics);
            return fail("zero memories or unknown cell type was parsed incorrectly");
        }
        if (index == 1U &&
            (statistics.cells != 0U || statistics.cell_type_count != 0U ||
             statistics.completeness != SOLAR_SYNTHESIS_STATISTICS_COMPLETE)) {
            free(path);
            solar_synthesis_statistics_free(&statistics);
            return fail("zero-cell design was not complete");
        }
        if (index == 2U &&
            (statistics.completeness != SOLAR_SYNTHESIS_STATISTICS_PARTIAL ||
             solar_synthesis_statistics_has_field(
                &statistics, SOLAR_SYNTHESIS_FIELD_MEMORIES))) {
            free(path);
            solar_synthesis_statistics_free(&statistics);
            return fail("partial fields were presented as reported zeros");
        }
        if (index == 3U && statistics.wires != UINT64_MAX) {
            free(path);
            solar_synthesis_statistics_free(&statistics);
            return fail("maximum 64-bit count was not accepted");
        }
        if (index == 4U &&
            (statistics.wires != 4U || statistics.wire_bits != 10U ||
             statistics.cells != 2U || statistics.cell_type_count != 2U ||
             expect_cell(&statistics, 0U, "$add", 1U) ||
             expect_cell(&statistics, 1U, "$sdff", 1U))) {
            free(path);
            solar_synthesis_statistics_free(&statistics);
            return fail("modern compact Yosys output was parsed incorrectly");
        }
        free(path);
        solar_synthesis_statistics_free(&statistics);
    }
    return 0;
}

static int test_invalid_inputs(void)
{
    char *empty_path = fixture_path("empty.txt");
    char *invalid_path = fixture_path("invalid_number.txt");
    char *missing_path = fixture_path("does-not-exist.txt");
    SolarGenericSynthesisStatistics statistics;
    SolarResult result;
    int failed = 0;

    solar_synthesis_statistics_init(&statistics);
    if (empty_path == NULL || invalid_path == NULL || missing_path == NULL) {
        failed = fail("could not allocate invalid fixture paths");
        goto cleanup;
    }
    result = solar_synthesis_statistics_analyze(empty_path, &statistics);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR || statistics.available) {
        failed = fail("empty output was not distinguished from valid statistics");
        goto cleanup;
    }
    result = solar_synthesis_statistics_analyze(missing_path, &statistics);
    if (result.status != SOLAR_STATUS_NOT_FOUND) {
        failed = fail("missing output did not return not found");
        goto cleanup;
    }
    result = solar_synthesis_statistics_analyze(invalid_path, &statistics);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "invalid numeric") == NULL) {
        failed = fail("malformed numeric output lacked an actionable diagnostic");
    }

cleanup:
    solar_synthesis_statistics_free(&statistics);
    free(missing_path);
    free(invalid_path);
    free(empty_path);
    return failed;
}

static int set_metadata(
    SolarGenericSynthesisStatistics *statistics,
    const char *report_path
)
{
    statistics->tool = strdup("Yosys");
    statistics->tool_version = strdup("Yosys 0.test");
    statistics->top = strdup("counter");
    statistics->report_path = strdup(report_path);
    return statistics->tool == NULL || statistics->tool_version == NULL ||
        statistics->top == NULL || statistics->report_path == NULL;
}

static int test_persistence_and_legacy_report(void)
{
    char root_template[] = "/tmp/solar-synthesis-statistics-XXXXXX";
    char *root = mkdtemp(root_template);
    char *manifest = NULL;
    char *fixture = fixture_path("typical.txt");
    char *report = NULL;
    SolarBuildContext context;
    SolarGenericSynthesisStatistics loaded;
    SolarResult result;
    bool removed = false;
    int failed = 0;

    solar_build_context_init(&context);
    solar_synthesis_statistics_init(&loaded);
    if (root == NULL || fixture == NULL) return fail("could not create report fixture");
    manifest = join_path(root, "solar.toml");
    report = join_path(root, ".solar/state/last-report.txt");
    if (manifest == NULL || report == NULL ||
        write_text(manifest, "[solar]\nformat = 2\n") != 0) {
        failed = fail("could not create persisted report project");
        goto cleanup;
    }
    context.project.root = strdup(root);
    context.project.manifest_path = strdup(manifest);
    context.operation = strdup("build synth");
    if (context.project.root == NULL || context.project.manifest_path == NULL ||
        context.operation == NULL) {
        failed = fail("could not allocate report context");
        goto cleanup;
    }
    result = solar_synthesis_statistics_analyze(
        fixture, &context.synthesis_result.statistics
    );
    if (result.status != SOLAR_STATUS_OK ||
        set_metadata(&context.synthesis_result.statistics, fixture)) {
        failed = fail("could not prepare normalized report statistics");
        goto cleanup;
    }
    context.synthesis_result.statistics_result = solar_result_ok();
    context.synthesis_result.result = solar_result_ok();
    context.has_synthesis_result = true;
    context.result = solar_result_ok();
    result = solar_build_report_write(&context);
    if (result.status != SOLAR_STATUS_OK) {
        failed = fail(result.diagnostic.message);
        goto cleanup;
    }
    result = solar_synthesis_statistics_load_last_report(root, &loaded);
    if (result.status != SOLAR_STATUS_OK || !loaded.available ||
        loaded.wires != 38U || loaded.cell_type_count != 5U ||
        loaded.tool_version == NULL ||
        strcmp(loaded.tool_version, "Yosys 0.test") != 0 ||
        loaded.report_path == NULL || strcmp(loaded.report_path, fixture) != 0 ||
        expect_cell(&loaded, 0U, "$dff", 8U)) {
        failed = fail("normalized report did not round-trip");
        goto cleanup;
    }
    solar_synthesis_statistics_free(&loaded);
    if (write_text(report, "SOLAR LAST BUILD REPORT\nlegacy report\n") != 0) {
        failed = fail("could not replace report with legacy fixture");
        goto cleanup;
    }
    result = solar_synthesis_statistics_load_last_report(root, &loaded);
    if (result.status != SOLAR_STATUS_OK || loaded.available ||
        loaded.reported_fields != 0U) {
        failed = fail("legacy report without statistics was not compatible");
    }

cleanup:
    solar_synthesis_statistics_free(&loaded);
    solar_build_context_free(&context);
    (void)solar_filesystem_clean_project(root, &removed);
    if (manifest != NULL) (void)unlink(manifest);
    if (root != NULL) (void)rmdir(root);
    free(report);
    free(fixture);
    free(manifest);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_typical_and_copy();
    failed += test_multiple_modules();
    failed += test_zero_unknown_partial_and_large();
    failed += test_invalid_inputs();
    failed += test_persistence_and_legacy_report();
    return failed == 0 ? 0 : 1;
}
