#include "compiler_backend_internal.h"

#include "backend_internal.h"
#include "solar/artifact.h"
#include "solar/filesystem.h"
#include "solar/runner.h"
#include "solar/solar.h"
#include "solar/yanc.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SOLAR_YANC_COPY_BUFFER_SIZE 16384U
#define SOLAR_YANC_FIXED_PATH_CAPACITY 1024U

typedef struct {
    char *root_relative;
    char *workspace_relative;
    char *publish_relative;
    char *final_relative;
    char *logs_relative;
    char *workspace;
    char *staged_project;
    char *software;
    char *hardware;
    char *simulation;
    char *temporary;
    char *publish;
    char *final;
    char *logs;
} YancWorkspace;

static SolarResult yanc_error(
    SolarStatus status,
    const char *hint,
    const char *format,
    ...
)
{
    SolarResult result;
    va_list arguments;

    result.status = status;
    result.diagnostic.severity = SOLAR_DIAGNOSTIC_ERROR;
    va_start(arguments, format);
    (void)vsnprintf(
        result.diagnostic.message,
        sizeof(result.diagnostic.message),
        format,
        arguments
    );
    va_end(arguments);
    (void)snprintf(
        result.diagnostic.hint,
        sizeof(result.diagnostic.hint),
        "%s",
        hint == NULL ? "" : hint
    );
    return result;
}

static SolarResult copy_owned_string(char **destination, const char *source)
{
    if (source == NULL) return solar_result_ok();
    *destination = strdup(source);
    if (*destination == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not copy YANC build metadata",
            "free memory and try again"
        );
    }
    return solar_result_ok();
}

static SolarResult join_path(
    const char *left,
    const char *right,
    char **output
)
{
    return solar_filesystem_join(left, right, output);
}

static void yanc_workspace_free(YancWorkspace *workspace)
{
    if (workspace == NULL) return;
    free(workspace->root_relative);
    free(workspace->workspace_relative);
    free(workspace->publish_relative);
    free(workspace->final_relative);
    free(workspace->logs_relative);
    free(workspace->workspace);
    free(workspace->staged_project);
    free(workspace->software);
    free(workspace->hardware);
    free(workspace->simulation);
    free(workspace->temporary);
    free(workspace->publish);
    free(workspace->final);
    free(workspace->logs);
    (void)memset(workspace, 0, sizeof(*workspace));
}

