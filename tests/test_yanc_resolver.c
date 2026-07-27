#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/project.h"
#include "solar/yanc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    TOOL_CMMCOMP = 1U << 0,
    TOOL_CPPPP = 1U << 1,
    TOOL_CPPCOMP = 1U << 2,
    TOOL_APPCOMP = 1U << 3,
    TOOL_ASMCOMP = 1U << 4,
    TOOL_GEN_GTKW = 1U << 5,
    TOOL_ALL = TOOL_CMMCOMP | TOOL_CPPPP | TOOL_CPPCOMP |
               TOOL_APPCOMP | TOOL_ASMCOMP | TOOL_GEN_GTKW
};

typedef struct {
    char *path;
    char *yanc_root;
    bool had_path;
    bool had_yanc_root;
} EnvironmentBackup;

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static char *join_path(const char *left, const char *right)
{
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    char *path = malloc(left_length + right_length + 2U);

    if (path == NULL) return NULL;
    (void)snprintf(
        path, left_length + right_length + 2U, "%s/%s", left, right
    );
    return path;
}

static bool directory_exists(const char *path)
{
    struct stat information;

    return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

static bool ensure_directory(const char *path)
{
    char *copy = strdup(path);
    char *cursor;
    bool success = true;

    if (copy == NULL) return false;
    for (cursor = copy + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
            success = false;
            break;
        }
        if (!directory_exists(copy)) {
            success = false;
            break;
        }
        *cursor = '/';
    }
    if (success && mkdir(copy, 0700) != 0 && errno != EEXIST) {
        success = false;
    }
    if (success && !directory_exists(copy)) success = false;
    free(copy);
    return success;
}

static bool copy_executable(const char *source_path, const char *target_path)
{
    char buffer[8192];
    int source = -1;
    int target = -1;
    bool success = false;

    source = open(source_path, O_RDONLY);
    if (source < 0) goto cleanup;
    target = open(target_path, O_WRONLY | O_CREAT | O_EXCL, 0700);
    if (target < 0) goto cleanup;
    for (;;) {
        ssize_t count = read(source, buffer, sizeof(buffer));
        ssize_t written = 0;

        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            goto cleanup;
        }
        while (written < count) {
            ssize_t step = write(
                target, buffer + written, (size_t)(count - written)
            );

            if (step < 0 && errno == EINTR) continue;
            if (step <= 0) goto cleanup;
            written += step;
        }
    }
    success = true;

cleanup:
    if (target >= 0 && close(target) != 0) success = false;
    if (source >= 0 && close(source) != 0) success = false;
    if (!success) (void)unlink(target_path);
    return success;
}

static bool create_tool(
    const char *root,
    const char *name,
    const char *self_path
)
{
    char *relative = join_path("bin", name);
    char *path;
    bool success;

    if (relative == NULL) return false;
    path = join_path(root, relative);
    free(relative);
    if (path == NULL) return false;
    success = copy_executable(self_path, path);
    free(path);
    return success;
}

static bool create_tools(
    const char *root,
    unsigned int tool_mask,
    const char *self_path
)
{
    if ((tool_mask & TOOL_CMMCOMP) != 0U &&
        !create_tool(root, "cmmcomp", self_path)) return false;
    if ((tool_mask & TOOL_CPPPP) != 0U &&
        !create_tool(root, "cpppp", self_path)) return false;
    if ((tool_mask & TOOL_CPPCOMP) != 0U &&
        !create_tool(root, "cppcomp", self_path)) return false;
    if ((tool_mask & TOOL_APPCOMP) != 0U &&
        !create_tool(root, "appcomp", self_path)) return false;
    if ((tool_mask & TOOL_ASMCOMP) != 0U &&
        !create_tool(root, "asmcomp", self_path)) return false;
    if ((tool_mask & TOOL_GEN_GTKW) != 0U &&
        !create_tool(root, "gen_gtkw", self_path)) return false;
    return true;
}

