#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/build.h"
#include "solar/artifact.h"
#include "solar/compiler.h"
#include "solar/project.h"
#include "solar/test.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *base;
    char *project_root;
    char *toolchain_root;
    char *trace_path;
    char *source_path;
    char *include_path;
} TestFixture;

typedef struct {
    char *root;
    char *trace;
    char *failure;
    char *omission;
    bool had_root;
    bool had_trace;
    bool had_failure;
    bool had_omission;
} EnvironmentBackup;

static int fail_test(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
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
        *cursor = '/';
    }
    if (success && mkdir(copy, 0700) != 0 && errno != EEXIST) success = false;
    free(copy);
    return success;
}

static bool write_text(const char *path, const char *text)
{
    int descriptor = open(
        path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600
    );
    size_t length = strlen(text);
    size_t offset = 0U;

    if (descriptor < 0) return false;
    while (offset < length) {
        ssize_t count = write(descriptor, text + offset, length - offset);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            (void)close(descriptor);
            return false;
        }
        offset += (size_t)count;
    }
    return close(descriptor) == 0;
}

static char *read_text(const char *path)
{
    struct stat information;
    char *text;
    size_t offset = 0U;
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);

    if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
        information.st_size < 0) {
        if (descriptor >= 0) (void)close(descriptor);
        return NULL;
    }
    text = malloc((size_t)information.st_size + 1U);
    if (text == NULL) {
        (void)close(descriptor);
        return NULL;
    }
    while (offset < (size_t)information.st_size) {
        ssize_t count = read(
            descriptor, text + offset, (size_t)information.st_size - offset
        );

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            free(text);
            (void)close(descriptor);
            return NULL;
        }
        offset += (size_t)count;
    }
    text[offset] = '\0';
    (void)close(descriptor);
    return text;
}

static bool regular_file_exists(const char *path)
{
    struct stat information;

    return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static bool copy_executable(const char *source, const char *destination)
{
    unsigned char buffer[8192];
    int input = -1;
    int output = -1;
    bool success = false;

    input = open(source, O_RDONLY | O_CLOEXEC);
    if (input < 0) goto cleanup;
    output = open(
        destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700
    );
    if (output < 0) goto cleanup;
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        size_t offset = 0U;

        if (count < 0 && errno == EINTR) continue;
        if (count < 0) goto cleanup;
        if (count == 0) break;
        while (offset < (size_t)count) {
            ssize_t written = write(
                output, buffer + offset, (size_t)count - offset
            );

            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) goto cleanup;
            offset += (size_t)written;
        }
    }
    success = true;

cleanup:
    if (output >= 0 && close(output) != 0) success = false;
    if (input >= 0) (void)close(input);
    if (!success) (void)unlink(destination);
    return success;
}