static SolarResult make_workspace_relative_paths(
    const SolarProject *project,
    YancWorkspace *workspace
)
{
    const char *processor = project->config.compiler.processor;
    SolarResult result = join_path(
        ".solar/tmp/yanc", processor, &workspace->root_relative
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(
        workspace->root_relative, "workspace", &workspace->workspace_relative
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(
        workspace->root_relative, "publish", &workspace->publish_relative
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return join_path(
        ".solar/logs/yanc", processor, &workspace->logs_relative
    );
}

static SolarResult resolve_workspace_paths(
    const SolarProject *project,
    YancWorkspace *workspace
)
{
    SolarResult result = solar_project_resolve_path(
        project, workspace->workspace_relative, &workspace->workspace
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, workspace->publish_relative, &workspace->publish
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_owned_string(&workspace->final, project->root);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, workspace->logs_relative, &workspace->logs
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(
        workspace->workspace,
        project->config.compiler.processor,
        &workspace->staged_project
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(workspace->staged_project, "Software", &workspace->software);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(workspace->staged_project, "Hardware", &workspace->hardware);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(
        workspace->staged_project, "Simulation", &workspace->simulation
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return join_path(workspace->workspace, "Temp", &workspace->temporary);
}

static SolarResult prepare_relative_directory(
    const char *project_root,
    const char *parent,
    const char *child
)
{
    char *relative = NULL;
    SolarResult result = join_path(parent, child, &relative);

    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_prepare_generated_directory(
            project_root, relative
        );
    }
    free(relative);
    return result;
}

static SolarResult create_workspace_directories(
    const SolarProject *project,
    const YancWorkspace *workspace
)
{
    char *staged_relative = NULL;
    SolarResult result = join_path(
        workspace->workspace_relative,
        project->config.compiler.processor,
        &staged_relative
    );

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_relative_directory(project->root, staged_relative, "Software");
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_relative_directory(project->root, staged_relative, "Hardware");
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_relative_directory(project->root, staged_relative, "Simulation");
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_relative_directory(
        project->root, workspace->workspace_relative, "Temp"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_relative_directory(
        project->root, workspace->publish_relative, "software"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_relative_directory(
        project->root, workspace->publish_relative, "hardware"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_relative_directory(
        project->root, workspace->publish_relative, "simulation"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_result_ok();

cleanup:
    free(staged_relative);
    return result;
}

static SolarResult yanc_workspace_prepare(
    const SolarProject *project,
    YancWorkspace *workspace
)
{
    bool removed = false;
    SolarResult result;

    (void)memset(workspace, 0, sizeof(*workspace));
    result = solar_filesystem_prepare_layout(project->root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, ".solar/tmp/yanc"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, ".solar/logs/yanc"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_workspace_relative_paths(project, workspace);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_remove_generated_tree(
        project->root, workspace->root_relative, &removed
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, workspace->workspace_relative
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, workspace->publish_relative
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, workspace->logs_relative
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = create_workspace_directories(project, workspace);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_workspace_paths(project, workspace);

cleanup:
    if (result.status != SOLAR_STATUS_OK) yanc_workspace_free(workspace);
    return result;
}

/*
 * YANC 5.2 retains 1024-byte path arrays in its CMM and Assembly frontends.
 * Solar rejects an unsupported native invocation before those tools see it,
 * leaving room for the suffixes the frontends append internally.
 */
static SolarResult validate_native_path_limits(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    const char *source_name
)
{
    const char *direct_paths[] = {
        workspace->staged_project,
        workspace->temporary,
        toolchain->hdl_directory,
        toolchain->macros_directory
    };
    size_t index;
    size_t project_length = strlen(workspace->staged_project);
    size_t processor_length = strlen(project->config.compiler.processor);

    for (index = 0U;
         index < sizeof(direct_paths) / sizeof(direct_paths[0]);
         index++) {
        if (direct_paths[index] == NULL ||
            strlen(direct_paths[index]) >= SOLAR_YANC_FIXED_PATH_CAPACITY) {
            return yanc_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "move the project or YANC installation to a shorter path",
                "path is too long for the fixed path buffers in bundled YANC 5.2"
            );
        }
    }
    if (project_length + strlen("/Software/") + strlen(source_name) + 1U >
            SOLAR_YANC_FIXED_PATH_CAPACITY ||
        project_length + strlen("/Software/") + processor_length +
                strlen(".asm") + 1U > SOLAR_YANC_FIXED_PATH_CAPACITY) {
        return yanc_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "move the project to a shorter path or use a shorter source name",
            "staged source path is too long for bundled YANC 5.2"
        );
    }
    return solar_result_ok();
}

static SolarResult copy_regular_file(const char *source, const char *destination)
{
    unsigned char buffer[SOLAR_YANC_COPY_BUFFER_SIZE];
    struct stat information;
    int input = -1;
    int output = -1;
    SolarResult result = solar_result_ok();

    input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0 || fstat(input, &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the source is a readable regular file",
            "cannot read YANC input %s: %s", source, strerror(errno)
        );
        goto cleanup;
    }
    output = open(
        destination,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    if (output < 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the generated directory is writable and not a symlink",
            "cannot create staged file %s: %s", destination, strerror(errno)
        );
        goto cleanup;
    }
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        size_t offset = 0U;

        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            result = yanc_error(
                SOLAR_STATUS_IO_ERROR,
                "check the source file and try again",
                "cannot read %s: %s", source, strerror(errno)
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
                result = yanc_error(
                    SOLAR_STATUS_IO_ERROR,
                    "check available storage and try again",
                    "cannot write %s: %s", destination, strerror(errno)
                );
                goto cleanup;
            }
            offset += (size_t)written;
        }
    }

cleanup:
    if (output >= 0 && close(output) != 0 && result.status == SOLAR_STATUS_OK) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot finish writing %s: %s", destination, strerror(errno)
        );
    }
    if (input >= 0) (void)close(input);
    return result;
}

static const char *path_basename(const char *path)
{
    const char *separator = strrchr(path, '/');

    return separator == NULL ? path : separator + 1;
}

static SolarResult stage_source(
    const SolarProject *project,
    const YancWorkspace *workspace,
    char **source_path_out,
    char **source_name_out
)
{
    char *original = NULL;
    char *destination = NULL;
    char *name = NULL;
    const char *configured_name;
    SolarResult result;

    *source_path_out = NULL;
    *source_name_out = NULL;
    result = solar_project_resolve_path(
        project, project->config.compiler.source, &original
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    configured_name = (
        strcmp(project->config.project.language, "asm") == 0 ||
        strcmp(project->config.project.language, "cmm") == 0
    ) ? project->config.compiler.processor
      : path_basename(project->config.compiler.source);
    if (strcmp(project->config.project.language, "asm") == 0 ||
        strcmp(project->config.project.language, "cmm") == 0) {
        const char *suffix = strcmp(
            project->config.project.language, "asm"
        ) == 0 ? ".asm" : ".cmm";
        size_t length = strlen(configured_name) + strlen(suffix) + 1U;

        name = malloc(length);
        if (name != NULL) {
            (void)snprintf(name, length, "%s%s", configured_name, suffix);
        }
    } else {
        name = strdup(configured_name);
    }
    if (name == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate staged source name",
            "free memory and try again"
        );
        goto cleanup;
    }
    result = join_path(workspace->software, name, &destination);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_regular_file(original, destination);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    *source_path_out = destination;
    destination = NULL;
    *source_name_out = name;
    name = NULL;

cleanup:
    free(name);
    free(destination);
    free(original);
    return result;
}

static SolarResult make_stage_log_path(
    const YancWorkspace *workspace,
    const char *stage,
    const char *suffix,
    char **path_out
)
{
    size_t name_length = strlen(stage) + strlen(suffix) + 2U;
    char *name = malloc(name_length);
    SolarResult result;

    *path_out = NULL;
    if (name == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate YANC log path",
            "free memory and try again"
        );
    }
    (void)snprintf(name, name_length, "%s.%s", stage, suffix);
    result = join_path(workspace->logs, name, path_out);
    free(name);
    return result;
}

static SolarResult run_yanc_stage(
    SolarCompilerResult *compiler_result,
    const YancWorkspace *workspace,
    const char *stage_name,
    const char *executable,
    const char *const *arguments
)
{
    char *stdout_log = NULL;
    char *stderr_log = NULL;
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    uint64_t duration_ns = 0U;
    SolarResult result;
    SolarResult record_result;

    result = make_stage_log_path(workspace, stage_name, "stdout.log", &stdout_log);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = make_stage_log_path(workspace, stage_name, "stderr.log", &stderr_log);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    specification.executable = executable;
    specification.argv = arguments;
    specification.working_directory = workspace->workspace;
    specification.stdout_log_path = stdout_log;
    specification.stderr_log_path = stderr_log;
    specification.progress_observer = compiler_result->progress_observer;
    solar_progress_stage(
        compiler_result->progress_observer,
        SOLAR_PROGRESS_GENERATE,
        stage_name,
        "YANC"
    );
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    duration_ns = process.duration_ns;
    if (result.status != SOLAR_STATUS_OK) {
        SolarResult contextual;

        if (process.outcome == SOLAR_PROCESS_EXITED) {
            contextual = yanc_error(
                result.status,
                "inspect the stage stderr log; later stages were not run",
                "YANC stage \"%s\" failed with exit code %d",
                stage_name,
                process.exit_code
            );
        } else if (process.outcome == SOLAR_PROCESS_SIGNALED) {
            contextual = yanc_error(
                result.status,
                "inspect the stage log and toolchain compatibility",
                "YANC stage \"%s\" terminated by signal %d",
                stage_name,
                process.term_signal
            );
        } else {
            contextual = yanc_error(
                result.status,
                "verify the executable and inspect the stage stderr log",
                "YANC stage \"%s\" failed before execution completed",
                stage_name
            );
        }

        solar_backend_add_log_context(&contextual, stage_name, stderr_log);
        result = contextual;
    }
    solar_process_result_free(&process);
    record_result = solar_compiler_record_stage(
        compiler_result, stage_name, executable, stderr_log, duration_ns, result
    );
    if (record_result.status != SOLAR_STATUS_OK) result = record_result;

cleanup:
    free(stderr_log);
    free(stdout_log);
    return result;
}

static SolarResult require_tool_path(const char *path, const char *name)
{
    struct stat information;

    if (path != NULL && stat(path, &information) == 0 &&
        S_ISREG(information.st_mode) && access(path, X_OK) == 0) {
        return solar_result_ok();
    }
    return yanc_error(
        SOLAR_STATUS_TOOL_MISSING,
        "install a complete YANC toolchain and set SOLAR_YANC_ROOT",
        "required YANC executable is unavailable: %s",
        name
    );
}

static SolarResult validate_required_toolchain(
    const SolarProject *project,
    const SolarYancToolchain *toolchain
)
{
    const char *language = project->config.project.language;
    SolarResult result;

    result = require_tool_path(toolchain->appcomp, "appcomp");
    if (result.status != SOLAR_STATUS_OK) return result;
    result = require_tool_path(toolchain->asmcomp, "asmcomp");
    if (result.status != SOLAR_STATUS_OK) return result;
    if (strcmp(language, "cmm") == 0) {
        result = require_tool_path(toolchain->cmmcomp, "cmmcomp");
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    if (strcmp(language, "cpp") == 0) {
        result = require_tool_path(toolchain->cpppp, "cpppp");
        if (result.status != SOLAR_STATUS_OK) return result;
        result = require_tool_path(toolchain->cppcomp, "cppcomp");
        if (result.status != SOLAR_STATUS_OK) return result;
        if (toolchain->headers_directory == NULL) {
            return yanc_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "use a checkout or release package containing YANC C++ headers",
                "YANC toolchain layout has no C++ headers directory"
            );
        }
    }
    if (toolchain->hdl_directory == NULL || toolchain->macros_directory == NULL) {
        return yanc_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "use a checkout or release package containing HDL and macros",
            "YANC toolchain layout is incomplete"
        );
    }
    return solar_result_ok();
}

static const char *diagnostic_argument(const SolarProject *project)
{
    return strcmp(project->config.yanc.diagnostics, "en") == 0 ? "-en" : "-pt";
}

static SolarResult run_cmm_frontend(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    const char *source_name,
    SolarCompilerResult *compiler_result
)
{
    const char *arguments[] = {
        toolchain->cmmcomp,
        diagnostic_argument(project),
        "-i", source_name,
        "-n", project->config.compiler.processor,
        "-p", workspace->staged_project,
        "-m", toolchain->macros_directory,
        "-t", workspace->temporary,
        NULL
    };

    return run_yanc_stage(
        compiler_result, workspace, "cmmcomp", toolchain->cmmcomp, arguments
    );
}

static SolarResult source_directory(
    const SolarProject *project,
    char **directory_out
)
{
    char *source = NULL;
    char *separator;
    SolarResult result = solar_project_resolve_path(
        project, project->config.compiler.source, &source
    );

    *directory_out = NULL;
    if (result.status != SOLAR_STATUS_OK) return result;
    separator = strrchr(source, '/');
    if (separator == NULL || separator == source) {
        free(source);
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not derive the compiler source directory",
            "report this Solar bug"
        );
    }
    *separator = '\0';
    *directory_out = source;
    return solar_result_ok();
}

static void free_cpppp_arguments(
    const SolarProject *project,
    const char **arguments
)
{
    size_t first_project_include = 9U;
    size_t index;

    if (arguments == NULL) return;
    for (index = 0U; index < project->config.compiler.include_dirs.count; index++) {
        free((char *)arguments[first_project_include + (index * 2U) + 1U]);
    }
    free(arguments);
}

static SolarResult build_cpppp_arguments(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const char *source_path,
    const char *preprocessed_path,
    const char *source_dir,
    const char ***arguments_out
)
{
    size_t include_count = project->config.compiler.include_dirs.count;
    size_t count = 9U + (include_count * 2U);
    const char **arguments = calloc(count + 1U, sizeof(*arguments));
    size_t output_index = 0U;
    size_t index;

    *arguments_out = NULL;
    if (arguments == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate cpppp arguments",
            "free memory and try again"
        );
    }
    arguments[output_index++] = toolchain->cpppp;
    arguments[output_index++] = "-i";
    arguments[output_index++] = source_path;
    arguments[output_index++] = "-o";
    arguments[output_index++] = preprocessed_path;
    arguments[output_index++] = "-I";
    arguments[output_index++] = toolchain->headers_directory;
    arguments[output_index++] = "-I";
    arguments[output_index++] = source_dir;
    for (index = 0U; index < include_count; index++) {
        char *resolved = NULL;
        SolarResult result = solar_project_resolve_path(
            project,
            project->config.compiler.include_dirs.items[index],
            &resolved
        );

        if (result.status != SOLAR_STATUS_OK) {
            free_cpppp_arguments(project, arguments);
            return result;
        }
        arguments[output_index++] = "-I";
        arguments[output_index++] = resolved;
    }
    *arguments_out = arguments;
    return solar_result_ok();
}

