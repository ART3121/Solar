#include "solar/compiler.h"

#include "../backends/compiler_backend_internal.h"

#include <stdlib.h>
#include <string.h>

static const SolarCompilerBackend *const COMPILER_BACKENDS[] = {
    &SOLAR_YANC_COMPILER_BACKEND
};

void solar_generated_artifacts_init(SolarGeneratedArtifacts *artifacts)
{
    if (artifacts != NULL) (void)memset(artifacts, 0, sizeof(*artifacts));
}

void solar_generated_artifacts_free(SolarGeneratedArtifacts *artifacts)
{
    if (artifacts == NULL) return;
    free(artifacts->build_directory);
    free(artifacts->working_directory);
    free(artifacts->assembly_path);
    free(artifacts->instruction_image_path);
    free(artifacts->data_image_path);
    free(artifacts->testbench_path);
    free(artifacts->instruction_translation_path);
    free(artifacts->processor_top);
    free(artifacts->testbench_top);
    solar_string_list_free(&artifacts->rtl_sources);
    solar_string_list_free(&artifacts->hdl_sources);
    solar_string_list_free(&artifacts->auxiliary_files);
    solar_generated_artifacts_init(artifacts);
}

void solar_compiler_result_init(SolarCompilerResult *result)
{
    if (result != NULL) (void)memset(result, 0, sizeof(*result));
}

void solar_compiler_result_free(SolarCompilerResult *result)
{
    size_t index;

    if (result == NULL) return;
    free(result->backend);
    free(result->toolchain_root);
    free(result->toolchain_version);
    solar_generated_artifacts_free(&result->artifacts);
    for (index = 0U; index < result->stage_count; index++) {
        free(result->stages[index].name);
        free(result->stages[index].executable_path);
        free(result->stages[index].log_path);
    }
    free(result->stages);
    solar_compiler_result_init(result);
}

SolarResult solar_compiler_record_stage(
    SolarCompilerResult *compiler_result,
    const char *name,
    const char *executable_path,
    const char *log_path,
    uint64_t duration_ns,
    SolarResult stage_result
)
{
    SolarCompilerStageResult *stages;
    SolarCompilerStageResult *stage;

    if (compiler_result == NULL || name == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot record an unnamed compiler stage",
            "provide compiler result storage and a stage name"
        );
    }
    stages = realloc(
        compiler_result->stages,
        (compiler_result->stage_count + 1U) * sizeof(*stages)
    );
    if (stages == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not grow compiler stage results",
            "free memory and try again"
        );
    }
    compiler_result->stages = stages;
    stage = &stages[compiler_result->stage_count];
    (void)memset(stage, 0, sizeof(*stage));
    stage->name = strdup(name);
    if (executable_path != NULL) {
        stage->executable_path = strdup(executable_path);
    }
    if (log_path != NULL) stage->log_path = strdup(log_path);
    if (stage->name == NULL ||
        (executable_path != NULL && stage->executable_path == NULL) ||
        (log_path != NULL && stage->log_path == NULL)) {
        free(stage->name);
        free(stage->executable_path);
        free(stage->log_path);
        (void)memset(stage, 0, sizeof(*stage));
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not copy compiler stage metadata",
            "free memory and try again"
        );
    }
    stage->result = stage_result;
    stage->duration_ns = duration_ns;
    compiler_result->stage_count++;
    return solar_result_ok();
}

static const SolarCompilerBackend *find_compiler_backend(const char *name)
{
    size_t index;

    if (name == NULL) return NULL;
    for (index = 0U;
         index < sizeof(COMPILER_BACKENDS) / sizeof(COMPILER_BACKENDS[0]);
         index++) {
        if (strcmp(name, COMPILER_BACKENDS[index]->name) == 0) {
            return COMPILER_BACKENDS[index];
        }
    }
    return NULL;
}

SolarResult solar_compiler_compile(
    const SolarProject *project,
    SolarCompilerResult *compiler_result
)
{
    return solar_compiler_compile_with_progress(project, NULL, compiler_result);
}

SolarResult solar_compiler_compile_with_progress(
    const SolarProject *project,
    const SolarProgressObserver *progress_observer,
    SolarCompilerResult *compiler_result
)
{
    const SolarCompilerBackend *backend;
    SolarResult result;

    if (compiler_result != NULL) solar_compiler_result_init(compiler_result);
    if (project == NULL || compiler_result == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot compile without a project and result storage",
            "load a project before invoking the Compiler Service"
        );
    }
    compiler_result->progress_observer = progress_observer;
    result = solar_project_validate(project);
    if (result.status != SOLAR_STATUS_OK) return result;
    if (project->config.compiler.backend == NULL) {
        return solar_result_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "this project has no hardware generation stage configured",
            "solar build rtl needs [compiler].backend for source-to-hardware projects"
        );
    }
    backend = find_compiler_backend(project->config.compiler.backend);
    if (backend == NULL || backend->compile == NULL) {
        return solar_result_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "the configured compiler backend is not registered",
            "select a compiler backend supported by this Solar build"
        );
    }
    compiler_result->backend = strdup(backend->name);
    if (compiler_result->backend == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not copy compiler backend metadata",
            "free memory and try again"
        );
    }
    return backend->compile(project, compiler_result);
}

SolarResult solar_compiler_prepare_waveform_layout(
    const SolarProject *project,
    const SolarGeneratedArtifacts *artifacts,
    const char *test_name,
    const char *waveform_path,
    char **layout_path_out
)
{
    const SolarCompilerBackend *backend;

    if (layout_path_out != NULL) *layout_path_out = NULL;
    if (project == NULL || artifacts == NULL || test_name == NULL ||
        waveform_path == NULL || layout_path_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot prepare a waveform layout without generated artifacts",
            "provide a generated test name, waveform, and result storage"
        );
    }
    backend = find_compiler_backend(project->config.compiler.backend);
    if (backend == NULL || backend->prepare_waveform_layout == NULL) {
        return solar_result_error(
            SOLAR_STATUS_NOT_FOUND,
            "the compiler backend provides no waveform presentation",
            "open the generated waveform without a backend-specific layout"
        );
    }
    return backend->prepare_waveform_layout(
        project, artifacts, test_name, waveform_path, layout_path_out
    );
}

SolarResult solar_compiler_find_waveform_layout(
    const SolarProject *project,
    const char *test_name,
    const char *waveform_path,
    char **layout_path_out
)
{
    const SolarCompilerBackend *backend;

    if (layout_path_out != NULL) *layout_path_out = NULL;
    if (project == NULL || test_name == NULL || waveform_path == NULL ||
        layout_path_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot find a waveform layout without a generated test",
            "provide a generated test name, waveform, and result storage"
        );
    }
    backend = find_compiler_backend(project->config.compiler.backend);
    if (backend == NULL || backend->find_waveform_layout == NULL) {
        return solar_result_ok();
    }
    return backend->find_waveform_layout(
        project, test_name, waveform_path, layout_path_out
    );
}