static char *create_layout(
    const char *base,
    const char *name,
    SolarYancLayoutKind layout,
    bool include_headers,
    unsigned int tool_mask,
    const char *self_path
)
{
    char *root = join_path(base, name);
    char *bin = NULL;
    char *hdl = NULL;
    char *macros = NULL;
    char *headers = NULL;
    bool success = false;

    if (root == NULL) goto cleanup;
    bin = join_path(root, "bin");
    hdl = join_path(root, "HDL");
    macros = join_path(
        root,
        layout == SOLAR_YANC_LAYOUT_CHECKOUT
            ? "Compilers/CMMComp/Includes"
            : "Macros"
    );
    if (include_headers) {
        headers = join_path(
            root,
            layout == SOLAR_YANC_LAYOUT_CHECKOUT
                ? "Compilers/CPPComp/Includes"
                : "Header"
        );
    }
    if (bin == NULL || hdl == NULL || macros == NULL ||
        (include_headers && headers == NULL)) goto cleanup;
    if (!ensure_directory(bin) || !ensure_directory(hdl) ||
        !ensure_directory(macros) ||
        (include_headers && !ensure_directory(headers))) goto cleanup;
    success = create_tools(root, tool_mask, self_path);

cleanup:
    free(bin);
    free(hdl);
    free(macros);
    free(headers);
    if (!success) {
        free(root);
        root = NULL;
    }
    return root;
}

static void remove_tree(const char *path)
{
    struct stat information;
    DIR *directory;
    struct dirent *entry;

    if (lstat(path, &information) != 0) return;
    if (!S_ISDIR(information.st_mode)) {
        (void)unlink(path);
        return;
    }
    directory = opendir(path);
    if (directory == NULL) return;
    while ((entry = readdir(directory)) != NULL) {
        char *child;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        child = join_path(path, entry->d_name);
        if (child != NULL) {
            remove_tree(child);
            free(child);
        }
    }
    (void)closedir(directory);
    (void)rmdir(path);
}

static bool save_environment(EnvironmentBackup *backup)
{
    const char *path = getenv("PATH");
    const char *root = getenv("SOLAR_YANC_ROOT");

    (void)memset(backup, 0, sizeof(*backup));
    backup->had_path = path != NULL;
    backup->had_yanc_root = root != NULL;
    if (path != NULL) backup->path = strdup(path);
    if (root != NULL) backup->yanc_root = strdup(root);
    if ((path != NULL && backup->path == NULL) ||
        (root != NULL && backup->yanc_root == NULL)) {
        free(backup->path);
        free(backup->yanc_root);
        return false;
    }
    return true;
}

static void restore_environment(EnvironmentBackup *backup)
{
    if (backup->had_path) {
        (void)setenv("PATH", backup->path, 1);
    } else {
        (void)unsetenv("PATH");
    }
    if (backup->had_yanc_root) {
        (void)setenv("SOLAR_YANC_ROOT", backup->yanc_root, 1);
    } else {
        (void)unsetenv("SOLAR_YANC_ROOT");
    }
    free(backup->path);
    free(backup->yanc_root);
}

static bool initialize_project(
    SolarProject *project,
    const char *project_root,
    const char *language,
    const char *yanc_root
)
{
    solar_project_init(project);
    project->root = strdup(project_root);
    project->config.project.language = strdup(language);
    if (yanc_root != NULL) project->config.yanc.root = strdup(yanc_root);
    return project->root != NULL &&
           project->config.project.language != NULL &&
           (yanc_root == NULL || project->config.yanc.root != NULL);
}

static bool resolved_root_equals(
    const SolarYancToolchain *toolchain,
    const char *expected_root
)
{
    char *expected = realpath(expected_root, NULL);
    bool matches = expected != NULL && toolchain->root != NULL &&
                   strcmp(expected, toolchain->root) == 0;

    free(expected);
    return matches;
}