static SolarResult run_cpp_frontend(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    const char *source_path,
    SolarCompilerResult *compiler_result
)
{
    char *preprocessed = NULL;
    char *source_dir = NULL;
    const char **cpppp_arguments = NULL;
    SolarResult result = join_path(
        workspace->temporary, "preprocessed.cpp", &preprocessed
    );

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = source_directory(project, &source_dir);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = build_cpppp_arguments(
        project, toolchain, source_path, preprocessed, source_dir,
        &cpppp_arguments
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = run_yanc_stage(
        compiler_result, workspace, "cpppp", toolchain->cpppp,
        cpppp_arguments
    );
    if (result.status == SOLAR_STATUS_OK) {
        const char *cppcomp_arguments[] = {
            toolchain->cppcomp,
            "-i", preprocessed,
            "-n", project->config.compiler.processor,
            "-p", workspace->staged_project,
            "-t", workspace->temporary,
            NULL
        };

        result = run_yanc_stage(
            compiler_result, workspace, "cppcomp", toolchain->cppcomp,
            cppcomp_arguments
        );
    }

cleanup:
    free_cpppp_arguments(project, cpppp_arguments);
    free(source_dir);
    free(preprocessed);
    return result;
}

static SolarResult parse_app_instruction_count(
    const YancWorkspace *workspace,
    unsigned long *instruction_count_out
)
{
    char *app_log = NULL;
    FILE *file = NULL;
    int descriptor = -1;
    char key[128];
    char value[128];
    SolarResult result = join_path(
        workspace->temporary, "app_log.txt", &app_log
    );

    *instruction_count_out = 0UL;
    if (result.status != SOLAR_STATUS_OK) return result;
    descriptor = open(app_log, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        result = yanc_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect the appcomp stage log",
            "appcomp did not create required metadata %s: %s",
            app_log, strerror(errno)
        );
        goto cleanup;
    }
    file = fdopen(descriptor, "r");
    if (file == NULL) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available file descriptors and try again",
            "cannot read appcomp metadata %s: %s",
            app_log, strerror(errno)
        );
        goto cleanup;
    }
    descriptor = -1;
    while (fscanf(file, "%127s %127s", key, value) == 2) {
        if (strcmp(key, "n_ins") == 0) {
            char *end = NULL;
            unsigned long count;

            errno = 0;
            count = strtoul(value, &end, 10);
            if (errno == 0 && end != value && *end == '\0' && count > 0UL) {
                *instruction_count_out = count;
                break;
            }
        }
    }
    if (*instruction_count_out == 0UL) {
        result = yanc_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect appcomp metadata and toolchain compatibility",
            "appcomp metadata has no valid n_ins value: %s",
            app_log
        );
    }

cleanup:
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    free(app_log);
    return result;
}

/*
 * YANC 5.2 asmcomp assumes cmm_log.txt exists even for direct Assembly.
 * appcomp already computed the one value required for a source without
 * frontend variable metadata, so Solar publishes that value in staging only.
 */
static SolarResult write_assembly_compatibility_log(
    const YancWorkspace *workspace
)
{
    unsigned long instruction_count;
    char *path = NULL;
    int descriptor = -1;
    FILE *file = NULL;
    SolarResult result = parse_app_instruction_count(
        workspace, &instruction_count
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(workspace->temporary, "cmm_log.txt", &path);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    descriptor = open(
        path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644
    );
    if (descriptor < 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the YANC staging directory is writable and not a symlink",
            "cannot create Assembly compatibility metadata %s: %s",
            path, strerror(errno)
        );
        goto cleanup;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available file descriptors and try again",
            "cannot open Assembly compatibility metadata: %s",
            strerror(errno)
        );
        goto cleanup;
    }
    descriptor = -1;
    if (fprintf(file, "num_ins %lu\n", instruction_count) < 0 ||
        fflush(file) != 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot write Assembly compatibility metadata %s: %s",
            path, strerror(errno)
        );
    }

cleanup:
    if (file != NULL && fclose(file) != 0 &&
        result.status == SOLAR_STATUS_OK) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot finish Assembly compatibility metadata: %s",
            strerror(errno)
        );
    }
    if (descriptor >= 0) (void)close(descriptor);
    free(path);
    return result;
}

static SolarResult write_assembly_program_counter_map(
    const YancWorkspace *workspace
)
{
    unsigned long instruction_count;
    unsigned long index;
    const char *processor = path_basename(workspace->staged_project);
    size_t name_length = strlen(processor) + strlen("pc__mem.txt") + 1U;
    char *name = NULL;
    char *path = NULL;
    int descriptor = -1;
    FILE *file = NULL;
    SolarResult result = parse_app_instruction_count(
        workspace, &instruction_count
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    name = malloc(name_length);
    if (name == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate Assembly program-counter map name",
            "free memory and try again"
        );
    }
    (void)snprintf(name, name_length, "pc_%s_mem.txt", processor);
    result = join_path(workspace->temporary, name, &path);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    descriptor = open(
        path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644
    );
    if (descriptor < 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the YANC staging directory is writable and not a symlink",
            "cannot create Assembly program-counter map %s: %s",
            path, strerror(errno)
        );
        goto cleanup;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available file descriptors and try again",
            "cannot open Assembly program-counter map: %s",
            strerror(errno)
        );
        goto cleanup;
    }
    descriptor = -1;
    for (index = 0UL; index < instruction_count; index++) {
        if (fputs("00000000000000000000\n", file) == EOF) {
            result = yanc_error(
                SOLAR_STATUS_IO_ERROR,
                "check available storage and try again",
                "cannot write Assembly program-counter map %s: %s",
                path, strerror(errno)
            );
            break;
        }
    }
    if (result.status == SOLAR_STATUS_OK && fflush(file) != 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot flush Assembly program-counter map %s: %s",
            path, strerror(errno)
        );
    }

cleanup:
    if (file != NULL && fclose(file) != 0 &&
        result.status == SOLAR_STATUS_OK) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot finish Assembly program-counter map: %s",
            strerror(errno)
        );
    }
    if (descriptor >= 0) (void)close(descriptor);
    free(path);
    free(name);
    return result;
}

