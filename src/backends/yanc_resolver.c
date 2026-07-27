#include "solar/yanc.h"

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static SolarResult resolver_error(
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

void solar_yanc_toolchain_init(SolarYancToolchain *toolchain)
{
    if (toolchain != NULL) (void)memset(toolchain, 0, sizeof(*toolchain));
}

void solar_yanc_toolchain_free(SolarYancToolchain *toolchain)
{
    if (toolchain == NULL) return;
    free(toolchain->root);
    free(toolchain->bin_directory);
    free(toolchain->hdl_directory);
    free(toolchain->macros_directory);
    free(toolchain->headers_directory);
    free(toolchain->cmmcomp);
    free(toolchain->cpppp);
    free(toolchain->cppcomp);
    free(toolchain->appcomp);
    free(toolchain->asmcomp);
    free(toolchain->gen_gtkw);
    free(toolchain->version);
    solar_yanc_toolchain_init(toolchain);
}

static bool external_path_is_safe(const char *path)
{
    const unsigned char *character = (const unsigned char *)path;

    if (path == NULL || path[0] != '/') return false;
    while (*character != '\0') {
        if (*character < 32U || *character == 127U) return false;
        character++;
    }
    return true;
}

static char *canonical_directory_if_present(const char *path)
{
    struct stat information;
    char *resolved;

    if (path == NULL || stat(path, &information) != 0 ||
        !S_ISDIR(information.st_mode) || access(path, R_OK | X_OK) != 0) {
        return NULL;
    }
    resolved = realpath(path, NULL);
    if (resolved != NULL && !external_path_is_safe(resolved)) {
        free(resolved);
        return NULL;
    }
    return resolved;
}

static char *canonical_executable_if_present(const char *path)
{
    struct stat information;
    char *resolved;

    if (path == NULL || stat(path, &information) != 0 ||
        !S_ISREG(information.st_mode) || access(path, X_OK) != 0) {
        return NULL;
    }
    resolved = realpath(path, NULL);
    if (resolved != NULL && !external_path_is_safe(resolved)) {
        free(resolved);
        return NULL;
    }
    return resolved;
}

static SolarResult path_candidate(
    const char *directory,
    size_t directory_length,
    const char *name,
    char **path_out
)
{
    char *directory_copy;
    char *candidate = NULL;
    SolarResult result;

    *path_out = NULL;
    if (directory_length == SIZE_MAX) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "YANC executable search path is too long",
            "use a shorter PATH entry"
        );
    }
    if (directory_length == 0U) {
        directory_copy = strdup(".");
    } else {
        directory_copy = malloc(directory_length + 1U);
        if (directory_copy != NULL) {
            (void)memcpy(directory_copy, directory, directory_length);
            directory_copy[directory_length] = '\0';
        }
    }
    if (directory_copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate YANC executable search path",
            "free memory and try again"
        );
    }
    result = solar_filesystem_join(directory_copy, name, &candidate);
    free(directory_copy);
    if (result.status == SOLAR_STATUS_OK) {
        *path_out = canonical_executable_if_present(candidate);
    }
    free(candidate);
    return result;
}

static SolarResult find_on_path(const char *name, char **path_out)
{
    const char *environment = getenv("PATH");
    const char *component;

    *path_out = NULL;
    if (environment == NULL) return solar_result_ok();
    component = environment;
    for (;;) {
        const char *separator = strchr(component, ':');
        size_t length = separator == NULL
            ? strlen(component)
            : (size_t)(separator - component);
        SolarResult result = path_candidate(
            component, length, name, path_out
        );

        if (result.status != SOLAR_STATUS_OK || *path_out != NULL) {
            return result;
        }
        if (separator == NULL) break;
        component = separator + 1;
    }
    return solar_result_ok();
}

static SolarResult derive_root_from_path(char **root_out)
{
    static const char *const CANDIDATES[] = {
        "cmmcomp", "cpppp", "cppcomp", "appcomp", "asmcomp"
    };
    size_t index;

    *root_out = NULL;
    for (index = 0U;
         index < sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);
         index++) {
        char *executable = NULL;
        char *bin_directory;
        char *separator;
        char *root;
        SolarResult result = find_on_path(CANDIDATES[index], &executable);

        if (result.status != SOLAR_STATUS_OK) return result;
        if (executable == NULL) continue;
        bin_directory = strdup(executable);
        free(executable);
        if (bin_directory == NULL) {
            return solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate derived YANC root",
                "free memory and try again"
            );
        }
        separator = strrchr(bin_directory, '/');
        if (separator == NULL) {
            free(bin_directory);
            continue;
        }
        *separator = '\0';
        separator = strrchr(bin_directory, '/');
        if (separator == NULL || strcmp(separator + 1, "bin") != 0) {
            free(bin_directory);
            continue;
        }
        *separator = '\0';
        root = canonical_directory_if_present(bin_directory);
        free(bin_directory);
        if (root != NULL) {
            *root_out = root;
            return solar_result_ok();
        }
    }
    return solar_result_ok();
}