static int test_environment_release_precedence(const char *self_path)
{
    const char *test_name = "YANC environment release precedence";
    char directory_template[] = "/tmp/solar-yanc-env-XXXXXX";
    char *base = mkdtemp(directory_template);
    char *environment_root = NULL;
    char *manifest_root = NULL;
    char *project_root = NULL;
    EnvironmentBackup environment;
    SolarProject project;
    SolarYancToolchain toolchain;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    solar_yanc_toolchain_init(&toolchain);
    if (base == NULL || !save_environment(&environment)) {
        return report_failure(test_name, "could not prepare test environment");
    }
    environment_root = create_layout(
        base, "release root with spaces", SOLAR_YANC_LAYOUT_RELEASE,
        true, TOOL_ALL, self_path
    );
    manifest_root = create_layout(
        base, "checkout fallback", SOLAR_YANC_LAYOUT_CHECKOUT,
        true, TOOL_ALL, self_path
    );
    project_root = join_path(base, "project");
    if (environment_root == NULL || manifest_root == NULL ||
        project_root == NULL || !ensure_directory(project_root) ||
        !initialize_project(&project, project_root, "cpp", manifest_root) ||
        setenv("SOLAR_YANC_ROOT", environment_root, 1) != 0) {
        failed = report_failure(test_name, "could not create fake layouts");
        goto cleanup;
    }
    result = solar_yanc_resolve(&project, &toolchain);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
    } else if (toolchain.layout != SOLAR_YANC_LAYOUT_RELEASE ||
               !resolved_root_equals(&toolchain, environment_root) ||
               toolchain.bin_directory == NULL ||
               toolchain.hdl_directory == NULL ||
               toolchain.macros_directory == NULL ||
               toolchain.headers_directory == NULL ||
               toolchain.cmmcomp == NULL || toolchain.cpppp == NULL ||
               toolchain.cppcomp == NULL || toolchain.appcomp == NULL ||
               toolchain.asmcomp == NULL || toolchain.gen_gtkw == NULL ||
               toolchain.version == NULL ||
               strcmp(toolchain.version, "cmmcomp (YANC) 5.2-fake") != 0) {
        failed = report_failure(test_name, "release resolution was incorrect");
    }

cleanup:
    solar_yanc_toolchain_free(&toolchain);
    solar_project_free(&project);
    restore_environment(&environment);
    free(environment_root);
    free(manifest_root);
    free(project_root);
    if (base != NULL) remove_tree(base);
    return failed;
}

static int test_checkout_with_spaces(const char *self_path)
{
    const char *test_name = "YANC checkout with spaces";
    char directory_template[] = "/tmp/solar-yanc-checkout-XXXXXX";
    char *base = mkdtemp(directory_template);
    char *project_root = NULL;
    char *checkout_root = NULL;
    EnvironmentBackup environment;
    SolarProject project;
    SolarYancToolchain toolchain;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    solar_yanc_toolchain_init(&toolchain);
    if (base == NULL || !save_environment(&environment)) {
        return report_failure(test_name, "could not prepare test environment");
    }
    project_root = join_path(base, "project root");
    if (project_root != NULL && ensure_directory(project_root)) {
        checkout_root = create_layout(
            project_root, "YANC checkout", SOLAR_YANC_LAYOUT_CHECKOUT,
            false, TOOL_CMMCOMP | TOOL_APPCOMP | TOOL_ASMCOMP, self_path
        );
    }
    if (checkout_root == NULL ||
        !initialize_project(&project, project_root, "cmm", checkout_root) ||
        unsetenv("SOLAR_YANC_ROOT") != 0 || setenv("PATH", base, 1) != 0) {
        failed = report_failure(test_name, "could not create checkout fixture");
        goto cleanup;
    }
    result = solar_yanc_resolve(&project, &toolchain);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
    } else if (toolchain.layout != SOLAR_YANC_LAYOUT_CHECKOUT ||
               !resolved_root_equals(&toolchain, checkout_root) ||
               toolchain.headers_directory != NULL ||
               toolchain.cmmcomp == NULL || toolchain.appcomp == NULL ||
               toolchain.asmcomp == NULL || toolchain.cpppp != NULL ||
               toolchain.cppcomp != NULL || toolchain.gen_gtkw != NULL) {
        failed = report_failure(
            test_name, "CMM incorrectly required optional C++ components"
        );
    }

cleanup:
    solar_yanc_toolchain_free(&toolchain);
    solar_project_free(&project);
    restore_environment(&environment);
    free(project_root);
    free(checkout_root);
    if (base != NULL) remove_tree(base);
    return failed;
}

