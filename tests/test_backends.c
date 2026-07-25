#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/backend.h"
#include "solar/filesystem.h"
#include "solar/project.h"
#include "solar/rtl.h"
#include "solar/synthesis.h"
#include "solar/test.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failure(const char *message)
{
    (void)fprintf(stderr, "backend test: %s\n", message);
    return 1;
}

static char *join_path(const char *left, const char *right)
{
    size_t capacity = strlen(left) + strlen(right) + 2U;
    char *path = malloc(capacity);

    if (path != NULL) (void)snprintf(path, capacity, "%s/%s", left, right);
    return path;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "r");
    long length;
    char *contents;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) (void)fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    contents = malloc((size_t)length + 1U);
    if (contents == NULL) {
        (void)fclose(file);
        return NULL;
    }
    if (fread(contents, 1U, (size_t)length, file) != (size_t)length) {
        free(contents);
        (void)fclose(file);
        return NULL;
    }
    contents[length] = '\0';
    if (fclose(file) != 0) {
        free(contents);
        return NULL;
    }
    return contents;
}

static void remove_initialized_project(const char *root)
{
    const char *files[] = {
        "solar.toml", "rtl/counter.v", "rtl/counter.sv", "tb/basic.v"
    };
    const char *directories[] = {"rtl", "tb"};
    size_t index;
    bool removed = false;

    if (root == NULL) return;
    (void)solar_filesystem_clean_project(root, &removed);
    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        char *path = join_path(root, files[index]);
        if (path != NULL) (void)unlink(path);
        free(path);
    }
    for (index = 0U; index < sizeof(directories) / sizeof(directories[0]); index++) {
        char *path = join_path(root, directories[index]);
        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    (void)rmdir(root);
}

static int create_tool_links(const char *directory, const char *helper_path)
{
    const char *tools[] = {"iverilog", "vvp", "yosys"};
    size_t index;

    for (index = 0U; index < sizeof(tools) / sizeof(tools[0]); index++) {
        char *path = join_path(directory, tools[index]);
        int link_result = path == NULL ? -1 : symlink(helper_path, path);

        free(path);
        if (link_result != 0) return -1;
    }
    return 0;
}

static void remove_tool_links(const char *directory)
{
    const char *tools[] = {"iverilog", "vvp", "yosys"};
    size_t index;

    if (directory == NULL) return;
    for (index = 0U; index < sizeof(tools) / sizeof(tools[0]); index++) {
        char *path = join_path(directory, tools[index]);
        if (path != NULL) (void)unlink(path);
        free(path);
    }
    (void)rmdir(directory);
}

static bool contains_argument_line(
    const char *record,
    const char *tool,
    const char *required
)
{
    const char *line = record;
    size_t tool_length = strlen(tool);

    while (line != NULL && *line != '\0') {
        const char *line_end = strchr(line, '\n');
        const char *match;

        if (line_end == NULL) line_end = line + strlen(line);
        if (strncmp(line, tool, tool_length) == 0 && line[tool_length] == '\t') {
            match = strstr(line, required);
            if (match != NULL && match < line_end) return true;
        }
        line = *line_end == '\0' ? NULL : line_end + 1;
    }
    return false;
}