static SolarResult run_assembler_stages(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    const char *assembly_path,
    SolarCompilerResult *compiler_result
)
{
    char frequency[32];
    char clocks[32];
    const char *app_arguments[] = {
        toolchain->appcomp,
        diagnostic_argument(project),
        "-i", assembly_path,
        "-t", workspace->temporary,
        NULL
    };
    SolarResult result = run_yanc_stage(
        compiler_result, workspace, "appcomp", toolchain->appcomp,
        app_arguments
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    if (strcmp(project->config.project.language, "asm") == 0) {
        result = write_assembly_compatibility_log(workspace);
        if (result.status != SOLAR_STATUS_OK) return result;
        result = write_assembly_program_counter_map(workspace);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    (void)snprintf(
        frequency, sizeof(frequency), "%" PRIu64,
        project->config.compiler.frequency_mhz
    );
    (void)snprintf(
        clocks, sizeof(clocks), "%" PRIu64,
        project->config.compiler.simulation_clocks
    );
    {
        const char *asm_arguments[] = {
            toolchain->asmcomp,
            diagnostic_argument(project),
            "-i", assembly_path,
            "-p", workspace->staged_project,
            "-d", toolchain->hdl_directory,
            "-m", toolchain->macros_directory,
            "-t", workspace->temporary,
            "-f", frequency,
            "-c", clocks,
            NULL
        };

        return run_yanc_stage(
            compiler_result, workspace, "asmcomp", toolchain->asmcomp,
            asm_arguments
        );
    }
}

static SolarResult run_yanc_pipeline(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    const char *source_path,
    const char *source_name,
    SolarCompilerResult *compiler_result
)
{
    char *assembly_path = NULL;
    char *assembly_name = NULL;
    SolarResult result = solar_result_ok();

    if (strcmp(project->config.project.language, "cmm") == 0) {
        result = run_cmm_frontend(
            project, toolchain, workspace, source_name, compiler_result
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    } else if (strcmp(project->config.project.language, "cpp") == 0) {
        result = run_cpp_frontend(
            project, toolchain, workspace, source_path, compiler_result
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    if (strcmp(project->config.project.language, "asm") == 0) {
        assembly_path = strdup(source_path);
    } else {
        size_t length = strlen(project->config.compiler.processor) + 5U;

        assembly_name = malloc(length);
        if (assembly_name != NULL) {
            (void)snprintf(
                assembly_name, length, "%s.asm",
                project->config.compiler.processor
            );
            result = join_path(workspace->software, assembly_name, &assembly_path);
        } else {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate generated assembly path",
                "free memory and try again"
            );
        }
    }
    free(assembly_name);
    if (assembly_path == NULL) {
        if (result.status == SOLAR_STATUS_OK) {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not prepare generated assembly path",
                "free memory and try again"
            );
        }
        return result;
    }
    result = run_assembler_stages(
        project, toolchain, workspace, assembly_path, compiler_result
    );
    free(assembly_path);
    return result;
}

typedef struct {
    char *assembly;
    char *rtl;
    char *data_image;
    char *instruction_image;
    char *testbench;
    char *program_counter_map;
} YancNativeArtifacts;

static void native_artifacts_free(YancNativeArtifacts *artifacts)
{
    if (artifacts == NULL) return;
    free(artifacts->assembly);
    free(artifacts->rtl);
    free(artifacts->data_image);
    free(artifacts->instruction_image);
    free(artifacts->testbench);
    free(artifacts->program_counter_map);
    (void)memset(artifacts, 0, sizeof(*artifacts));
}

static SolarResult processor_file_name(
    const SolarProject *project,
    const char *suffix,
    char **name_out
)
{
    size_t length = strlen(project->config.compiler.processor) +
        strlen(suffix) + 1U;
    char *name = malloc(length);

    *name_out = NULL;
    if (name == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a generated artifact name",
            "free memory and try again"
        );
    }
    (void)snprintf(
        name, length, "%s%s", project->config.compiler.processor, suffix
    );
    *name_out = name;
    return solar_result_ok();
}

static SolarResult resolve_named_artifact(
    const SolarProject *project,
    const char *directory,
    const char *prefix,
    const char *suffix,
    char **path_out
)
{
    char *name = NULL;
    SolarResult result;

    if (prefix == NULL) {
        result = processor_file_name(project, suffix, &name);
    } else {
        size_t length = strlen(prefix) +
            strlen(project->config.compiler.processor) + strlen(suffix) + 1U;

        name = malloc(length);
        if (name == NULL) {
            return solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate a generated artifact name",
                "free memory and try again"
            );
        }
        (void)snprintf(
            name, length, "%s%s%s", prefix,
            project->config.compiler.processor, suffix
        );
        result = solar_result_ok();
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(directory, name, path_out);
    }
    free(name);
    return result;
}

static SolarResult resolve_native_artifacts(
    const SolarProject *project,
    const YancWorkspace *workspace,
    YancNativeArtifacts *artifacts
)
{
    SolarResult result;

    (void)memset(artifacts, 0, sizeof(*artifacts));
    result = resolve_named_artifact(
        project, workspace->software, NULL, ".asm", &artifacts->assembly
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_named_artifact(
        project, workspace->hardware, NULL, ".v", &artifacts->rtl
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_named_artifact(
        project, workspace->hardware, NULL, "_data.mif",
        &artifacts->data_image
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_named_artifact(
        project, workspace->hardware, NULL, "_inst.mif",
        &artifacts->instruction_image
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_named_artifact(
        project, workspace->temporary, NULL, "_tb.v", &artifacts->testbench
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return resolve_named_artifact(
        project, workspace->temporary, "pc_", "_mem.txt",
        &artifacts->program_counter_map
    );
}

static SolarResult validate_native_artifact(
    const char *path,
    const char *description,
    const char *stage
)
{
    struct stat information;

    if (path != NULL && stat(path, &information) == 0 &&
        S_ISREG(information.st_mode) && access(path, R_OK) == 0) {
        return solar_result_ok();
    }
    return yanc_error(
        SOLAR_STATUS_PROCESS_FAILED,
        "inspect the YANC stage log and verify the configured processor name",
        "YANC generated no %s\nexpected: %s\nstage: %s",
        description,
        path == NULL ? "(unknown)" : path,
        stage
    );
}

static SolarResult validate_native_artifacts(
    const YancNativeArtifacts *artifacts
)
{
    SolarResult result = validate_native_artifact(
        artifacts->assembly, "assembly artifact", "frontend"
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_native_artifact(
        artifacts->rtl, "processor Verilog", "asmcomp"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_native_artifact(
        artifacts->data_image, "data memory image", "asmcomp"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_native_artifact(
        artifacts->instruction_image, "instruction memory image", "asmcomp"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_native_artifact(
        artifacts->testbench, "generated testbench", "asmcomp"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return validate_native_artifact(
        artifacts->program_counter_map,
        "program-counter simulation map",
        "asmcomp"
    );
}

static SolarResult read_regular_text(const char *path, char **text_out)
{
    struct stat information;
    char *text = NULL;
    size_t offset = 0U;
    int descriptor = -1;
    SolarResult result = solar_result_ok();

    *text_out = NULL;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure generated text artifacts are readable regular files",
            "cannot read generated text %s: %s", path, strerror(errno)
        );
        goto cleanup;
    }
    if ((uintmax_t)information.st_size > (uintmax_t)(SIZE_MAX - 1U)) {
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "generated text artifact is too large to normalize",
            "reduce the generated design size or report this Solar limitation"
        );
        goto cleanup;
    }
    text = malloc((size_t)information.st_size + 1U);
    if (text == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate generated text",
            "free memory and try again"
        );
        goto cleanup;
    }
    while (offset < (size_t)information.st_size) {
        ssize_t count = read(
            descriptor, text + offset, (size_t)information.st_size - offset
        );

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            result = yanc_error(
                SOLAR_STATUS_IO_ERROR,
                "check the generated artifact and try again",
                "cannot finish reading %s: %s", path,
                count == 0 ? "unexpected end of file" : strerror(errno)
            );
            goto cleanup;
        }
        offset += (size_t)count;
    }
    text[offset] = '\0';
    *text_out = text;
    text = NULL;

cleanup:
    if (descriptor >= 0) (void)close(descriptor);
    free(text);
    return result;
}

static SolarResult replace_all_text(
    const char *input,
    const char *search,
    const char *replacement,
    char **output
)
{
    const char *cursor = input;
    size_t input_length = strlen(input);
    size_t search_length = strlen(search);
    size_t replacement_length = strlen(replacement);
    size_t match_count = 0U;
    size_t output_length;
    char *result;
    char *write_cursor;

    *output = NULL;
    if (search_length == 0U) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "cannot normalize an empty generated path",
            "report this Solar bug"
        );
    }
    while ((cursor = strstr(cursor, search)) != NULL) {
        match_count++;
        cursor += search_length;
    }
    if (replacement_length >= search_length) {
        size_t growth = replacement_length - search_length;

        if (growth != 0U &&
            match_count > (SIZE_MAX - input_length - 1U) / growth) {
            return solar_result_error(
                SOLAR_STATUS_IO_ERROR,
                "normalized generated artifact is too large",
                "use shorter project paths"
            );
        }
        output_length = input_length + (match_count * growth);
    } else {
        output_length = input_length -
            (match_count * (search_length - replacement_length));
    }
    result = malloc(output_length + 1U);
    if (result == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate normalized generated text",
            "free memory and try again"
        );
    }
    cursor = input;
    write_cursor = result;
    for (;;) {
        const char *match = strstr(cursor, search);
        size_t prefix_length;

        if (match == NULL) break;
        prefix_length = (size_t)(match - cursor);
        (void)memcpy(write_cursor, cursor, prefix_length);
        write_cursor += prefix_length;
        (void)memcpy(write_cursor, replacement, replacement_length);
        write_cursor += replacement_length;
        cursor = match + search_length;
    }
    (void)memcpy(write_cursor, cursor, strlen(cursor) + 1U);
    *output = result;
    return solar_result_ok();
}

static SolarResult write_text_safely(const char *path, const char *text)
{
    size_t length = strlen(text);
    size_t offset = 0U;
    int descriptor = open(
        path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644
    );
    SolarResult result = solar_result_ok();

    if (descriptor < 0) {
        return yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the publication directory is writable and not a symlink",
            "cannot create normalized artifact %s: %s", path, strerror(errno)
        );
    }
    while (offset < length) {
        ssize_t count = write(descriptor, text + offset, length - offset);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            result = yanc_error(
                SOLAR_STATUS_IO_ERROR,
                "check available storage and try again",
                "cannot write normalized artifact %s: %s", path, strerror(errno)
            );
            break;
        }
        offset += (size_t)count;
    }
    if (close(descriptor) != 0 && result.status == SOLAR_STATUS_OK) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot finish normalized artifact %s: %s", path, strerror(errno)
        );
    }
    return result;
}