static void remove_tree(const char *path)
{
    struct stat information;
    DIR *directory;
    struct dirent *entry;

    if (path == NULL || lstat(path, &information) != 0) return;
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

static bool save_variable(
    const char *name,
    char **value_out,
    bool *present_out
)
{
    const char *value = getenv(name);

    *present_out = value != NULL;
    *value_out = value == NULL ? NULL : strdup(value);
    return value == NULL || *value_out != NULL;
}

static bool save_environment(EnvironmentBackup *backup)
{
    bool success;

    (void)memset(backup, 0, sizeof(*backup));
    success = save_variable(
        "SOLAR_YANC_ROOT", &backup->root, &backup->had_root
    ) && save_variable(
        "SOLAR_FAKE_YANC_TRACE", &backup->trace, &backup->had_trace
    ) && save_variable(
        "SOLAR_FAKE_YANC_FAIL_STAGE", &backup->failure,
        &backup->had_failure
    ) && save_variable(
        "SOLAR_FAKE_YANC_OMIT", &backup->omission,
        &backup->had_omission
    );
    if (!success) {
        free(backup->root);
        free(backup->trace);
        free(backup->failure);
        free(backup->omission);
        (void)memset(backup, 0, sizeof(*backup));
    }
    return success;
}

static void restore_variable(const char *name, const char *value, bool present)
{
    if (present) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
}

static void restore_environment(EnvironmentBackup *backup)
{
    restore_variable("SOLAR_YANC_ROOT", backup->root, backup->had_root);
    restore_variable(
        "SOLAR_FAKE_YANC_TRACE", backup->trace, backup->had_trace
    );
    restore_variable(
        "SOLAR_FAKE_YANC_FAIL_STAGE", backup->failure, backup->had_failure
    );
    restore_variable(
        "SOLAR_FAKE_YANC_OMIT", backup->omission, backup->had_omission
    );
    free(backup->root);
    free(backup->trace);
    free(backup->failure);
    free(backup->omission);
}

static bool create_fake_toolchain(
    TestFixture *fixture,
    const char *tool_path
)
{
    static const char *const TOOLS[] = {
        "cmmcomp", "cpppp", "cppcomp", "appcomp", "asmcomp", "gen_gtkw",
        "iverilog", "vvp"
    };
    static const char *const HDL[] = {
        "addr_dec.v", "instr_dec.v", "processor.v", "core.v", "ula.v"
    };
    char *bin = join_path(fixture->toolchain_root, "bin");
    char *hdl = join_path(fixture->toolchain_root, "HDL");
    char *macros = join_path(fixture->toolchain_root, "Macros");
    char *headers = join_path(fixture->toolchain_root, "Header");
    size_t index;
    bool success = false;

    if (bin == NULL || hdl == NULL || macros == NULL || headers == NULL ||
        !ensure_directory(bin) || !ensure_directory(hdl) ||
        !ensure_directory(macros) || !ensure_directory(headers)) goto cleanup;
    for (index = 0U; index < sizeof(TOOLS) / sizeof(TOOLS[0]); index++) {
        char *destination = join_path(bin, TOOLS[index]);

        if (destination == NULL || !copy_executable(tool_path, destination)) {
            free(destination);
            goto cleanup;
        }
        free(destination);
    }
    for (index = 0U; index < sizeof(HDL) / sizeof(HDL[0]); index++) {
        char *path = join_path(hdl, HDL[index]);
        char module[128];
        size_t name_length = strlen(HDL[index]);

        if (name_length < 3U ||
            name_length - 2U >= sizeof(module) - strlen("module ; endmodule\n") ||
            snprintf(
                module, sizeof(module), "module %.*s; endmodule\n",
                (int)(name_length - 2U), HDL[index]
            ) < 0 || path == NULL || !write_text(path, module)) {
            free(path);
            goto cleanup;
        }
        free(path);
    }
    success = true;

cleanup:
    free(headers);
    free(macros);
    free(hdl);
    free(bin);
    return success;
}

static const char *source_extension(const char *language)
{
    if (strcmp(language, "cmm") == 0) return "cmm";
    if (strcmp(language, "cpp") == 0) return "cpp";
    return "asm";
}

static bool write_manifest(
    const TestFixture *fixture,
    const char *language,
    const char *diagnostics
)
{
    char *manifest = join_path(fixture->project_root, "solar.toml");
    FILE *file;
    bool success = false;

    if (manifest == NULL) return false;
    file = fopen(manifest, "w");
    if (file == NULL) goto cleanup;
    if (fprintf(
            file,
            "[solar]\nformat = 2\n\n"
            "[project]\nname = \"fake-yanc\"\n"
            "language = \"%s\"\ndefault_test = \"generated\"\n\n"
            "[compiler]\nbackend = \"yanc\"\n"
            "source = \"src/processor.%s\"\nprocessor = \"processor\"\n"
            "frequency_mhz = 125\nsimulation_clocks = 4567\n",
            language, source_extension(language)
        ) < 0) goto close_file;
    if (strcmp(language, "cpp") == 0 &&
        fprintf(file, "include_dirs = [\"src/include path\"]\n") < 0) {
        goto close_file;
    }
    if (fprintf(
            file,
            "\n[yanc]\ndiagnostics = \"%s\"\n\n"
            "[simulation]\nbackend = \"iverilog\"\n\n"
            "[synthesis]\nbackend = \"yosys\"\ntop = \"processor\"\n",
            diagnostics
        ) < 0) goto close_file;
    success = true;

close_file:
    if (fclose(file) != 0) success = false;
cleanup:
    free(manifest);
    return success;
}

static bool fixture_create(
    TestFixture *fixture,
    const char *language,
    const char *diagnostics,
    const char *tool_path
)
{
    char directory_template[] = "/tmp/solar-yanc-compiler-XXXXXX";
    char *temporary = mkdtemp(directory_template);
    char *source_directory = NULL;
    char *source_name = NULL;
    const char *source_text;
    bool success = false;

    (void)memset(fixture, 0, sizeof(*fixture));
    source_text = strcmp(language, "cmm") == 0
        ? "#PRNAME processor\nvoid main() {}\n"
        : "original YANC source\n";
    if (temporary == NULL) return false;
    fixture->base = strdup(temporary);
    if (fixture->base == NULL) return false;
    fixture->project_root = join_path(fixture->base, "project with spaces");
    fixture->toolchain_root = join_path(
        fixture->base, "YANC release with spaces"
    );
    fixture->trace_path = join_path(fixture->base, "tool arguments.log");
    source_directory = join_path(fixture->project_root, "src");
    fixture->include_path = join_path(source_directory, "include path");
    source_name = malloc(strlen("processor.") +
                         strlen(source_extension(language)) + 1U);
    if (source_name != NULL) {
        (void)snprintf(
            source_name,
            strlen("processor.") + strlen(source_extension(language)) + 1U,
            "processor.%s", source_extension(language)
        );
        fixture->source_path = join_path(source_directory, source_name);
    }
    if (fixture->project_root == NULL || fixture->toolchain_root == NULL ||
        fixture->trace_path == NULL || source_directory == NULL ||
        fixture->include_path == NULL || fixture->source_path == NULL ||
        !ensure_directory(fixture->include_path) ||
        !write_text(fixture->source_path, source_text) ||
        !write_manifest(fixture, language, diagnostics) ||
        !create_fake_toolchain(fixture, tool_path) ||
        setenv("SOLAR_YANC_ROOT", fixture->toolchain_root, 1) != 0 ||
        setenv("SOLAR_FAKE_YANC_TRACE", fixture->trace_path, 1) != 0 ||
        unsetenv("SOLAR_FAKE_YANC_FAIL_STAGE") != 0 ||
        unsetenv("SOLAR_FAKE_YANC_OMIT") != 0) goto cleanup;
    success = true;

cleanup:
    free(source_name);
    free(source_directory);
    return success;
}

static void fixture_free(TestFixture *fixture)
{
    remove_tree(fixture->base);
    free(fixture->include_path);
    free(fixture->source_path);
    free(fixture->trace_path);
    free(fixture->toolchain_root);
    free(fixture->project_root);
    free(fixture->base);
    (void)memset(fixture, 0, sizeof(*fixture));
}

static bool run_compile(
    const TestFixture *fixture,
    SolarProject *project,
    SolarCompilerResult *compiler_result,
    SolarResult *result_out
)
{
    SolarResult result;

    solar_project_init(project);
    solar_compiler_result_init(compiler_result);
    result = solar_project_load(fixture->project_root, project);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_compiler_compile(project, compiler_result);
    }
    *result_out = result;
    return result.status == SOLAR_STATUS_OK;
}