int main(int argc, char **argv)
{
    char project_template[] = "/tmp/solar backend-project-XXXXXX";
    char tools_template[] = "/tmp/solar-backend-tools-XXXXXX";
    char *project_root = NULL;
    char *tool_directory = NULL;
    char *record_path = NULL;
    char *script_path = NULL;
    char *record = NULL;
    char *script = NULL;
    char *old_path = NULL;
    SolarProject project;
    SolarRtlBuildResult rtl_result;
    SolarTestResult test_result;
    SolarSynthesisResult synthesis_result;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    solar_rtl_build_result_init(&rtl_result);
    solar_test_result_init(&test_result);
    solar_synthesis_result_init(&synthesis_result);
    if (argc != 2) return failure("expected backend_helper path");
    project_root = mkdtemp(project_template);
    tool_directory = mkdtemp(tools_template);
    if (project_root == NULL || tool_directory == NULL) {
        failed = failure("mkdtemp failed");
        goto cleanup;
    }
    record_path = join_path(project_root, "backend-argv.log");
    script_path = join_path(project_root, ".solar/tmp/synth/synthesis.ys");
    old_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (record_path == NULL || script_path == NULL ||
        create_tool_links(tool_directory, argv[1]) != 0 ||
        setenv("PATH", tool_directory, 1) != 0 ||
        setenv("SOLAR_BACKEND_RECORD", record_path, 1) != 0) {
        failed = failure("could not prepare fake backend tools");
        goto cleanup;
    }
    result = solar_project_initialize(project_root);
    if (result.status == SOLAR_STATUS_OK) {
        char *verilog = join_path(project_root, "rtl/counter.v");
        char *system_verilog = join_path(project_root, "rtl/counter.sv");

        if (verilog == NULL || system_verilog == NULL ||
            rename(verilog, system_verilog) != 0) {
            result = solar_result_error(
                SOLAR_STATUS_IO_ERROR,
                "could not prepare SystemVerilog backend fixture", ""
            );
        }
        free(system_verilog);
        free(verilog);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_load(project_root, &project);
    }
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_rtl_build(&project, NULL, &rtl_result);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_test_run(&project, NULL, NULL, &test_result);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_synthesis_run(
            &project, NULL, &synthesis_result
        );
    }
    if (result.status != SOLAR_STATUS_OK) {
        failed = failure(result.diagnostic.message);
        goto cleanup;
    }

    record = read_file(record_path);
    script = read_file(script_path);
    if (record == NULL || script == NULL ||
        !contains_argument_line(record, "iverilog", "\t-g2012\t") ||
        !contains_argument_line(record, "iverilog", "\t-tnull\t-s\tcounter\t") ||
        !contains_argument_line(record, "iverilog", "/rtl/counter.sv") ||
        !contains_argument_line(record, "iverilog", "/tb/basic.v") ||
        !contains_argument_line(
            record, "vvp", "/.solar/tmp/tests/basic/simulation.vvp"
        ) ||
        !contains_argument_line(record, "yosys", "/.solar/tmp/synth/synthesis.ys") ||
        strstr(script, "read_verilog -sv ") == NULL ||
        strstr(script, "/rtl/counter.sv") == NULL ||
        strstr(script, "tee -o statistics.txt") == NULL ||
        strstr(script, "write_verilog -noattr netlist.v") == NULL ||
        strstr(script, "tb/basic.v") != NULL ||
        strstr(script, project_root) == NULL) {
        (void)fprintf(
            stderr,
            "recorded arguments:\n%s\ngenerated script:\n%s\n",
            record == NULL ? "(null)" : record,
            script == NULL ? "(null)" : script
        );
        failed = failure("backend arguments or RTL-only Yosys script were incorrect");
    }
    if (!synthesis_result.statistics.available ||
        synthesis_result.statistics_result.status != SOLAR_STATUS_OK ||
        synthesis_result.statistics.modules != 1U ||
        synthesis_result.statistics.wires != 3U ||
        synthesis_result.statistics.cells != 2U ||
        synthesis_result.statistics.cell_type_count != 2U ||
        synthesis_result.statistics.tool_version == NULL ||
        strstr(synthesis_result.statistics.tool_version, "Yosys 0.test") == NULL ||
        synthesis_result.statistics.report_path == NULL ||
        strcmp(
            synthesis_result.statistics.report_path,
            synthesis_result.report_path
        ) != 0) {
        failed = failure("Yosys Generic Synthesis Statistics were not collected");
    }
    if (setenv("SOLAR_BACKEND_BAD_STATISTICS", "1", 1) != 0) {
        failed = failure("could not configure malformed Yosys statistics");
    } else {
        solar_synthesis_result_free(&synthesis_result);
        result = solar_synthesis_run(&project, NULL, &synthesis_result);
        (void)unsetenv("SOLAR_BACKEND_BAD_STATISTICS");
        if (result.status != SOLAR_STATUS_OK ||
            synthesis_result.statistics.available ||
            synthesis_result.statistics_result.status !=
                SOLAR_STATUS_CONFIG_ERROR ||
            synthesis_result.netlist_path == NULL ||
            synthesis_result.report_path == NULL) {
            failed = failure(
                "optional statistics parsing incorrectly failed synthesis"
            );
        }
    }

cleanup:
    if (old_path != NULL) (void)setenv("PATH", old_path, 1);
    else (void)unsetenv("PATH");
    (void)unsetenv("SOLAR_BACKEND_RECORD");
    (void)unsetenv("SOLAR_BACKEND_BAD_STATISTICS");
    solar_synthesis_result_free(&synthesis_result);
    solar_rtl_build_result_free(&rtl_result);
    solar_test_result_free(&test_result);
    solar_project_free(&project);
    free(script);
    free(record);
    if (record_path != NULL) (void)unlink(record_path);
    remove_initialized_project(project_root);
    remove_tool_links(tool_directory);
    free(old_path);
    free(script_path);
    free(record_path);
    return failed;
}