static SolarResult normalized_text_copy(
    const YancWorkspace *workspace,
    const char *source,
    const char *destination
)
{
    static const char *const DIRECTORIES[] = {
        "Hardware", "Simulation", "Software"
    };
    static const char *const NORMALIZED[] = {
        "hardware", "simulation", "software"
    };
    char *current = NULL;
    size_t index;
    SolarResult result = read_regular_text(source, &current);

    if (result.status != SOLAR_STATUS_OK) return result;
    for (index = 0U;
         index < sizeof(DIRECTORIES) / sizeof(DIRECTORIES[0]);
         index++) {
        char *staged_prefix = NULL;
        char *final_prefix = NULL;
        char *replaced = NULL;

        result = join_path(
            workspace->staged_project, DIRECTORIES[index], &staged_prefix
        );
        if (result.status == SOLAR_STATUS_OK) {
            if (strcmp(DIRECTORIES[index], "Simulation") == 0) {
                /*
                 * Generated testbench I/O must follow the runner working
                 * directory. Solar runs it below .solar/tmp, so tool-created
                 * sidecars cannot leak into the public simulation directory.
                 */
                final_prefix = strdup(".");
                if (final_prefix == NULL) {
                    result = solar_result_error(
                        SOLAR_STATUS_INTERNAL_ERROR,
                        "could not normalize YANC simulation paths",
                        "free memory and try again"
                    );
                }
            } else {
                result = join_path(
                    workspace->final, NORMALIZED[index], &final_prefix
                );
            }
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = replace_all_text(
                current, staged_prefix, final_prefix, &replaced
            );
        }
        free(final_prefix);
        free(staged_prefix);
        if (result.status != SOLAR_STATUS_OK) {
            free(current);
            return result;
        }
        free(current);
        current = replaced;
    }
    {
        char *replaced = NULL;

        result = replace_all_text(
            current, workspace->staged_project, workspace->final, &replaced
        );
        if (result.status == SOLAR_STATUS_OK) {
            free(current);
            current = replaced;
            result = write_text_safely(destination, current);
        }
    }
    free(current);
    return result;
}

static SolarResult text_copy_with_replacement(
    const char *source,
    const char *destination,
    const char *search,
    const char *replacement
)
{
    char *text = NULL;
    char *replaced = NULL;
    SolarResult result = read_regular_text(source, &text);

    if (result.status == SOLAR_STATUS_OK) {
        result = replace_all_text(text, search, replacement, &replaced);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = write_text_safely(destination, replaced);
    }
    free(replaced);
    free(text);
    return result;
}

static SolarResult adapt_reserved_processor_name(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    const char *published_rtl
)
{
    char *rewritten_rtl = NULL;
    char *source_core = NULL;
    char *hardware = NULL;
    char *published_core = NULL;
    SolarResult result;

    if (strcmp(project->config.compiler.processor, "processor") != 0) {
        return solar_result_ok();
    }
    result = read_regular_text(published_rtl, &rewritten_rtl);
    if (result.status == SOLAR_STATUS_OK) {
        char *replacement = NULL;

        result = replace_all_text(
            rewritten_rtl,
            "processor#(",
            "solar_yanc_processor_core#(",
            &replacement
        );
        if (result.status == SOLAR_STATUS_OK) {
            result = write_text_safely(published_rtl, replacement);
        }
        free(replacement);
    }
    free(rewritten_rtl);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(toolchain->hdl_directory, "processor.v", &source_core);
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(workspace->publish, "hardware", &hardware);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(
            hardware, "solar_yanc_processor_core.v", &published_core
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = text_copy_with_replacement(
            source_core,
            published_core,
            "module processor",
            "module solar_yanc_processor_core"
        );
    }
    free(published_core);
    free(hardware);
    free(source_core);
    return result;
}

static SolarResult publish_artifact_file(
    const char *publish_root,
    const char *subdirectory,
    const char *name,
    const char *source,
    char **destination_out
)
{
    char *directory = NULL;
    char *destination = NULL;
    SolarResult result = join_path(publish_root, subdirectory, &directory);

    *destination_out = NULL;
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(directory, name, &destination);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_regular_file(source, destination);
    }
    free(directory);
    if (result.status == SOLAR_STATUS_OK) {
        *destination_out = destination;
    } else {
        free(destination);
    }
    return result;
}