static int test_path_doctor_and_asm(const char *self_path)
{
    const char *test_name = "YANC PATH doctor and Assembly requirements";
    char directory_template[] = "/tmp/solar-yanc-path-XXXXXX";
    char *base = mkdtemp(directory_template);
    char *release_root = NULL;
    char *bin_path = NULL;
    EnvironmentBackup environment;
    SolarProject project;
    SolarYancToolchain doctor_toolchain;
    SolarYancToolchain asm_toolchain;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    solar_yanc_toolchain_init(&doctor_toolchain);
    solar_yanc_toolchain_init(&asm_toolchain);
    if (base == NULL || !save_environment(&environment)) {
        return report_failure(test_name, "could not prepare test environment");
    }
    release_root = create_layout(
        base, "partial release with spaces", SOLAR_YANC_LAYOUT_RELEASE,
        false, TOOL_APPCOMP | TOOL_ASMCOMP, self_path
    );
    if (release_root != NULL) bin_path = join_path(release_root, "bin");
    if (release_root == NULL || bin_path == NULL ||
        !initialize_project(&project, base, "asm", NULL) ||
        setenv("SOLAR_YANC_ROOT", release_root, 1) != 0 ||
        setenv("PATH", bin_path, 1) != 0) {
        failed = report_failure(test_name, "could not create PATH fixture");
        goto cleanup;
    }

    result = solar_yanc_resolve(NULL, &doctor_toolchain);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    if (!resolved_root_equals(&doctor_toolchain, release_root) ||
        doctor_toolchain.layout != SOLAR_YANC_LAYOUT_RELEASE ||
        doctor_toolchain.headers_directory != NULL ||
        doctor_toolchain.cmmcomp != NULL || doctor_toolchain.cpppp != NULL ||
        doctor_toolchain.cppcomp != NULL || doctor_toolchain.appcomp == NULL ||
        doctor_toolchain.asmcomp == NULL || doctor_toolchain.version == NULL ||
        strcmp(doctor_toolchain.version, "asmcomp (YANC) 5.2-fake") != 0) {
        failed = report_failure(test_name, "doctor did not preserve partial layout");
        goto cleanup;
    }

    result = solar_yanc_resolve(&project, &asm_toolchain);
    if (result.status != SOLAR_STATUS_OK ||
        !resolved_root_equals(&asm_toolchain, release_root)) {
        failed = report_failure(
            test_name,
            result.status == SOLAR_STATUS_OK
                ? "Assembly resolution selected the wrong root"
                : result.diagnostic.message
        );
    }

cleanup:
    solar_yanc_toolchain_free(&doctor_toolchain);
    solar_yanc_toolchain_free(&asm_toolchain);
    solar_project_free(&project);
    restore_environment(&environment);
    free(release_root);
    free(bin_path);
    if (base != NULL) remove_tree(base);
    return failed;
}

static int test_partial_layout_reports_optional_components(const char *self_path)
{
    const char *test_name = "YANC partial layout optional components";
    char directory_template[] = "/tmp/solar-yanc-cpp-XXXXXX";
    char *base = mkdtemp(directory_template);
    char *root = NULL;
    EnvironmentBackup environment;
    SolarProject project;
    SolarYancToolchain toolchain;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    solar_yanc_toolchain_init(&toolchain);
    if (base == NULL || !save_environment(&environment)) {
        return report_failure(test_name, "could not prepare test environment");
    }
    root = create_layout(
        base, "release", SOLAR_YANC_LAYOUT_RELEASE,
        false, TOOL_CPPPP | TOOL_APPCOMP | TOOL_ASMCOMP,
        self_path
    );
    if (root == NULL || !initialize_project(&project, base, "cpp", root) ||
        unsetenv("SOLAR_YANC_ROOT") != 0) {
        failed = report_failure(test_name, "could not create C++ fixture");
        goto cleanup;
    }
    result = solar_yanc_resolve(&project, &toolchain);
    if (result.status != SOLAR_STATUS_OK ||
        toolchain.layout != SOLAR_YANC_LAYOUT_RELEASE ||
        toolchain.headers_directory != NULL || toolchain.cppcomp != NULL ||
        toolchain.cpppp == NULL || toolchain.appcomp == NULL ||
        toolchain.asmcomp == NULL) {
        failed = report_failure(
            test_name,
            "resolver did not expose the partial layout for diagnostics"
        );
    }

cleanup:
    solar_yanc_toolchain_free(&toolchain);
    solar_project_free(&project);
    restore_environment(&environment);
    free(root);
    if (base != NULL) remove_tree(base);
    return failed;
}

