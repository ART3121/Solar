#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

#include "solar/compiler.h"
#include "solar/project.h"
#include "solar/progress.h"
#include "solar/result.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SOLAR_TEST_FAILURE_NONE = 0,
    SOLAR_TEST_FAILURE_CONFIGURATION,
    SOLAR_TEST_FAILURE_TOOL_MISSING,
    SOLAR_TEST_FAILURE_GENERATION,
    SOLAR_TEST_FAILURE_SIMULATION_COMPILE,
    SOLAR_TEST_FAILURE_SIMULATION_RUNTIME,
    SOLAR_TEST_FAILURE_LOGICAL,
    SOLAR_TEST_FAILURE_FILESYSTEM,
    SOLAR_TEST_FAILURE_INTERNAL
} SolarTestFailureKind;

typedef struct {
    char *name;
    char *profile_name;
    char *executable_path;
    char *waveform_path;
    char *waveform_layout_path;
    SolarDiagnostic waveform_layout_diagnostic;
    /* Owned VVP stdout; preserved independently in the per-test run log. */
    char *output;
    uint64_t compile_duration_ns;
    uint64_t simulation_duration_ns;
    uint64_t source_resolution_duration_ns;
    uint64_t publication_duration_ns;
    uint64_t total_process_duration_ns;
    SolarTestFailureKind failure_kind;
    SolarResult result;
} SolarTestResult;

typedef struct {
    SolarTestResult *results;
    size_t count;
    size_t passed;
    size_t failed;
} SolarTestSummary;

void solar_test_result_init(SolarTestResult *result);
void solar_test_result_free(SolarTestResult *result);
void solar_test_summary_init(SolarTestSummary *summary);
void solar_test_summary_free(SolarTestSummary *summary);
const char *solar_test_failure_kind_name(SolarTestFailureKind kind);

SolarResult solar_test_run(
    const SolarProject *project,
    const char *test_name,
    const char *profile_name,
    SolarTestResult *test_result
);

/*
 * Reuses generated_artifacts for compiler projects. Pass NULL to retain the
 * standalone behavior in solar_test_run(), which generates hardware itself.
 */
SolarResult solar_test_run_with_artifacts(
    const SolarProject *project,
    const char *test_name,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarTestResult *test_result
);
SolarResult solar_test_run_with_artifacts_and_progress(
    const SolarProject *project,
    const char *test_name,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    const SolarProgressObserver *progress_observer,
    SolarTestResult *test_result
);

/* Individual failures are stored in summary; orchestration errors are returned. */
SolarResult solar_test_run_all(
    const SolarProject *project,
    const char *profile_name,
    SolarTestSummary *summary
);

SolarResult solar_test_run_all_with_artifacts(
    const SolarProject *project,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarTestSummary *summary
);
SolarResult solar_test_run_all_with_artifacts_and_progress(
    const SolarProject *project,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    const SolarProgressObserver *progress_observer,
    SolarTestSummary *summary
);

#endif