static SolarResult publish_native_files(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    const YancNativeArtifacts *native
)
{
    char *assembly_name = NULL;
    char *rtl_name = NULL;
    char *data_name = NULL;
    char *instruction_name = NULL;
    char *testbench_name = NULL;
    char *pc_name = NULL;
    char *destination = NULL;
    SolarResult result = processor_file_name(project, ".asm", &assembly_name);

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, ".v", &rtl_name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_data.mif", &data_name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_inst.mif", &instruction_name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_tb.v", &testbench_name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    {
        size_t length = strlen(project->config.compiler.processor) +
            strlen("pc__mem.txt") + 1U;

        pc_name = malloc(length);
        if (pc_name == NULL) {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate simulation sidecar name",
                "free memory and try again"
            );
            goto cleanup;
        }
        (void)snprintf(
            pc_name, length, "pc_%s_mem.txt",
            project->config.compiler.processor
        );
    }
    result = publish_artifact_file(
        workspace->publish, "software", assembly_name, native->assembly,
        &destination
    );
    free(destination);
    destination = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    {
        char *directory = NULL;

        result = join_path(workspace->publish, "hardware", &directory);
        if (result.status == SOLAR_STATUS_OK) {
            result = join_path(directory, rtl_name, &destination);
        }
        free(directory);
        if (result.status == SOLAR_STATUS_OK) {
            result = normalized_text_copy(
                workspace, native->rtl, destination
            );
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = adapt_reserved_processor_name(
                project, toolchain, workspace, destination
            );
        }
        free(destination);
        destination = NULL;
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    result = publish_artifact_file(
        workspace->publish, "hardware", data_name, native->data_image,
        &destination
    );
    free(destination);
    destination = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = publish_artifact_file(
        workspace->publish, "hardware", instruction_name,
        native->instruction_image, &destination
    );
    free(destination);
    destination = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    {
        char *directory = NULL;

        result = join_path(workspace->publish, "simulation", &directory);
        if (result.status == SOLAR_STATUS_OK) {
            result = join_path(directory, testbench_name, &destination);
        }
        free(directory);
        if (result.status == SOLAR_STATUS_OK) {
            result = normalized_text_copy(
                workspace, native->testbench, destination
            );
        }
        free(destination);
        destination = NULL;
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    result = publish_artifact_file(
        workspace->publish, "simulation", pc_name,
        native->program_counter_map, &destination
    );

cleanup:
    free(destination);
    free(pc_name);
    free(testbench_name);
    free(instruction_name);
    free(data_name);
    free(rtl_name);
    free(assembly_name);
    return result;
}

static SolarResult write_build_metadata(
    const SolarProject *project,
    const SolarYancToolchain *toolchain
)
{
    char *metadata_directory = NULL;
    char *metadata_path = NULL;
    char timestamp[64] = "unknown";
    char frequency[32];
    char clocks[32];
    time_t now = time(NULL);
    struct tm utc_time;
    int descriptor = -1;
    FILE *file = NULL;
    char *metadata_relative = NULL;
    SolarResult result = join_path(
        ".solar/state/yanc", project->config.compiler.processor,
        &metadata_relative
    );

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, metadata_relative
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_project_resolve_path(
        project, metadata_relative, &metadata_directory
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(metadata_directory, "build-info.txt", &metadata_path);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (now != (time_t)-1 && gmtime_r(&now, &utc_time) != NULL) {
        (void)strftime(
            timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc_time
        );
    }
    (void)snprintf(
        frequency, sizeof(frequency), "%" PRIu64,
        project->config.compiler.frequency_mhz
    );
    (void)snprintf(
        clocks, sizeof(clocks), "%" PRIu64,
        project->config.compiler.simulation_clocks
    );
    descriptor = open(
        metadata_path,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    if (descriptor < 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the metadata directory is writable and not a symlink",
            "cannot create YANC build metadata %s: %s",
            metadata_path, strerror(errno)
        );
        goto cleanup;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available file descriptors and try again",
            "cannot open YANC metadata stream: %s", strerror(errno)
        );
        goto cleanup;
    }
    descriptor = -1;
    if (fprintf(
            file,
            "solar_version=" SOLAR_VERSION "\n"
            "yanc_version=%s\n"
            "yanc_root=%s\n"
            "language=%s\n"
            "source=%s\n"
            "processor=%s\n"
            "frequency_mhz=%s\n"
            "simulation_clocks=%s\n"
            "cmmcomp=%s\n"
            "cpppp=%s\n"
            "cppcomp=%s\n"
            "appcomp=%s\n"
            "asmcomp=%s\n"
            "timestamp=%s\n",
            toolchain->version == NULL ? "unknown" : toolchain->version,
            toolchain->root,
            project->config.project.language,
            project->config.compiler.source,
            project->config.compiler.processor,
            frequency,
            clocks,
            toolchain->cmmcomp == NULL ? "unavailable" : toolchain->cmmcomp,
            toolchain->cpppp == NULL ? "unavailable" : toolchain->cpppp,
            toolchain->cppcomp == NULL ? "unavailable" : toolchain->cppcomp,
            toolchain->appcomp,
            toolchain->asmcomp,
            timestamp
        ) < 0 || fflush(file) != 0) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot write YANC build metadata %s: %s",
            metadata_path, strerror(errno)
        );
    }

cleanup:
    if (file != NULL && fclose(file) != 0 &&
        result.status == SOLAR_STATUS_OK) {
        result = yanc_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot finish YANC build metadata: %s", strerror(errno)
        );
    }
    if (descriptor >= 0) (void)close(descriptor);
    free(metadata_path);
    free(metadata_directory);
    free(metadata_relative);
    return result;
}

