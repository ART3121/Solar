#include "solar/test.h"

#include "solar/artifact.h"
#include "solar/backend.h"
#include "solar/compiler.h"
#include "solar/cocotb.h"
#include "solar/filesystem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SOLAR_TEST_OUTPUT_LIMIT ((size_t)16U * 1024U * 1024U)

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0U;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
        (uint64_t)value.tv_nsec;
}

static uint64_t elapsed_since(uint64_t started)
{
    uint64_t now = monotonic_nanoseconds();

    return now >= started ? now - started : 0U;
}

void solar_simulation_request_init(SolarSimulationRequest *request)
{
    if (request != NULL) (void)memset(request, 0, sizeof(*request));
}

void solar_simulation_request_free(SolarSimulationRequest *request)
{
    if (request == NULL) return;
    free(request->backend);
    free(request->project_root);
    free(request->working_directory);
    free(request->test_name);
    free(request->top);
    solar_string_list_free(&request->rtl_sources);
    solar_string_list_free(&request->test_sources);
    solar_string_list_free(&request->include_dirs);
    solar_string_list_free(&request->defines);
    free(request->executable_path);
    free(request->waveform_path);
    free(request->compile_stdout_log);
    free(request->compile_stderr_log);
    free(request->run_stdout_log);
    free(request->run_stderr_log);
    solar_simulation_request_init(request);
}

void solar_test_result_init(SolarTestResult *result)
{
    if (result == NULL) return;
    (void)memset(result, 0, sizeof(*result));
    result->result = solar_result_ok();
}

void solar_test_result_free(SolarTestResult *result)
{
    if (result == NULL) return;
    free(result->name);
    free(result->profile_name);
    free(result->executable_path);
    free(result->waveform_path);
    free(result->waveform_layout_path);
    free(result->output);
    solar_test_result_init(result);
}

static SolarResult load_test_output(const char *path, char **output)
{
    static const char truncation_notice[] =
        "\n[solar: simulation output truncated at 16 MiB]\n";
    struct stat information;
    FILE *file = NULL;
    int descriptor = -1;
    char *text = NULL;
    size_t length;
    size_t count;
    size_t allocation;
    bool truncated;
    int read_error;
    int close_result;

    *output = NULL;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0) {
        if (descriptor >= 0) (void)close(descriptor);
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot inspect the simulation output log",
            "check .solar/logs/tests and filesystem permissions"
        );
    }
    truncated = (uintmax_t)information.st_size >
        (uintmax_t)SOLAR_TEST_OUTPUT_LIMIT;
    length = truncated ? SOLAR_TEST_OUTPUT_LIMIT : (size_t)information.st_size;
    allocation = length + (truncated ? sizeof(truncation_notice) - 1U : 0U) + 1U;
    text = malloc(allocation);
    if (text == NULL) {
        (void)close(descriptor);
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate simulation output text",
            "free memory and try again"
        );
    }
    file = fdopen(descriptor, "r");
    if (file == NULL) {
        (void)close(descriptor);
        free(text);
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot open the simulation output log",
            "check .solar/logs/tests and filesystem permissions"
        );
    }
    count = fread(text, 1U, length, file);
    read_error = ferror(file);
    close_result = fclose(file);
    if (count != length || read_error != 0 || close_result != 0) {
        free(text);
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot read the simulation output log",
            "check .solar/logs/tests and filesystem permissions"
        );
    }
    if (truncated) {
        (void)memcpy(text + length, truncation_notice,
            sizeof(truncation_notice) - 1U);
        length += sizeof(truncation_notice) - 1U;
    }
    text[length] = '\0';
    *output = text;
    return solar_result_ok();
}

void solar_test_summary_init(SolarTestSummary *summary)
{
    if (summary != NULL) (void)memset(summary, 0, sizeof(*summary));
}

void solar_test_summary_free(SolarTestSummary *summary)
{
    size_t index;

    if (summary == NULL) return;
    for (index = 0U; index < summary->count; index++) {
        solar_test_result_free(&summary->results[index]);
    }
    free(summary->results);
    solar_test_summary_init(summary);
}