static char *self_executable_path(void)
{
    size_t capacity = 256U;

    for (;;) {
        char *path = malloc(capacity);
        ssize_t length;

        if (path == NULL) return NULL;
        length = readlink("/proc/self/exe", path, capacity - 1U);
        if (length < 0) {
            free(path);
            return NULL;
        }
        if ((size_t)length < capacity - 1U) {
            path[length] = '\0';
            return path;
        }
        free(path);
        if (capacity > SIZE_MAX / 2U) return NULL;
        capacity *= 2U;
    }
}

static SolarResult bundled_root(char **root_out)
{
    static const char *const RELATIVE_CANDIDATES[] = {
        "yanc-bundle",
        SOLAR_YANC_INSTALL_RELATIVE
    };
    char *executable = self_executable_path();
    char *separator;
    size_t index;

    *root_out = NULL;
    if (executable == NULL) return solar_result_ok();
    separator = strrchr(executable, '/');
    if (separator == NULL) {
        free(executable);
        return solar_result_ok();
    }
    *separator = '\0';
    for (index = 0U;
         index < sizeof(RELATIVE_CANDIDATES) /
             sizeof(RELATIVE_CANDIDATES[0]);
         index++) {
        char *candidate = NULL;
        SolarResult result = solar_filesystem_join(
            executable, RELATIVE_CANDIDATES[index], &candidate
        );

        if (result.status != SOLAR_STATUS_OK) {
            free(executable);
            return result;
        }
        *root_out = canonical_directory_if_present(candidate);
        free(candidate);
        if (*root_out != NULL) break;
    }
    free(executable);
    return solar_result_ok();
}

static SolarResult select_root(
    const SolarProject *project,
    char **root_out,
    bool *bundled_out
)
{
    const char *environment_root = getenv("SOLAR_YANC_ROOT");
    const char *configured_root = project == NULL
        ? NULL
        : project->config.yanc.root;
    const char *selected = NULL;
    char *resolved;
    SolarResult result;

    *root_out = NULL;
    *bundled_out = false;
    if (environment_root != NULL && environment_root[0] != '\0') {
        selected = environment_root;
    } else if (configured_root != NULL && configured_root[0] != '\0') {
        selected = configured_root;
    }
    if (selected == NULL) {
        result = bundled_root(root_out);
        if (result.status != SOLAR_STATUS_OK || *root_out != NULL) {
            *bundled_out = *root_out != NULL;
            return result;
        }
        return derive_root_from_path(root_out);
    }
    if (selected[0] != '/') {
        return resolver_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "use an absolute YANC root path",
            "YANC root is not absolute: %s",
            selected
        );
    }
    resolved = canonical_directory_if_present(selected);
    if (resolved == NULL) {
        return resolver_error(
            SOLAR_STATUS_TOOL_MISSING,
            "use the bundled YANC or set SOLAR_YANC_ROOT to a readable development checkout",
            "YANC root is unavailable or unreadable: %s",
            selected
        );
    }
    *root_out = resolved;
    return solar_result_ok();
}

static SolarResult join_optional_directory(
    const char *root,
    const char *relative,
    char **directory_out
)
{
    char *candidate = NULL;
    SolarResult result = solar_filesystem_join(root, relative, &candidate);

    *directory_out = NULL;
    if (result.status == SOLAR_STATUS_OK) {
        *directory_out = canonical_directory_if_present(candidate);
    }
    free(candidate);
    return result;
}