static bool stages_equal(
    const SolarCompilerResult *result,
    const char *const *expected,
    size_t expected_count
)
{
    size_t index;

    if (result->stage_count != expected_count) return false;
    for (index = 0U; index < expected_count; index++) {
        if (strcmp(result->stages[index].name, expected[index]) != 0 ||
            result->stages[index].result.status != SOLAR_STATUS_OK ||
            result->stages[index].log_path == NULL) return false;
    }
    return true;
}

static bool trace_stage_contains(
    const char *trace,
    const char *stage,
    const char *needle
)
{
    const char *line = strstr(trace, stage);
    const char *end;
    const char *match;

    if (line == NULL) return false;
    end = strchr(line, '\n');
    if (end == NULL) end = line + strlen(line);
    match = strstr(line, needle);
    return match != NULL && match < end;
}

static bool success_artifacts_are_complete(
    const TestFixture *fixture,
    const SolarCompilerResult *compiler_result
)
{
    char *metadata = join_path(
        fixture->project_root,
        ".solar/state/yanc/processor/build-info.txt"
    );
    char *rtl = NULL;
    char *testbench = NULL;
    bool complete;

    if (compiler_result->artifacts.rtl_sources.count == 1U) {
        rtl = read_text(compiler_result->artifacts.rtl_sources.items[0]);
    }
    testbench = read_text(compiler_result->artifacts.testbench_path);
    complete = compiler_result->toolchain_version != NULL &&
        strstr(compiler_result->toolchain_version, "5.2-fake") != NULL &&
        compiler_result->artifacts.hdl_sources.count == 5U &&
        compiler_result->artifacts.auxiliary_files.count >= 4U &&
        regular_file_exists(compiler_result->artifacts.assembly_path) &&
        regular_file_exists(compiler_result->artifacts.data_image_path) &&
        regular_file_exists(compiler_result->artifacts.instruction_image_path) &&
        regular_file_exists(compiler_result->artifacts.testbench_path) &&
        regular_file_exists(
            compiler_result->artifacts.instruction_translation_path
        ) &&
        metadata != NULL && regular_file_exists(metadata) &&
        rtl != NULL && testbench != NULL &&
        strstr(rtl, ".solar/tmp/yanc") == NULL &&
        strstr(testbench, ".solar/tmp/yanc") == NULL &&
        strstr(rtl, compiler_result->artifacts.build_directory) != NULL &&
        strstr(testbench, fixture->project_root) != NULL &&
        strstr(compiler_result->artifacts.build_directory, fixture->project_root) ==
            compiler_result->artifacts.build_directory;
    free(testbench);
    free(rtl);
    free(metadata);
    return complete;
}