const char *solar_test_failure_kind_name(SolarTestFailureKind kind)
{
    switch (kind) {
    case SOLAR_TEST_FAILURE_NONE: return "none";
    case SOLAR_TEST_FAILURE_CONFIGURATION: return "configuration error";
    case SOLAR_TEST_FAILURE_TOOL_MISSING: return "tool missing";
    case SOLAR_TEST_FAILURE_GENERATION: return "hardware generation failure";
    case SOLAR_TEST_FAILURE_SIMULATION_COMPILE:
        return "simulation compile failure";
    case SOLAR_TEST_FAILURE_SIMULATION_RUNTIME:
        return "simulation runtime failure";
    case SOLAR_TEST_FAILURE_LOGICAL: return "logical testbench failure";
    case SOLAR_TEST_FAILURE_FILESYSTEM: return "filesystem failure";
    case SOLAR_TEST_FAILURE_INTERNAL: return "internal Solar failure";
    }
    return "unknown test failure";
}

static SolarTestFailureKind generic_failure_kind(SolarResult result)
{
    switch (result.status) {
    case SOLAR_STATUS_OK: return SOLAR_TEST_FAILURE_NONE;
    case SOLAR_STATUS_CONFIG_ERROR:
    case SOLAR_STATUS_NOT_FOUND:
    case SOLAR_STATUS_INVALID_ARGUMENT:
        return SOLAR_TEST_FAILURE_CONFIGURATION;
    case SOLAR_STATUS_TOOL_MISSING:
        return SOLAR_TEST_FAILURE_TOOL_MISSING;
    case SOLAR_STATUS_IO_ERROR:
        return SOLAR_TEST_FAILURE_FILESYSTEM;
    case SOLAR_STATUS_INTERNAL_ERROR:
        return SOLAR_TEST_FAILURE_INTERNAL;
    case SOLAR_STATUS_PROCESS_FAILED:
        return SOLAR_TEST_FAILURE_SIMULATION_RUNTIME;
    }
    return SOLAR_TEST_FAILURE_INTERNAL;
}

static SolarTestFailureKind simulation_failure_kind(
    SolarResult result,
    SolarSimulationFailureKind simulation_failure
)
{
    if (result.status == SOLAR_STATUS_TOOL_MISSING) {
        return SOLAR_TEST_FAILURE_TOOL_MISSING;
    }
    switch (simulation_failure) {
    case SOLAR_SIMULATION_FAILURE_COMPILE:
        return SOLAR_TEST_FAILURE_SIMULATION_COMPILE;
    case SOLAR_SIMULATION_FAILURE_RUNTIME:
        return SOLAR_TEST_FAILURE_SIMULATION_RUNTIME;
    case SOLAR_SIMULATION_FAILURE_LOGICAL:
        return SOLAR_TEST_FAILURE_LOGICAL;
    case SOLAR_SIMULATION_FAILURE_NONE:
        return generic_failure_kind(result);
    }
    return SOLAR_TEST_FAILURE_INTERNAL;
}

static SolarResult copy_string(char **output, const char *value)
{
    if (value == NULL) return solar_result_ok();
    *output = strdup(value);
    if (*output == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not copy test execution data",
            "free memory and try again"
        );
    }
    return solar_result_ok();
}

static SolarResult make_relative_path(
    const char *directory,
    const char *file_name,
    char **path_out
)
{
    size_t directory_length;
    size_t name_length;
    char *path;

    if (path_out != NULL) *path_out = NULL;
    if (directory == NULL || file_name == NULL || path_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot build a test artifact path from a missing value",
            "validate the selected test before preparing its artifacts"
        );
    }
    directory_length = strlen(directory);
    name_length = strlen(file_name);
    path = malloc(directory_length + name_length + 2U);
    if (path == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a test artifact path",
            "free memory and try again"
        );
    }
    (void)memcpy(path, directory, directory_length);
    path[directory_length] = '/';
    (void)memcpy(path + directory_length + 1U, file_name, name_length + 1U);
    *path_out = path;
    return solar_result_ok();
}