static SolarResult resolve_layout(SolarYancToolchain *toolchain)
{
    char *checkout_macros = NULL;
    char *checkout_headers = NULL;
    char *release_macros = NULL;
    char *release_headers = NULL;
    SolarResult result = join_optional_directory(
        toolchain->root, "bin", &toolchain->bin_directory
    );

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_optional_directory(
        toolchain->root, "HDL", &toolchain->hdl_directory
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_optional_directory(
        toolchain->root, "Compilers/CMMComp/Includes", &checkout_macros
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_optional_directory(
        toolchain->root, "Compilers/CPPComp/Includes", &checkout_headers
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_optional_directory(
        toolchain->root, "Macros", &release_macros
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = join_optional_directory(
        toolchain->root, "Header", &release_headers
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;

    if (release_macros != NULL || release_headers != NULL) {
        toolchain->layout = SOLAR_YANC_LAYOUT_RELEASE;
        toolchain->macros_directory = release_macros;
        toolchain->headers_directory = release_headers;
        release_macros = NULL;
        release_headers = NULL;
    } else if (checkout_macros != NULL || checkout_headers != NULL) {
        toolchain->layout = SOLAR_YANC_LAYOUT_CHECKOUT;
        toolchain->macros_directory = checkout_macros;
        toolchain->headers_directory = checkout_headers;
        checkout_macros = NULL;
        checkout_headers = NULL;
    }
    if (toolchain->bin_directory == NULL ||
        toolchain->hdl_directory == NULL ||
        toolchain->layout == SOLAR_YANC_LAYOUT_UNKNOWN) {
        result = resolver_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "use a YANC checkout or release package with bin, HDL and include data",
            "YANC toolchain layout is invalid: %s",
            toolchain->root
        );
    }

cleanup:
    free(release_headers);
    free(release_macros);
    free(checkout_headers);
    free(checkout_macros);
    return result;
}

static SolarResult resolve_tool(
    const SolarYancToolchain *toolchain,
    const char *name,
    char **tool_out
)
{
    char *candidate = NULL;
    SolarResult result = solar_filesystem_join(
        toolchain->bin_directory, name, &candidate
    );

    *tool_out = NULL;
    if (result.status == SOLAR_STATUS_OK) {
        *tool_out = canonical_executable_if_present(candidate);
    }
    free(candidate);
    return result;
}

static SolarResult resolve_tools(SolarYancToolchain *toolchain)
{
    SolarResult result = resolve_tool(
        toolchain, "cmmcomp", &toolchain->cmmcomp
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_tool(toolchain, "cpppp", &toolchain->cpppp);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_tool(toolchain, "cppcomp", &toolchain->cppcomp);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_tool(toolchain, "appcomp", &toolchain->appcomp);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = resolve_tool(toolchain, "asmcomp", &toolchain->asmcomp);
    if (result.status != SOLAR_STATUS_OK) return result;
    return resolve_tool(toolchain, "gen_gtkw", &toolchain->gen_gtkw);
}

static char *first_output_line(const SolarProcessResult *process)
{
    const char *source = process->stdout_text;
    size_t length;
    char *line;

    if (source == NULL || source[0] == '\0') source = process->stderr_text;
    if (source == NULL || source[0] == '\0') return NULL;
    while (*source == '\n' || *source == '\r' ||
           *source == ' ' || *source == '\t') {
        source++;
    }
    length = strcspn(source, "\r\n");
    line = malloc(length + 1U);
    if (line == NULL) return NULL;
    (void)memcpy(line, source, length);
    line[length] = '\0';
    return line;
}

static void probe_version(SolarYancToolchain *toolchain)
{
    const char *executable = toolchain->cmmcomp != NULL
        ? toolchain->cmmcomp
        : (toolchain->asmcomp != NULL ? toolchain->asmcomp : toolchain->appcomp);
    const char *arguments[] = {executable, "--version", NULL};
    SolarProcessSpec specification = {0};
    SolarProcessResult process;

    if (executable == NULL) return;
    specification.executable = executable;
    specification.argv = arguments;
    specification.working_directory = NULL;
    specification.stdout_log_path = NULL;
    specification.stderr_log_path = NULL;
    solar_process_result_init(&process);
    (void)solar_runner_run(&specification, &process);
    toolchain->version = first_output_line(&process);
    solar_process_result_free(&process);
}

SolarResult solar_yanc_resolve(
    const SolarProject *project,
    SolarYancToolchain *toolchain
)
{
    SolarResult result;

    if (toolchain != NULL) solar_yanc_toolchain_init(toolchain);
    if (toolchain == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot resolve YANC without result storage",
            "provide a SolarYancToolchain output value"
        );
    }
    result = select_root(project, &toolchain->root, &toolchain->bundled);
    if (result.status != SOLAR_STATUS_OK) goto failure;
    if (toolchain->root == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_TOOL_MISSING,
            "YANC toolchain was not found",
            "rebuild Solar with its bundled YANC sources"
        );
        goto failure;
    }
    result = resolve_layout(toolchain);
    if (result.status != SOLAR_STATUS_OK) goto failure;
    result = resolve_tools(toolchain);
    if (result.status != SOLAR_STATUS_OK) goto failure;
    probe_version(toolchain);
    return solar_result_ok();

failure:
    solar_yanc_toolchain_free(toolchain);
    return result;
}