static int test_invalid_explicit_root_does_not_fall_back(const char *self_path)
{
    const char *test_name = "YANC invalid explicit root precedence";
    char directory_template[] = "/tmp/solar-yanc-invalid-XXXXXX";
    char *base = mkdtemp(directory_template);
    char *invalid_root = NULL;
    char *invalid_bin = NULL;
    char *valid_root = NULL;
    char *valid_bin = NULL;
    EnvironmentBackup environment;
    SolarYancToolchain toolchain;
    SolarResult result;
    int failed = 0;

    solar_yanc_toolchain_init(&toolchain);
    if (base == NULL || !save_environment(&environment)) {
        return report_failure(test_name, "could not prepare test environment");
    }
    invalid_root = join_path(base, "invalid root");
    if (invalid_root != NULL) invalid_bin = join_path(invalid_root, "bin");
    valid_root = create_layout(
        base, "valid fallback", SOLAR_YANC_LAYOUT_RELEASE,
        true, TOOL_ALL, self_path
    );
    if (valid_root != NULL) valid_bin = join_path(valid_root, "bin");
    if (invalid_bin == NULL || valid_bin == NULL ||
        !ensure_directory(invalid_bin) ||
        setenv("SOLAR_YANC_ROOT", invalid_root, 1) != 0 ||
        setenv("PATH", valid_bin, 1) != 0) {
        failed = report_failure(test_name, "could not create invalid fixture");
        goto cleanup;
    }
    result = solar_yanc_resolve(NULL, &toolchain);
    if (result.status == SOLAR_STATUS_OK ||
        strstr(result.diagnostic.message, "layout") == NULL ||
        toolchain.root != NULL) {
        failed = report_failure(test_name, "invalid explicit root fell back to PATH");
    }

cleanup:
    solar_yanc_toolchain_free(&toolchain);
    restore_environment(&environment);
    free(invalid_root);
    free(invalid_bin);
    free(valid_root);
    free(valid_bin);
    if (base != NULL) remove_tree(base);
    return failed;
}

static int test_bundled_toolchain(void)
{
    const char *test_name = "YANC bundled toolchain";
    char directory_template[] = "/tmp/solar-yanc-absent-XXXXXX";
    char *base = mkdtemp(directory_template);
    EnvironmentBackup environment;
    SolarYancToolchain toolchain;
    SolarResult result;
    int failed = 0;

    solar_yanc_toolchain_init(&toolchain);
    if (base == NULL || !save_environment(&environment)) {
        return report_failure(test_name, "could not prepare test environment");
    }
    if (unsetenv("SOLAR_YANC_ROOT") != 0 || setenv("PATH", base, 1) != 0) {
        failed = report_failure(test_name, "could not isolate PATH");
        goto cleanup;
    }
    result = solar_yanc_resolve(NULL, &toolchain);
    if (result.status != SOLAR_STATUS_OK || toolchain.root == NULL ||
        !toolchain.bundled || toolchain.cmmcomp == NULL ||
        toolchain.cpppp == NULL || toolchain.cppcomp == NULL ||
        toolchain.appcomp == NULL || toolchain.asmcomp == NULL) {
        failed = report_failure(test_name, "bundled YANC was not resolved");
    }

cleanup:
    solar_yanc_toolchain_free(&toolchain);
    restore_environment(&environment);
    if (base != NULL) remove_tree(base);
    return failed;
}

int main(int argc, char *argv[])
{
    char *self_path;
    int failures = 0;

    if (argc != 2) {
        return report_failure(
            "YANC resolver tests", "expected the fake YANC tool path"
        );
    }
    self_path = realpath(argv[1], NULL);
    if (self_path == NULL) {
        return report_failure(
            "YANC resolver tests", "could not resolve the test executable"
        );
    }
    failures += test_environment_release_precedence(self_path);
    failures += test_checkout_with_spaces(self_path);
    failures += test_path_doctor_and_asm(self_path);
    failures += test_partial_layout_reports_optional_components(self_path);
    failures += test_invalid_explicit_root_does_not_fall_back(self_path);
    failures += test_bundled_toolchain();
    failures += solar_yanc_resolve(NULL, NULL).status ==
                SOLAR_STATUS_INVALID_ARGUMENT ? 0 : 1;
    free(self_path);
    return failures == 0 ? 0 : 1;
}