static SolarResult resolve_list(
    const SolarProject *project,
    const SolarStringList *relative,
    SolarStringList *absolute
)
{
    size_t index;

    for (index = 0U; index < relative->count; index++) {
        char *resolved = NULL;
        SolarResult result = solar_project_resolve_path(
            project, relative->items[index], &resolved
        );

        if (result.status == SOLAR_STATUS_OK) {
            result = solar_string_list_append_copy(absolute, resolved);
        }
        free(resolved);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static SolarResult copy_list(
    const SolarStringList *source,
    SolarStringList *destination
)
{
    size_t index;

    for (index = 0U; index < source->count; index++) {
        SolarResult result = solar_string_list_append_copy(
            destination, source->items[index]
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static SolarResult copy_absolute_list(
    const SolarStringList *source,
    SolarStringList *destination
)
{
    return copy_list(source, destination);
}

static SolarResult resolve_request_paths(
    const SolarProject *project,
    const char *executable_relative,
    const char *waveform_relative,
    const char *compile_stdout_relative,
    const char *compile_stderr_relative,
    const char *run_stdout_relative,
    const char *run_stderr_relative,
    SolarSimulationRequest *request
)
{
    SolarResult result = solar_project_resolve_path(
        project, executable_relative, &request->executable_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, waveform_relative, &request->waveform_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, compile_stdout_relative, &request->compile_stdout_log
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, compile_stderr_relative, &request->compile_stderr_log
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, run_stdout_relative, &request->run_stdout_log
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_project_resolve_path(
        project, run_stderr_relative, &request->run_stderr_log
    );
}

static SolarResult prepare_v2_paths(
    const SolarProject *project,
    const SolarTest *test,
    SolarSimulationRequest *request
)
{
    char *build_directory = NULL;
    char *log_directory = NULL;
    char *executable = NULL;
    char *waveform = NULL;
    char *compile_stdout = NULL;
    char *compile_stderr = NULL;
    char *run_stdout = NULL;
    char *run_stderr = NULL;
    char *waveform_directory = NULL;
    SolarResult result = make_relative_path(
        ".solar/tmp/tests", test->name, &build_directory
    );

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_relative_path(".solar/logs/tests", test->name, &log_directory);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, build_directory
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, log_directory
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_relative_path(
        build_directory,
        strcmp(request->backend, "verilator") == 0
            ? "simulation"
            : "simulation.vvp",
        &executable
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_relative_path(build_directory, test->waveform, &waveform);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    waveform_directory = strdup(waveform);
    if (waveform_directory == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate the waveform directory path",
            "free memory and try again"
        );
        goto cleanup;
    }
    {
        char *separator = strrchr(waveform_directory, '/');

        if (separator != NULL) *separator = '\0';
    }
    result = solar_filesystem_prepare_generated_directory(
        project->root, waveform_directory
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_relative_path(
        log_directory,
        strcmp(request->backend, "verilator") == 0
            ? "verilator.stdout.log"
            : "iverilog.stdout.log",
        &compile_stdout
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_relative_path(
        log_directory,
        strcmp(request->backend, "verilator") == 0
            ? "verilator.stderr.log"
            : "iverilog.stderr.log",
        &compile_stderr
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_relative_path(
        log_directory,
        strcmp(request->backend, "verilator") == 0
            ? "simulation.stdout.log"
            : "vvp.stdout.log",
        &run_stdout
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_relative_path(
        log_directory,
        strcmp(request->backend, "verilator") == 0
            ? "simulation.stderr.log"
            : "vvp.stderr.log",
        &run_stderr
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_project_resolve_path(
        project, build_directory, &request->working_directory
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_request_paths(
        project, executable, waveform, compile_stdout, compile_stderr,
        run_stdout, run_stderr, request
    );

cleanup:
    free(waveform_directory);
    free(run_stderr);
    free(run_stdout);
    free(compile_stderr);
    free(compile_stdout);
    free(waveform);
    free(executable);
    free(log_directory);
    free(build_directory);
    return result;
}

static SolarResult build_request(
    const SolarProject *project,
    const SolarProfile *profile,
    const SolarTest *test,
    SolarSimulationRequest *request
)
{
    SolarEffectiveConfig effective;
    SolarResult result;

    solar_effective_config_init(&effective);
    solar_simulation_request_init(request);
    result = copy_string(&request->backend, project->config.simulation.backend);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->project_root, project->root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->test_name, test->name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->top, test->top);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_list(project, &project->config.sources.rtl, &request->rtl_sources);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_list(project, &test->sources, &request->test_sources);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_config_build_effective(
        &project->config, profile, test, &effective
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_list(project, &effective.include_dirs, &request->include_dirs);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_list(&effective.defines, &request->defines);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_layout(project->root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_v2_paths(project, test, request);

cleanup:
    solar_effective_config_free(&effective);
    if (result.status != SOLAR_STATUS_OK) solar_simulation_request_free(request);
    return result;
}

static SolarResult append_generated_sources(
    const SolarGeneratedArtifacts *artifacts,
    SolarSimulationRequest *request
)
{
    SolarResult result = copy_absolute_list(
        &artifacts->rtl_sources, &request->rtl_sources
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_absolute_list(
        &artifacts->hdl_sources, &request->rtl_sources
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_string_list_append_copy(
        &request->test_sources, artifacts->testbench_path
    );
}

static SolarResult build_generated_request(
    const SolarProject *project,
    const SolarProfile *profile,
    const SolarTest *test,
    const SolarGeneratedArtifacts *artifacts,
    SolarSimulationRequest *request
)
{
    SolarEffectiveConfig effective;
    SolarResult result;

    solar_effective_config_init(&effective);
    solar_simulation_request_init(request);
    result = copy_string(&request->backend, project->config.simulation.backend);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->project_root, project->root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->test_name, test->name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->top, artifacts->testbench_top);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = append_generated_sources(artifacts, request);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_config_build_effective(
        &project->config, profile, NULL, &effective
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_list(
        project, &effective.include_dirs, &request->include_dirs
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_list(&effective.defines, &request->defines);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_layout(project->root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_v2_paths(project, test, request);

cleanup:
    solar_effective_config_free(&effective);
    if (result.status != SOLAR_STATUS_OK) solar_simulation_request_free(request);
    return result;
}

static SolarResult test_file_error(
    SolarStatus status,
    const char *message_prefix,
    const char *path,
    int error_number
)
{
    SolarResult result;

    result.status = status;
    result.diagnostic.severity = SOLAR_DIAGNOSTIC_ERROR;
    (void)snprintf(
        result.diagnostic.message,
        sizeof(result.diagnostic.message),
        "%s %s: %s",
        message_prefix,
        path,
        strerror(error_number)
    );
    (void)snprintf(
        result.diagnostic.hint,
        sizeof(result.diagnostic.hint),
        "%s",
        "ensure generated test artifacts are readable and .solar is writable"
    );
    return result;
}

static SolarResult copy_runtime_file(
    const char *source,
    const char *working_directory
)
{
    unsigned char buffer[16384];
    const char *name = strrchr(source, '/');
    char *destination = NULL;
    struct stat information;
    int input = -1;
    int output = -1;
    SolarResult result;

    name = name == NULL ? source : name + 1;
    result = solar_filesystem_join(working_directory, name, &destination);
    if (result.status != SOLAR_STATUS_OK) return result;
    input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0) {
        result = test_file_error(
            SOLAR_STATUS_IO_ERROR, "cannot read generated runtime file",
            source, errno
        );
        goto cleanup;
    }
    if (fstat(input, &information) != 0) {
        result = test_file_error(
            SOLAR_STATUS_IO_ERROR, "cannot inspect generated runtime file",
            source, errno
        );
        goto cleanup;
    }
    if (!S_ISREG(information.st_mode)) {
        result = test_file_error(
            SOLAR_STATUS_IO_ERROR, "generated runtime path is not a regular file",
            source, EINVAL
        );
        goto cleanup;
    }
    output = open(
        destination,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    if (output < 0) {
        result = test_file_error(
            SOLAR_STATUS_IO_ERROR, "cannot create test runtime file",
            destination, errno
        );
        goto cleanup;
    }
    result = solar_result_ok();
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        size_t offset = 0U;

        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            result = test_file_error(
                SOLAR_STATUS_IO_ERROR, "cannot read generated runtime file",
                source, errno
            );
            goto cleanup;
        }
        if (count == 0) break;
        while (offset < (size_t)count) {
            ssize_t written = write(
                output, buffer + offset, (size_t)count - offset
            );

            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                result = test_file_error(
                    SOLAR_STATUS_IO_ERROR, "cannot write test runtime file",
                    destination, errno
                );
                goto cleanup;
            }
            offset += (size_t)written;
        }
    }

cleanup:
    if (output >= 0) (void)close(output);
    if (input >= 0) (void)close(input);
    free(destination);
    return result;
}

static bool is_program_counter_map(const char *path)
{
    const char *name = strrchr(path, '/');
    size_t length;

    name = name == NULL ? path : name + 1;
    length = strlen(name);
    return strncmp(name, "pc_", 3U) == 0 && length >= 8U &&
        strcmp(name + length - 8U, "_mem.txt") == 0;
}

static SolarResult stage_generated_runtime_files(
    const SolarGeneratedArtifacts *artifacts,
    const SolarSimulationRequest *request
)
{
    size_t index;
    SolarResult result = copy_runtime_file(
        artifacts->data_image_path, request->working_directory
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_runtime_file(
        artifacts->instruction_image_path, request->working_directory
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    for (index = 0U; index < artifacts->auxiliary_files.count; index++) {
        if (is_program_counter_map(artifacts->auxiliary_files.items[index])) {
            return copy_runtime_file(
                artifacts->auxiliary_files.items[index],
                request->working_directory
            );
        }
    }
    return solar_result_error(
        SOLAR_STATUS_PROCESS_FAILED,
        "generated YANC artifacts have no program-counter simulation map",
        "re-run solar build rtl and inspect the asmcomp log"
    );
}

static SolarResult reset_request_artifacts(
    const SolarProject *project,
    const SolarSimulationRequest *request
)
{
    const char *executable_relative = request->executable_path + strlen(project->root) + 1U;
    const char *waveform_relative = request->waveform_path + strlen(project->root) + 1U;
    SolarResult result = solar_filesystem_reset_artifact(
        project->root, executable_relative
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_filesystem_reset_artifact(project->root, waveform_relative);
}

static SolarResult set_test_result_metadata(
    const SolarTest *test,
    const SolarProfile *profile,
    const SolarSimulationRequest *request,
    SolarTestResult *test_result
)
{
    SolarResult result = copy_string(&test_result->name, test->name);

    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_string(
        &test_result->profile_name, profile == NULL ? NULL : profile->name
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_string(
        &test_result->executable_path, request->executable_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_result_ok();
}

static SolarResult publish_waveform(
    const SolarProject *project,
    const SolarProfile *profile,
    const SolarTest *test,
    const SolarSimulationRequest *request,
    SolarTestResult *test_result
)
{
    struct stat information;
    const char *name = strrchr(test->waveform, '/');
    const char *root = project->config.compiler.backend == NULL
        ? "sim" : "simulation";
    char *relative = NULL;
    SolarResult result;

    if (lstat(request->waveform_path, &information) != 0) {
        if (errno == ENOENT) return solar_result_ok();
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot inspect the simulation waveform",
            "check permissions below .solar/tmp/tests"
        );
    }
    if (!S_ISREG(information.st_mode) ||
        access(request->waveform_path, R_OK) != 0) {
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "the simulation waveform is not a readable regular file",
            "remove the conflicting internal path and run the simulation again"
        );
    }
    name = name == NULL ? test->waveform : name + 1;
    result = make_relative_path(root, name, &relative);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_artifact_publish_file(
            project->root, request->waveform_path, relative, "waveform",
            "simulation", test->name, request->backend,
            profile == NULL ? NULL : profile->name,
            &test_result->waveform_path
        );
    }
    free(relative);
    return result;
}

static void record_layout_warning(
    SolarTestResult *test_result,
    SolarResult layout_result
)
{
    test_result->waveform_layout_diagnostic = layout_result.diagnostic;
    test_result->waveform_layout_diagnostic.severity =
        SOLAR_DIAGNOSTIC_WARNING;
}

SolarResult solar_test_run_with_artifacts_and_progress(
    const SolarProject *project,
    const char *test_name,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    const SolarProgressObserver *progress_observer,
    SolarTestResult *test_result
)
{
    const SolarProfile *profile = NULL;
    const SolarTest *test = NULL;
    SolarCompilerResult compiler_result;
    const SolarGeneratedArtifacts *active_artifacts = generated_artifacts;
    SolarSimulationRequest request;
    SolarSimulationFailureKind simulation_failure =
        SOLAR_SIMULATION_FAILURE_NONE;
    SolarSimulationMetrics metrics = {0};
    SolarTestFailureKind failure_kind = SOLAR_TEST_FAILURE_NONE;
    uint64_t resolution_started = 0U;
    uint64_t publication_started = 0U;
    SolarResult result;

    solar_compiler_result_init(&compiler_result);
    if (test_result != NULL) solar_test_result_init(test_result);
    if (project == NULL || test_result == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot run a test without a project and result storage",
            "load a project before running a test"
        );
    }
    result = solar_project_validate(project);
    if (result.status != SOLAR_STATUS_OK) goto finish;
    result = solar_config_select_profile(
        &project->config, profile_name, &profile
    );
    if (result.status != SOLAR_STATUS_OK) goto finish;
    result = solar_config_select_test(&project->config, test_name, &test);
    if (result.status != SOLAR_STATUS_OK) goto finish;
    resolution_started = monotonic_nanoseconds();
    if (test->kind == SOLAR_TEST_GENERATED) {
        if (active_artifacts == NULL) {
            result = solar_compiler_compile(project, &compiler_result);
            if (result.status != SOLAR_STATUS_OK) {
                failure_kind = result.status == SOLAR_STATUS_TOOL_MISSING
                    ? SOLAR_TEST_FAILURE_TOOL_MISSING
                    : SOLAR_TEST_FAILURE_GENERATION;
            } else {
                active_artifacts = &compiler_result.artifacts;
            }
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = build_generated_request(
                project, profile, test, active_artifacts, &request
            );
        }
    } else {
        result = build_request(project, profile, test, &request);
    }
    if (result.status != SOLAR_STATUS_OK) goto finish;
    request.progress_observer = progress_observer;
    result = set_test_result_metadata(test, profile, &request, test_result);
    if (result.status == SOLAR_STATUS_OK) {
        result = reset_request_artifacts(project, &request);
    }
    if (result.status == SOLAR_STATUS_OK &&
        test->kind == SOLAR_TEST_GENERATED) {
        result = stage_generated_runtime_files(
            active_artifacts, &request
        );
    }
    test_result->source_resolution_duration_ns = elapsed_since(
        resolution_started
    );
    if (result.status == SOLAR_STATUS_OK) {
        if (test->driver != NULL && strcmp(test->driver, "cocotb") == 0) {
            char *module_path = NULL;

            result = solar_project_resolve_path(
                project, test->cocotb, &module_path
            );
            if (result.status == SOLAR_STATUS_OK) {
                result = solar_cocotb_run(
                    &request, module_path, &simulation_failure, &metrics
                );
            }
            free(module_path);
        } else {
            result = solar_backend_simulate_request(
                &request, &simulation_failure, &metrics
            );
        }
        if (result.status != SOLAR_STATUS_OK) {
            failure_kind = simulation_failure_kind(
                result, simulation_failure
            );
        }
    }
    if (result.status == SOLAR_STATUS_OK) {
        solar_progress_stage(
            progress_observer, SOLAR_PROGRESS_PUBLISH,
            "Publishing waveform", request.backend
        );
        publication_started = monotonic_nanoseconds();
        result = publish_waveform(
            project, profile, test, &request, test_result
        );
        if (result.status == SOLAR_STATUS_OK &&
            test->kind == SOLAR_TEST_GENERATED &&
            test_result->waveform_path != NULL) {
            SolarResult layout_result = solar_compiler_prepare_waveform_layout(
                project, active_artifacts, test->name,
                test_result->waveform_path,
                &test_result->waveform_layout_path
            );

            if (layout_result.status != SOLAR_STATUS_OK) {
                record_layout_warning(test_result, layout_result);
            }
        }
        test_result->publication_duration_ns = elapsed_since(
            publication_started
        );
    }
    {
        SolarResult output_result = load_test_output(
            request.run_stdout_log, &test_result->output
        );

        if (result.status == SOLAR_STATUS_OK &&
            output_result.status != SOLAR_STATUS_OK) {
            result = output_result;
        }
    }
    solar_simulation_request_free(&request);

finish:
    solar_compiler_result_free(&compiler_result);
    if (result.status != SOLAR_STATUS_OK &&
        failure_kind == SOLAR_TEST_FAILURE_NONE) {
        failure_kind = generic_failure_kind(result);
    }
    test_result->failure_kind = failure_kind;
    test_result->compile_duration_ns = metrics.compile_duration_ns;
    test_result->simulation_duration_ns = metrics.run_duration_ns;
    test_result->total_process_duration_ns = metrics.total_duration_ns;
    test_result->result = result;
    return result;
}

SolarResult solar_test_run_with_artifacts(
    const SolarProject *project,
    const char *test_name,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarTestResult *test_result
)
{
    return solar_test_run_with_artifacts_and_progress(
        project, test_name, profile_name, generated_artifacts, NULL,
        test_result
    );
}

SolarResult solar_test_run(
    const SolarProject *project,
    const char *test_name,
    const char *profile_name,
    SolarTestResult *test_result
)
{
    return solar_test_run_with_artifacts(
        project, test_name, profile_name, NULL, test_result
    );
}

static SolarResult summary_append(
    SolarTestSummary *summary,
    SolarTestResult *test_result
)
{
    SolarTestResult *results = realloc(
        summary->results, (summary->count + 1U) * sizeof(*results)
    );

    if (results == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not grow the test summary",
            "free memory and try again"
        );
    }
    summary->results = results;
    summary->results[summary->count++] = *test_result;
    solar_test_result_init(test_result);
    return solar_result_ok();
}

SolarResult solar_test_run_all_with_artifacts_and_progress(
    const SolarProject *project,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    const SolarProgressObserver *progress_observer,
    SolarTestSummary *summary
)
{
    const SolarProfile *profile = NULL;
    size_t index;
    SolarResult result;

    if (summary != NULL) solar_test_summary_init(summary);
    if (project == NULL || summary == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot run tests without a project and summary storage",
            "load a project before running all tests"
        );
    }
    result = solar_project_validate(project);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_config_select_profile(
        &project->config, profile_name, &profile
    );
    if (result.status != SOLAR_STATUS_OK) return result;

    for (index = 0U; index < project->config.test_count; index++) {
        SolarTestResult test_result;

        solar_test_result_init(&test_result);
        solar_progress_item(
            progress_observer, index, project->config.test_count,
            project->config.tests[index].name
        );
        (void)solar_test_run_with_artifacts_and_progress(
            project,
            project->config.tests[index].name,
            profile == NULL ? NULL : profile->name,
            generated_artifacts,
            progress_observer,
            &test_result
        );
        if (test_result.result.status == SOLAR_STATUS_OK) summary->passed++;
        else summary->failed++;
        result = summary_append(summary, &test_result);
        solar_test_result_free(&test_result);
        if (result.status != SOLAR_STATUS_OK) {
            solar_test_summary_free(summary);
            return result;
        }
    }
    return solar_result_ok();
}

SolarResult solar_test_run_all_with_artifacts(
    const SolarProject *project,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarTestSummary *summary
)
{
    return solar_test_run_all_with_artifacts_and_progress(
        project, profile_name, generated_artifacts, NULL, summary
    );
}

SolarResult solar_test_run_all(
    const SolarProject *project,
    const char *profile_name,
    SolarTestSummary *summary
)
{
    return solar_test_run_all_with_artifacts(
        project, profile_name, NULL, summary
    );
}