static bool generated_assembly_is_public(
    const TestFixture *fixture,
    const SolarCompilerResult *compiler_result
)
{
    SolarArtifactList artifacts;
    const SolarArtifactRecord *record;
    char *expected = join_path(
        fixture->project_root, "software/processor.asm"
    );
    char *text = NULL;
    SolarResult result;
    bool valid = false;

    solar_artifact_list_init(&artifacts);
    if (expected == NULL || compiler_result->artifacts.assembly_path == NULL ||
        strcmp(compiler_result->artifacts.assembly_path, expected) != 0) {
        goto cleanup;
    }
    text = read_text(expected);
    result = solar_artifact_list_load(fixture->project_root, &artifacts);
    record = result.status == SOLAR_STATUS_OK
        ? solar_artifact_find(&artifacts, "assembly", "generated")
        : NULL;
    valid = text != NULL && strstr(text, "fake YANC assembly") != NULL &&
        record != NULL && strcmp(record->path, "software/processor.asm") == 0 &&
        strcmp(record->stage, "generation") == 0 &&
        strcmp(record->backend, "yanc") == 0;

cleanup:
    solar_artifact_list_free(&artifacts);
    free(text);
    free(expected);
    return valid;
}

static int test_cmm_pipeline(const char *tool_path)
{
    static const char *const STAGES[] = {"cmmcomp", "appcomp", "asmcomp"};
    const char *name = "YANC CMM compiler pipeline";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *trace = NULL;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        fixture_free(&fixture);
        return failed;
    }
    if (!run_compile(&fixture, &project, &compiler_result, &result)) {
        failed = fail_test(name, result.diagnostic.message);
        goto cleanup;
    }
    trace = read_text(fixture.trace_path);
    if (!stages_equal(&compiler_result, STAGES, 3U) || trace == NULL ||
        strstr(trace, "cmmcomp\t-pt\t-i\tprocessor.cmm") == NULL ||
        !trace_stage_contains(trace, "asmcomp", "\t-f\t125") ||
        !trace_stage_contains(trace, "asmcomp", "\t-c\t4567") ||
        strstr(trace, "cpppp") != NULL ||
        !generated_assembly_is_public(&fixture, &compiler_result) ||
        !success_artifacts_are_complete(&fixture, &compiler_result)) {
        failed = fail_test(name, "pipeline arguments or artifacts were wrong");
    }

cleanup:
    free(trace);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static int test_cpp_pipeline(const char *tool_path)
{
    static const char *const STAGES[] = {
        "cpppp", "cppcomp", "appcomp", "asmcomp"
    };
    const char *name = "YANC C++ compiler pipeline";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *trace = NULL;
    char *headers = NULL;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "cpp", "en", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        fixture_free(&fixture);
        return failed;
    }
    headers = join_path(fixture.toolchain_root, "Header");
    if (!run_compile(&fixture, &project, &compiler_result, &result)) {
        failed = fail_test(name, result.diagnostic.message);
        goto cleanup;
    }
    trace = read_text(fixture.trace_path);
    if (!stages_equal(&compiler_result, STAGES, 4U) || trace == NULL ||
        headers == NULL ||
        !trace_stage_contains(trace, "cpppp", headers) ||
        !trace_stage_contains(trace, "cpppp", fixture.include_path) ||
        trace_stage_contains(trace, "cpppp", "\t-en") ||
        trace_stage_contains(trace, "cppcomp", "\t-en") ||
        !trace_stage_contains(trace, "appcomp", "\t-en") ||
        !trace_stage_contains(trace, "asmcomp", "\t-en") ||
        strstr(trace, "cmmcomp") != NULL ||
        !generated_assembly_is_public(&fixture, &compiler_result) ||
        !success_artifacts_are_complete(&fixture, &compiler_result)) {
        failed = fail_test(name, "C++ include or language arguments were wrong");
    }

cleanup:
    free(headers);
    free(trace);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static int test_assembly_pipeline(const char *tool_path)
{
    static const char *const STAGES[] = {"appcomp", "asmcomp"};
    const char *name = "YANC Assembly compiler pipeline";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *before = NULL;
    char *after = NULL;
    char *trace = NULL;
    char *program_counter_path = NULL;
    char *program_counter_text = NULL;
    char *generated_public_assembly = NULL;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "asm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        fixture_free(&fixture);
        return failed;
    }
    before = read_text(fixture.source_path);
    if (!run_compile(&fixture, &project, &compiler_result, &result)) {
        failed = fail_test(name, result.diagnostic.message);
        goto cleanup;
    }
    after = read_text(fixture.source_path);
    trace = read_text(fixture.trace_path);
    program_counter_path = join_path(
        compiler_result.artifacts.working_directory,
        "pc_processor_mem.txt"
    );
    if (program_counter_path != NULL) {
        program_counter_text = read_text(program_counter_path);
    }
    generated_public_assembly = join_path(
        fixture.project_root, "software/processor.asm"
    );
    if (before == NULL || after == NULL || strcmp(before, after) != 0 ||
        trace == NULL || !stages_equal(&compiler_result, STAGES, 2U) ||
        strstr(trace, "cmmcomp") != NULL || strstr(trace, "cpppp") != NULL ||
        !trace_stage_contains(trace, "appcomp", "processor.asm") ||
        program_counter_text == NULL ||
        strcmp(program_counter_text, "00000000000000000000\n") != 0 ||
        generated_public_assembly == NULL ||
        regular_file_exists(generated_public_assembly) ||
        !success_artifacts_are_complete(&fixture, &compiler_result)) {
        failed = fail_test(name, "Assembly staging modified source or stages");
    }