static SolarResult append_hdl_sources(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    SolarGeneratedArtifacts *artifacts
)
{
    static const char *const HDL_FILES[] = {
        "addr_dec.v", "instr_dec.v", "processor.v", "core.v", "ula.v"
    };
    size_t index;

    for (index = 0U; index < sizeof(HDL_FILES) / sizeof(HDL_FILES[0]); index++) {
        char *path = NULL;
        char *validation_path = NULL;
        SolarResult result;

        if (strcmp(project->config.compiler.processor, "processor") == 0 &&
            strcmp(HDL_FILES[index], "processor.v") == 0) {
            char *final_hardware = NULL;
            char *publish_hardware = NULL;

            result = join_path(
                workspace->final, "hardware", &final_hardware
            );
            if (result.status == SOLAR_STATUS_OK) {
                result = join_path(
                    final_hardware, "solar_yanc_processor_core.v", &path
                );
            }
            if (result.status == SOLAR_STATUS_OK) {
                result = join_path(
                    workspace->publish, "hardware", &publish_hardware
                );
            }
            if (result.status == SOLAR_STATUS_OK) {
                result = join_path(
                    publish_hardware, "solar_yanc_processor_core.v",
                    &validation_path
                );
            }
            free(publish_hardware);
            free(final_hardware);
        } else {
            result = join_path(
                toolchain->hdl_directory, HDL_FILES[index], &path
            );
            if (result.status == SOLAR_STATUS_OK) {
                result = copy_owned_string(&validation_path, path);
            }
        }

        if (result.status == SOLAR_STATUS_OK) {
            result = validate_native_artifact(
                validation_path, "required YANC HDL module",
                "toolchain resolution"
            );
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = solar_string_list_append_copy(
                &artifacts->hdl_sources, path
            );
        }
        free(validation_path);
        free(path);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static SolarResult populate_generated_artifacts(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const YancWorkspace *workspace,
    SolarGeneratedArtifacts *artifacts
)
{
    char *software = NULL;
    char *hardware = NULL;
    char *simulation = NULL;
    char *metadata = NULL;
    char *name = NULL;
    SolarResult result;

    solar_generated_artifacts_init(artifacts);
    result = join_path(workspace->final, "hardware", &artifacts->build_directory);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (strcmp(project->config.project.language, "asm") == 0) {
        result = join_path(workspace->publish, "software", &software);
    } else {
        result = join_path(project->root, "software", &software);
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(workspace->final, "hardware", &hardware);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(workspace->final, "simulation", &simulation);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(
        project->root, ".solar/state/yanc", &metadata
    );
    if (result.status == SOLAR_STATUS_OK) {
        char *processor_metadata = NULL;

        result = join_path(
            metadata, project->config.compiler.processor, &processor_metadata
        );
        free(metadata);
        metadata = processor_metadata;
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    {
        char *internal_simulation = NULL;

        result = join_path(
            workspace->publish, "simulation", &internal_simulation
        );
        if (result.status == SOLAR_STATUS_OK) {
            result = copy_owned_string(
                &artifacts->working_directory, internal_simulation
            );
        }
        free(internal_simulation);
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, ".asm", &name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(software, name, &artifacts->assembly_path);
    free(name);
    name = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, ".v", &name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    {
        char *rtl = NULL;

        result = join_path(hardware, name, &rtl);
        if (result.status == SOLAR_STATUS_OK) {
            result = solar_string_list_append_copy(
                &artifacts->rtl_sources, rtl
            );
        }
        free(rtl);
    }
    free(name);
    name = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_data.mif", &name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(hardware, name, &artifacts->data_image_path);
    free(name);
    name = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_inst.mif", &name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(hardware, name, &artifacts->instruction_image_path);
    free(name);
    name = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_tb.v", &name);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(simulation, name, &artifacts->testbench_path);
    free(name);
    name = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_owned_string(
        &artifacts->processor_top, project->config.compiler.processor
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_tb", &artifacts->testbench_top);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(
        workspace->temporary, "trad_opcode.txt",
        &artifacts->instruction_translation_path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    {
        struct stat information;

        if (lstat(artifacts->instruction_translation_path, &information) != 0 ||
            !S_ISREG(information.st_mode) ||
            access(artifacts->instruction_translation_path, R_OK) != 0) {
            free(artifacts->instruction_translation_path);
            artifacts->instruction_translation_path = NULL;
        }
    }
    result = append_hdl_sources(project, toolchain, workspace, artifacts);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_string_list_append_copy(
        &artifacts->auxiliary_files, artifacts->data_image_path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_string_list_append_copy(
        &artifacts->auxiliary_files, artifacts->instruction_image_path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = processor_file_name(project, "_mem.txt", &name);
    if (result.status == SOLAR_STATUS_OK) {
        char *pc_name = NULL;
        size_t length = strlen(name) + 4U;

        pc_name = malloc(length);
        if (pc_name == NULL) {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate simulation sidecar path",
                "free memory and try again"
            );
        } else {
            (void)snprintf(pc_name, length, "pc_%s", name);
            {
                char *pc_path = NULL;

                char *internal_simulation = NULL;

                result = join_path(
                    workspace->publish, "simulation", &internal_simulation
                );
                if (result.status == SOLAR_STATUS_OK) {
                    result = join_path(internal_simulation, pc_name, &pc_path);
                }
                free(internal_simulation);
                if (result.status == SOLAR_STATUS_OK) {
                    result = solar_string_list_append_copy(
                        &artifacts->auxiliary_files, pc_path
                    );
                }
                free(pc_path);
            }
        }
        free(pc_name);
    }
    free(name);
    name = NULL;
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_path(metadata, "build-info.txt", &name);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_string_list_append_copy(
            &artifacts->auxiliary_files, name
        );
    }

cleanup:
    free(name);
    free(metadata);
    free(simulation);
    free(hardware);
    free(software);
    if (result.status != SOLAR_STATUS_OK) {
        solar_generated_artifacts_free(artifacts);
    }
    return result;
}

static SolarResult publish_public_yanc_artifacts(
    const SolarProject *project,
    const YancWorkspace *workspace
)
{
    static const struct {
        const char *subdirectory;
        const char *suffix;
        const char *public_directory;
        const char *type;
    } FILES[] = {
        {"software", ".asm", "software", "assembly"},
        {"hardware", ".v", "hardware", "rtl"},
        {"hardware", "_data.mif", "hardware", "data_image"},
        {"hardware", "_inst.mif", "hardware", "instruction_image"},
        {"simulation", "_tb.v", "simulation", "testbench"}
    };
    SolarArtifactPublication publications[6];
    char *sources[6] = {NULL};
    char *relative_paths[6] = {NULL};
    const char *types[6] = {NULL};
    size_t count = 0U;
    size_t index;
    SolarResult result = solar_result_ok();

    for (index = 0U; index < sizeof(FILES) / sizeof(FILES[0]); index++) {
        char *directory = NULL;
        char *name = NULL;

        if (strcmp(FILES[index].type, "assembly") == 0 &&
            strcmp(project->config.project.language, "asm") == 0) {
            continue;
        }
        result = join_path(
            workspace->publish, FILES[index].subdirectory, &directory
        );

        if (result.status == SOLAR_STATUS_OK) {
            result = processor_file_name(project, FILES[index].suffix, &name);
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = join_path(directory, name, &sources[count]);
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = join_path(
                FILES[index].public_directory, name, &relative_paths[count]
            );
        }
        if (result.status == SOLAR_STATUS_OK) types[count++] = FILES[index].type;
        free(name);
        free(directory);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    if (strcmp(project->config.compiler.processor, "processor") == 0) {
        char *directory = NULL;
        char *source = NULL;
        result = join_path(
            workspace->publish, "hardware", &directory
        );

        if (result.status == SOLAR_STATUS_OK) {
            result = join_path(
                directory, "solar_yanc_processor_core.v", &source
            );
        }
        if (result.status == SOLAR_STATUS_OK) {
            sources[count] = source;
            source = NULL;
            result = join_path(
                "hardware", "solar_yanc_processor_core.v",
                &relative_paths[count]
            );
        }
        if (result.status == SOLAR_STATUS_OK) types[count++] = "rtl_support";
        free(source);
        free(directory);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    for (index = 0U; index < count; index++) {
        publications[index] = (SolarArtifactPublication){
            sources[index], relative_paths[index], types[index], "generation",
            "generated", "yanc", project->config.project.default_profile
        };
    }
    result = solar_artifact_publish_files(
        project->root, publications, count
    );

cleanup:
    for (index = 0U; index < 6U; index++) {
        free(relative_paths[index]);
        free(sources[index]);
    }
    return result;
}

typedef struct {
    char *staging_relative;
    char *final_relative;
    char *staging_directory;
    char *layout_staging;
    char *translation_staging;
    char *layout_final;
    char *layout_final_relative;
    char *translation_final;
    char *translation_final_relative;
    char *stdout_log;
    char *stderr_log;
} YancWaveformLayout;

static void yanc_waveform_layout_free(YancWaveformLayout *layout)
{
    if (layout == NULL) return;
    free(layout->staging_relative);
    free(layout->final_relative);
    free(layout->staging_directory);
    free(layout->layout_staging);
    free(layout->translation_staging);
    free(layout->layout_final);
    free(layout->layout_final_relative);
    free(layout->translation_final);
    free(layout->translation_final_relative);
    free(layout->stdout_log);
    free(layout->stderr_log);
    (void)memset(layout, 0, sizeof(*layout));
}

static SolarResult make_yanc_waveform_layout_paths(
    const SolarProject *project,
    const char *test_name,
    YancWaveformLayout *layout
)
{
    char *test_temporary = NULL;
    char *test_logs = NULL;
    SolarResult result;

    (void)memset(layout, 0, sizeof(*layout));
    result = join_path(".solar/tmp/tests", test_name, &test_temporary);
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(
            test_temporary, "waveform-layout", &layout->staging_relative
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(
            ".solar/state/waveforms", test_name, &layout->final_relative
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(
            project->root, layout->staging_relative,
            &layout->staging_directory
        );
    }
    if (result.status == SOLAR_STATUS_OK) result = join_path(
        layout->staging_directory, "layout.gtkw", &layout->layout_staging
    );
    if (result.status == SOLAR_STATUS_OK) result = join_path(
        layout->staging_directory, "trad_opcode.txt",
        &layout->translation_staging
    );
    if (result.status == SOLAR_STATUS_OK) result = join_path(
        layout->final_relative, "layout.gtkw", &layout->layout_final_relative
    );
    if (result.status == SOLAR_STATUS_OK) result = join_path(
        project->root, layout->layout_final_relative, &layout->layout_final
    );
    if (result.status == SOLAR_STATUS_OK) result = join_path(
        layout->final_relative, "trad_opcode.txt",
        &layout->translation_final_relative
    );
    if (result.status == SOLAR_STATUS_OK) result = join_path(
        project->root, layout->translation_final_relative,
        &layout->translation_final
    );
    if (result.status == SOLAR_STATUS_OK) {
        result = join_path(".solar/logs/tests", test_name, &test_logs);
    }
    if (result.status == SOLAR_STATUS_OK) result = join_path(
        project->root, test_logs, &layout->stdout_log
    );
    if (result.status == SOLAR_STATUS_OK) {
        char *directory = layout->stdout_log;

        layout->stdout_log = NULL;
        result = join_path(directory, "gen_gtkw.stdout.log", &layout->stdout_log);
        if (result.status == SOLAR_STATUS_OK) result = join_path(
            directory, "gen_gtkw.stderr.log", &layout->stderr_log
        );
        free(directory);
    }
    free(test_logs);
    free(test_temporary);
    if (result.status != SOLAR_STATUS_OK) yanc_waveform_layout_free(layout);
    return result;
}

static SolarResult prepare_yanc_waveform_staging(
    const SolarProject *project,
    const SolarGeneratedArtifacts *artifacts,
    const char *test_name,
    const YancWaveformLayout *layout
)
{
    char *log_relative = NULL;
    bool removed = false;
    SolarResult result = solar_filesystem_remove_generated_tree(
        project->root, layout->staging_relative, &removed
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_filesystem_prepare_generated_directory(
        project->root, layout->staging_relative
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_filesystem_prepare_generated_directory(
        project->root, ".solar/state/waveforms"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = join_path(".solar/logs/tests", test_name, &log_relative);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_prepare_generated_directory(
            project->root, log_relative
        );
    }
    free(log_relative);
    if (result.status != SOLAR_STATUS_OK) return result;
    return copy_regular_file(
        artifacts->instruction_translation_path,
        layout->translation_staging
    );
}

static SolarResult waveform_relative_path(
    const SolarProject *project,
    const char *waveform_path,
    const char **relative_out
)
{
    size_t root_length = strlen(project->root);

    *relative_out = NULL;
    if (strncmp(waveform_path, project->root, root_length) != 0 ||
        waveform_path[root_length] != '/' ||
        !solar_filesystem_is_safe_relative(waveform_path + root_length + 1U)) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "YANC waveform is outside the active project",
            "format only a registered project waveform"
        );
    }
    *relative_out = waveform_path + root_length + 1U;
    return solar_result_ok();
}

static SolarResult run_yanc_waveform_formatter(
    const SolarProject *project,
    const SolarYancToolchain *toolchain,
    const char *waveform_path,
    const YancWaveformLayout *layout
)
{
    const char *waveform_relative = NULL;
    const char *arguments[] = {
        toolchain->gen_gtkw, "--assembly-only", NULL,
        layout->layout_staging, layout->translation_final_relative,
        layout->layout_final_relative, NULL
    };
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result = waveform_relative_path(
        project, waveform_path, &waveform_relative
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    arguments[2] = waveform_relative;
    specification.executable = toolchain->gen_gtkw;
    specification.argv = arguments;
    specification.working_directory = project->root;
    specification.stdout_log_path = layout->stdout_log;
    specification.stderr_log_path = layout->stderr_log;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    solar_process_result_free(&process);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(
            &result, "gen_gtkw", layout->stderr_log
        );
        return result;
    }
    return validate_native_artifact(
        layout->layout_staging, "GTKWave Assembly layout", "gen_gtkw"
    );
}

static SolarResult yanc_prepare_waveform_layout(
    const SolarProject *project,
    const SolarGeneratedArtifacts *artifacts,
    const char *test_name,
    const char *waveform_path,
    char **layout_path_out
)
{
    SolarYancToolchain toolchain;
    YancWaveformLayout layout;
    bool removed = false;
    SolarResult result;

    *layout_path_out = NULL;
    solar_yanc_toolchain_init(&toolchain);
    result = make_yanc_waveform_layout_paths(project, test_name, &layout);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (artifacts->instruction_translation_path == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_NOT_FOUND,
            "YANC produced no Assembly opcode translation table",
            "inspect the asmcomp log; the raw waveform remains available"
        );
        goto cleanup;
    }
    result = solar_yanc_resolve(project, &toolchain);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (toolchain.gen_gtkw == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_TOOL_MISSING,
            "YANC waveform formatter is unavailable: gen_gtkw",
            "use Solar's bundled YANC or an external root with gen_gtkw"
        );
        goto cleanup;
    }
    result = prepare_yanc_waveform_staging(
        project, artifacts, test_name, &layout
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = run_yanc_waveform_formatter(
        project, &toolchain, waveform_path, &layout
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_publish_generated_directory(
        project->root, layout.staging_relative, layout.final_relative
    );
    if (result.status == SOLAR_STATUS_OK) {
        *layout_path_out = layout.layout_final;
        layout.layout_final = NULL;
    }

cleanup:
    if (result.status != SOLAR_STATUS_OK && layout.final_relative != NULL) {
        (void)solar_filesystem_remove_generated_tree(
            project->root, layout.final_relative, &removed
        );
    }
    if (layout.staging_relative != NULL) {
        (void)solar_filesystem_remove_generated_tree(
            project->root, layout.staging_relative, &removed
        );
    }
    yanc_waveform_layout_free(&layout);
    solar_yanc_toolchain_free(&toolchain);
    return result;
}

static bool layout_is_current(
    const YancWaveformLayout *layout,
    const char *waveform_path
)
{
    struct stat layout_information;
    struct stat translation_information;
    struct stat waveform_information;

    if (lstat(layout->layout_final, &layout_information) != 0 ||
        !S_ISREG(layout_information.st_mode) ||
        access(layout->layout_final, R_OK) != 0 ||
        lstat(layout->translation_final, &translation_information) != 0 ||
        !S_ISREG(translation_information.st_mode) ||
        access(layout->translation_final, R_OK) != 0 ||
        stat(waveform_path, &waveform_information) != 0) {
        return false;
    }
    if (layout_information.st_mtim.tv_sec != waveform_information.st_mtim.tv_sec) {
        return layout_information.st_mtim.tv_sec >
            waveform_information.st_mtim.tv_sec;
    }
    return layout_information.st_mtim.tv_nsec >=
        waveform_information.st_mtim.tv_nsec;
}

static SolarResult yanc_find_waveform_layout(
    const SolarProject *project,
    const char *test_name,
    const char *waveform_path,
    char **layout_path_out
)
{
    YancWaveformLayout layout;
    SolarResult result;

    *layout_path_out = NULL;
    result = make_yanc_waveform_layout_paths(project, test_name, &layout);
    if (result.status == SOLAR_STATUS_OK && layout_is_current(
            &layout, waveform_path
        )) {
        *layout_path_out = layout.layout_final;
        layout.layout_final = NULL;
    }
    yanc_waveform_layout_free(&layout);
    return result;
}

static SolarResult yanc_compile(
    const SolarProject *project,
    SolarCompilerResult *compiler_result
)
{
    SolarYancToolchain toolchain;
    YancWorkspace workspace;
    YancNativeArtifacts native;
    char *source_path = NULL;
    char *source_name = NULL;
    SolarResult result;

    solar_yanc_toolchain_init(&toolchain);
    (void)memset(&workspace, 0, sizeof(workspace));
    (void)memset(&native, 0, sizeof(native));
    result = solar_yanc_resolve(project, &toolchain);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = validate_required_toolchain(project, &toolchain);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_owned_string(
        &compiler_result->toolchain_root, toolchain.root
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_owned_string(
        &compiler_result->toolchain_version, toolchain.version
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = yanc_workspace_prepare(project, &workspace);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = stage_source(
        project, &workspace, &source_path, &source_name
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = validate_native_path_limits(
        project, &toolchain, &workspace, source_name
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = run_yanc_pipeline(
        project, &toolchain, &workspace, source_path, source_name,
        compiler_result
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_native_artifacts(project, &workspace, &native);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = validate_native_artifacts(&native);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = publish_native_files(project, &toolchain, &workspace, &native);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = write_build_metadata(project, &toolchain);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    /*
     * Prepare the complete structured result while outputs are still private.
     * After publication there must be no fallible validation or allocation
     * that could report failure after replacing a previous valid artifact set.
     */
    result = populate_generated_artifacts(
        project, &toolchain, &workspace, &compiler_result->artifacts
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = publish_public_yanc_artifacts(project, &workspace);
    if (result.status != SOLAR_STATUS_OK) {
        solar_generated_artifacts_free(&compiler_result->artifacts);
        goto cleanup;
    }

cleanup:
    free(source_name);
    free(source_path);
    native_artifacts_free(&native);
    yanc_workspace_free(&workspace);
    solar_yanc_toolchain_free(&toolchain);
    return result;
}

const SolarCompilerBackend SOLAR_YANC_COMPILER_BACKEND = {
    .name = "yanc",
    .compile = yanc_compile,
    .prepare_waveform_layout = yanc_prepare_waveform_layout,
    .find_waveform_layout = yanc_find_waveform_layout
};
