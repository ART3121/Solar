#ifndef SOLAR_COMPILER_H
#define SOLAR_COMPILER_H

#include "solar/project.h"
#include "solar/progress.h"
#include "solar/result.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *build_directory;
    char *working_directory;
    char *assembly_path;
    char *instruction_image_path;
    char *data_image_path;
    char *testbench_path;
    char *instruction_translation_path;
    char *processor_top;
    char *testbench_top;
    SolarStringList rtl_sources;
    SolarStringList hdl_sources;
    SolarStringList auxiliary_files;
} SolarGeneratedArtifacts;

typedef struct {
    char *name;
    char *executable_path;
    char *log_path;
    uint64_t duration_ns;
    SolarResult result;
} SolarCompilerStageResult;

typedef struct {
    char *backend;
    char *toolchain_root;
    char *toolchain_version;
    SolarGeneratedArtifacts artifacts;
    SolarCompilerStageResult *stages;
    size_t stage_count;
    /* Borrowed only during compilation; never freed with this result. */
    const SolarProgressObserver *progress_observer;
} SolarCompilerResult;

void solar_generated_artifacts_init(SolarGeneratedArtifacts *artifacts);
void solar_generated_artifacts_free(SolarGeneratedArtifacts *artifacts);
void solar_compiler_result_init(SolarCompilerResult *result);
void solar_compiler_result_free(SolarCompilerResult *result);

/*
 * Compiles one configured source-to-hardware project. The returned result owns
 * all paths and stage diagnostics until solar_compiler_result_free().
 */
SolarResult solar_compiler_compile(
    const SolarProject *project,
    SolarCompilerResult *compiler_result
);
SolarResult solar_compiler_compile_with_progress(
    const SolarProject *project,
    const SolarProgressObserver *progress_observer,
    SolarCompilerResult *compiler_result
);

/*
 * Prepares an optional viewer layout for one generated waveform. The selected
 * compiler backend owns the tool-specific work. layout_path_out is owned by
 * the caller on success.
 */
SolarResult solar_compiler_prepare_waveform_layout(
    const SolarProject *project,
    const SolarGeneratedArtifacts *artifacts,
    const char *test_name,
    const char *waveform_path,
    char **layout_path_out
);

/*
 * Finds a previously prepared layout without invoking external tools. Backends
 * with no presentation support return success with a NULL output.
 */
SolarResult solar_compiler_find_waveform_layout(
    const SolarProject *project,
    const char *test_name,
    const char *waveform_path,
    char **layout_path_out
);

#endif