cleanup:
    free(program_counter_text);
    free(program_counter_path);
    free(generated_public_assembly);
    free(trace);
    free(after);
    free(before);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static int test_failure_preserves_previous_build(const char *tool_path)
{
    const char *name = "YANC stage failure atomic publication";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *marker = NULL;
    char *assembly = NULL;
    char *assembly_after = NULL;
    char *trace = NULL;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        goto cleanup;
    }
    if (!run_compile(&fixture, &project, &compiler_result, &result)) {
        failed = fail_test(
            name, result.diagnostic.message
        );
        goto cleanup;
    }
    marker = join_path(
        compiler_result.artifacts.build_directory, "previous-build.marker"
    );
    assembly = join_path(fixture.project_root, "software/processor.asm");
    if (marker == NULL || assembly == NULL ||
        !write_text(marker, "valid previous build\n") ||
        !write_text(assembly, "previous generated assembly\n")) {
        failed = fail_test(name, "could not mark previous build");
        goto cleanup;
    }
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    if (!write_text(fixture.trace_path, "") ||
        setenv("SOLAR_FAKE_YANC_FAIL_STAGE", "appcomp", 1) != 0) {
        failed = fail_test(name, "could not configure fake failure");
        goto cleanup;
    }
    if (run_compile(&fixture, &project, &compiler_result, &result) ||
        result.status != SOLAR_STATUS_PROCESS_FAILED ||
        compiler_result.stage_count != 2U ||
        strcmp(compiler_result.stages[0].name, "cmmcomp") != 0 ||
        strcmp(compiler_result.stages[1].name, "appcomp") != 0 ||
        !regular_file_exists(marker)) {
        failed = fail_test(name, "failure replaced build or ran later stages");
        goto cleanup;
    }
    assembly_after = read_text(assembly);
    trace = read_text(fixture.trace_path);
    if (assembly_after == NULL ||
        strcmp(assembly_after, "previous generated assembly\n") != 0 ||
        trace == NULL || strstr(trace, "asmcomp") != NULL ||
        strstr(result.diagnostic.message, "appcomp") == NULL ||
        strstr(result.diagnostic.message, "23") == NULL) {
        failed = fail_test(name, "failure diagnostic or stage order was wrong");
    }

cleanup:
    (void)unsetenv("SOLAR_FAKE_YANC_FAIL_STAGE");
    free(assembly_after);
    free(assembly);
    free(trace);
    free(marker);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static int test_manual_assembly_blocks_publication(const char *tool_path)
{
    const char *name = "YANC manual Assembly publication conflict";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *software = NULL;
    char *assembly = NULL;
    char *after = NULL;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        goto cleanup;
    }
    software = join_path(fixture.project_root, "software");
    assembly = join_path(fixture.project_root, "software/processor.asm");
    if (software == NULL || assembly == NULL || !ensure_directory(software) ||
        !write_text(assembly, "manual assembly\n")) {
        failed = fail_test(name, "could not create manual Assembly");
        goto cleanup;
    }
    if (run_compile(&fixture, &project, &compiler_result, &result) ||
        result.status != SOLAR_STATUS_IO_ERROR ||
        strstr(result.diagnostic.message, "unregistered public file") == NULL) {
        failed = fail_test(name, "manual Assembly did not block publication");
        goto cleanup;
    }
    after = read_text(assembly);
    if (after == NULL || strcmp(after, "manual assembly\n") != 0) {
        failed = fail_test(name, "manual Assembly was modified");
    }

cleanup:
    free(after);
    free(assembly);
    free(software);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static int test_missing_artifact_preserves_previous_build(const char *tool_path)
{
    const char *name = "YANC missing artifact atomic publication";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *marker = NULL;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        goto cleanup;
    }
    if (!run_compile(&fixture, &project, &compiler_result, &result)) {
        failed = fail_test(
            name, result.diagnostic.message
        );
        goto cleanup;
    }
    marker = join_path(
        compiler_result.artifacts.build_directory, "previous-build.marker"
    );
    if (marker == NULL || !write_text(marker, "valid previous build\n")) {
        failed = fail_test(name, "could not mark previous build");
        goto cleanup;
    }
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    if (setenv("SOLAR_FAKE_YANC_OMIT", "inst", 1) != 0) {
        failed = fail_test(name, "could not configure missing artifact");
        goto cleanup;
    }
    if (run_compile(&fixture, &project, &compiler_result, &result) ||
        result.status != SOLAR_STATUS_PROCESS_FAILED ||
        compiler_result.stage_count != 3U || !regular_file_exists(marker) ||
        strstr(result.diagnostic.message, "instruction memory image") == NULL ||
        strstr(result.diagnostic.message, "asmcomp") == NULL) {
        failed = fail_test(name, "missing artifact replaced valid build");
    }

cleanup:
    (void)unsetenv("SOLAR_FAKE_YANC_OMIT");
    free(marker);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static int test_missing_hdl_preserves_previous_build(const char *tool_path)
{
    const char *name = "YANC missing HDL atomic publication";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *marker = NULL;
    char *hdl_directory = NULL;
    char *missing_hdl = NULL;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        goto cleanup;
    }
    if (!run_compile(&fixture, &project, &compiler_result, &result)) {
        failed = fail_test(name, result.diagnostic.message);
        goto cleanup;
    }
    marker = join_path(
        compiler_result.artifacts.build_directory, "previous-build.marker"
    );
    hdl_directory = join_path(fixture.toolchain_root, "HDL");
    if (hdl_directory != NULL) {
        missing_hdl = join_path(hdl_directory, "core.v");
    }
    if (marker == NULL || missing_hdl == NULL ||
        !write_text(marker, "valid previous build\n") ||
        unlink(missing_hdl) != 0) {
        failed = fail_test(name, "could not prepare missing HDL fixture");
        goto cleanup;
    }
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    if (run_compile(&fixture, &project, &compiler_result, &result) ||
        result.status != SOLAR_STATUS_PROCESS_FAILED ||
        !regular_file_exists(marker) ||
        strstr(result.diagnostic.message, "required YANC HDL module") == NULL) {
        failed = fail_test(name, "late HDL failure replaced valid build");
    }

cleanup:
    free(missing_hdl);
    free(hdl_directory);
    free(marker);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static size_t count_trace_stage(const char *trace, const char *stage)
{
    size_t count = 0U;
    size_t stage_length = strlen(stage);
    const char *line = trace;

    while (line != NULL && *line != '\0') {
        const char *next = strchr(line, '\n');

        if (strncmp(line, stage, stage_length) == 0 &&
            (line[stage_length] == '\t' || line[stage_length] == '\n' ||
             line[stage_length] == '\0')) {
            count++;
        }
        line = next == NULL ? NULL : next + 1;
    }
    return count;
}

static int test_build_context_reuses_generation(const char *tool_path)
{
    const char *name = "YANC build context generation reuse";
    static const char *const BACKENDS[] = {"iverilog", "vvp", "yosys"};
    TestFixture fixture;
    SolarBuildContext context;
    SolarResult result;
    char *backend_directory = NULL;
    char *saved_path = NULL;
    char *trace = NULL;
    size_t index;
    int failed = 0;

    solar_build_context_init(&context);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        goto cleanup;
    }
    backend_directory = join_path(fixture.base, "backend tools");
    saved_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (backend_directory == NULL || !ensure_directory(backend_directory)) {
        failed = fail_test(name, "could not create backend directory");
        goto cleanup;
    }
    for (index = 0U; index < sizeof(BACKENDS) / sizeof(BACKENDS[0]); index++) {
        char *destination = join_path(backend_directory, BACKENDS[index]);

        if (destination == NULL || !copy_executable(tool_path, destination)) {
            free(destination);
            failed = fail_test(name, "could not install fake backend");
            goto cleanup;
        }
        free(destination);
    }
    if (setenv("PATH", backend_directory, 1) != 0 ||
        !write_text(fixture.trace_path, "")) {
        failed = fail_test(name, "could not configure fake backends");
        goto cleanup;
    }
    result = solar_build_context_load(
        &context, fixture.project_root, "build full", NULL, false
    );
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_build_context_check(&context);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_build_context_rtl(&context);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_build_context_test(&context, NULL, false, false);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_build_context_synthesize(&context);
    }
    solar_build_context_finish(&context, result);
    trace = read_text(fixture.trace_path);
    if (result.status != SOLAR_STATUS_OK || trace == NULL ||
        count_trace_stage(trace, "cmmcomp") != 1U ||
        count_trace_stage(trace, "appcomp") != 1U ||
        count_trace_stage(trace, "asmcomp") != 1U ||
        count_trace_stage(trace, "iverilog") != 1U ||
        count_trace_stage(trace, "vvp") != 1U ||
        count_trace_stage(trace, "yosys") != 1U) {
        failed = fail_test(name, result.diagnostic.message[0] == '\0'
            ? "pipeline invocation count was wrong"
            : result.diagnostic.message);
    }

cleanup:
    if (saved_path != NULL) (void)setenv("PATH", saved_path, 1);
    else (void)unsetenv("PATH");
    free(trace);
    free(saved_path);
    solar_build_context_free(&context);
    if (backend_directory != NULL) {
        for (index = 0U;
             index < sizeof(BACKENDS) / sizeof(BACKENDS[0]);
             index++) {
            char *path = join_path(backend_directory, BACKENDS[index]);

            if (path != NULL) (void)unlink(path);
            free(path);
        }
        (void)rmdir(backend_directory);
    }
    free(backend_directory);
    fixture_free(&fixture);
    return failed;
}

static int test_native_path_limit_prevents_tool_execution(const char *tool_path)
{
    const char *name = "YANC fixed native path limit";
    static const char *const COMPONENT =
        "deep_project_path_component_used_to_exercise_bundled_yanc_limits";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *parent = NULL;
    char *moved_root = NULL;
    char *trace = NULL;
    size_t index;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        goto cleanup;
    }
    parent = strdup(fixture.base);
    for (index = 0U; parent != NULL && index < 17U; index++) {
        char *next = join_path(parent, COMPONENT);

        free(parent);
        parent = next;
        if (parent == NULL || !ensure_directory(parent)) break;
    }
    if (parent == NULL || index != 17U) {
        failed = fail_test(name, "could not create long project path");
        goto cleanup;
    }
    moved_root = join_path(parent, "project");
    if (moved_root == NULL || rename(fixture.project_root, moved_root) != 0) {
        failed = fail_test(name, "could not move fixture to long path");
        goto cleanup;
    }
    free(fixture.project_root);
    fixture.project_root = moved_root;
    moved_root = NULL;
    if (!write_text(fixture.trace_path, "")) {
        failed = fail_test(name, "could not reset fake-tool trace");
        goto cleanup;
    }
    if (run_compile(&fixture, &project, &compiler_result, &result) ||
        result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "too long") == NULL) {
        failed = fail_test(name, "long native path was not rejected safely");
        goto cleanup;
    }
    trace = read_text(fixture.trace_path);
    if (trace == NULL || trace[0] != '\0') {
        failed = fail_test(name, "a YANC executable ran after path rejection");
    }

cleanup:
    free(trace);
    free(moved_root);
    free(parent);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

static int test_waveform_layout_and_missing_helper(const char *tool_path)
{
    const char *name = "YANC GTKWave Assembly layout";
    TestFixture fixture;
    SolarProject project;
    SolarCompilerResult compiler_result;
    SolarResult result;
    char *waveform = NULL;
    char *layout = NULL;
    char *found_layout = NULL;
    char *layout_text = NULL;
    char *formatter = NULL;
    char *trace = NULL;
    char *tool_bin = NULL;
    char *saved_path = NULL;
    char *test_path = NULL;
    SolarTestResult test_result;
    int failed = 0;

    solar_project_init(&project);
    solar_compiler_result_init(&compiler_result);
    solar_test_result_init(&test_result);
    if (!fixture_create(&fixture, "cmm", "pt", tool_path)) {
        failed = fail_test(name, "could not create fixture");
        goto cleanup;
    }
    if (!run_compile(&fixture, &project, &compiler_result, &result)) {
        failed = fail_test(name, result.diagnostic.message);
        goto cleanup;
    }
    waveform = join_path(fixture.project_root, ".solar/tmp/manual-wave.vcd");
    if (waveform == NULL || !write_text(waveform, "fake VCD\n")) {
        failed = fail_test(name, "could not create waveform fixture");
        goto cleanup;
    }
    result = solar_compiler_prepare_waveform_layout(
        &project, &compiler_result.artifacts, "generated", waveform, &layout
    );
    layout_text = layout == NULL ? NULL : read_text(layout);
    trace = read_text(fixture.trace_path);
    if (result.status != SOLAR_STATUS_OK || layout_text == NULL ||
        strstr(layout_text, "{Assembly}") == NULL ||
        strstr(layout_text, "C+-") != NULL ||
        strstr(layout_text, "Variables") != NULL || trace == NULL ||
        !trace_stage_contains(trace, "gen_gtkw", "\t--assembly-only\t") ||
        !trace_stage_contains(
            trace, "gen_gtkw", "\t.solar/tmp/manual-wave.vcd\t"
        )) {
        failed = fail_test(name, "Assembly-only layout was not prepared");
        goto cleanup;
    }
    result = solar_compiler_find_waveform_layout(
        &project, "generated", waveform, &found_layout
    );
    if (result.status != SOLAR_STATUS_OK || found_layout == NULL ||
        strcmp(found_layout, layout) != 0) {
        failed = fail_test(name, "prepared layout was not discoverable");
        goto cleanup;
    }
    free(found_layout);
    found_layout = NULL;
    formatter = join_path(fixture.toolchain_root, "bin/gen_gtkw");
    if (formatter == NULL || unlink(formatter) != 0) {
        failed = fail_test(name, "could not remove optional formatter");
        goto cleanup;
    }
    free(layout_text);
    layout_text = NULL;
    result = solar_compiler_prepare_waveform_layout(
        &project, &compiler_result.artifacts, "generated", waveform,
        &layout_text
    );
    if (result.status != SOLAR_STATUS_TOOL_MISSING || layout_text != NULL ||
        layout == NULL || access(layout, F_OK) == 0) {
        failed = fail_test(
            name, "missing formatter did not remove the stale layout safely"
        );
        goto cleanup;
    }
    result = solar_compiler_find_waveform_layout(
        &project, "generated", waveform, &found_layout
    );
    if (result.status != SOLAR_STATUS_OK || found_layout != NULL) {
        failed = fail_test(name, "removed stale layout remained discoverable");
        goto cleanup;
    }
    tool_bin = join_path(fixture.toolchain_root, "bin");
    saved_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (tool_bin == NULL || saved_path == NULL) {
        failed = fail_test(name, "could not prepare simulation PATH");
        goto cleanup;
    }
    test_path = malloc(strlen(tool_bin) + strlen(saved_path) + 2U);
    if (test_path == NULL) {
        failed = fail_test(name, "could not allocate simulation PATH");
        goto cleanup;
    }
    (void)snprintf(
        test_path, strlen(tool_bin) + strlen(saved_path) + 2U,
        "%s:%s", tool_bin, saved_path
    );
    if (setenv("PATH", test_path, 1) != 0) {
        failed = fail_test(name, "could not select fake simulation tools");
        goto cleanup;
    }
    result = solar_test_run_with_artifacts(
        &project, "generated", NULL, &compiler_result.artifacts, &test_result
    );
    if (result.status != SOLAR_STATUS_OK || test_result.waveform_path == NULL ||
        test_result.waveform_layout_path != NULL ||
        test_result.waveform_layout_diagnostic.severity !=
            SOLAR_DIAGNOSTIC_WARNING ||
        strstr(
            test_result.waveform_layout_diagnostic.message, "gen_gtkw"
        ) == NULL) {
        failed = fail_test(
            name, "formatter fallback incorrectly failed the simulation"
        );
        (void)fprintf(
            stderr,
            "status=%d result=%s waveform=%s layout=%s warning=%d:%s\n",
            (int)result.status, result.diagnostic.message,
            test_result.waveform_path == NULL ? "(null)" :
                test_result.waveform_path,
            test_result.waveform_layout_path == NULL ? "(null)" :
                test_result.waveform_layout_path,
            (int)test_result.waveform_layout_diagnostic.severity,
            test_result.waveform_layout_diagnostic.message
        );
    }

cleanup:
    if (saved_path != NULL) (void)setenv("PATH", saved_path, 1);
    solar_test_result_free(&test_result);
    free(test_path);
    free(saved_path);
    free(tool_bin);
    free(trace);
    free(formatter);
    free(found_layout);
    free(layout_text);
    free(layout);
    free(waveform);
    solar_compiler_result_free(&compiler_result);
    solar_project_free(&project);
    fixture_free(&fixture);
    return failed;
}

int main(int argc, char *argv[])
{
    EnvironmentBackup environment;
    char *tool_path;
    int failures = 0;

    if (argc != 2) {
        return fail_test(
            "YANC compiler tests", "expected the fake YANC tool path"
        );
    }
    tool_path = realpath(argv[1], NULL);
    if (tool_path == NULL || !save_environment(&environment)) {
        free(tool_path);
        return fail_test(
            "YANC compiler tests", "could not prepare test environment"
        );
    }
    failures += test_cmm_pipeline(tool_path);
    failures += test_cpp_pipeline(tool_path);
    failures += test_assembly_pipeline(tool_path);
    failures += test_manual_assembly_blocks_publication(tool_path);
    failures += test_failure_preserves_previous_build(tool_path);
    failures += test_missing_artifact_preserves_previous_build(tool_path);
    failures += test_missing_hdl_preserves_previous_build(tool_path);
    failures += test_build_context_reuses_generation(tool_path);
    failures += test_native_path_limit_prevents_tool_execution(tool_path);
    failures += test_waveform_layout_and_missing_helper(tool_path);
    restore_environment(&environment);
    free(tool_path);
    return failures == 0 ? 0 : 1;
}
