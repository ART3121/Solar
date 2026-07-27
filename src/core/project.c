#include "solar/project.h"

#include "cmm_source_internal.h"

#include "solar/filesystem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char VERILOG_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "\n"
    "[project]\n"
    "name = \"counter\"\n"
    "language = \"verilog\"\n"
    "default_test = \"basic\"\n"
    "default_profile = \"debug\"\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "defines = [\"DEBUG\"]\n"
    "\n"
    "# Verilog files below rtl/ and tb/ are discovered automatically.\n";

static const char VERILOG_RTL[] =
    "module counter (\n"
    "    input wire clk,\n"
    "    input wire reset,\n"
    "    output reg [3:0] value\n"
    ");\n"
    "    always @(posedge clk) begin\n"
    "        if (reset)\n"
    "            value <= 4'b0000;\n"
    "        else\n"
    "            value <= value + 1'b1;\n"
    "    end\n"
    "endmodule\n";

static const char VERILOG_TESTBENCH[] =
    "`timescale 1ns/1ps\n"
    "module counter_tb;\n"
    "    reg clk = 1'b0;\n"
    "    reg reset = 1'b1;\n"
    "    wire [3:0] value;\n"
    "\n"
    "    counter dut (.clk(clk), .reset(reset), .value(value));\n"
    "\n"
    "    always #5 clk = ~clk;\n"
    "\n"
    "    initial begin\n"
    "        $dumpfile(\"basic.vcd\");\n"
    "        $dumpvars(0, counter_tb);\n"
    "        #12 reset = 1'b0;\n"
    "        #100;\n"
    "        $display(\"counter value: %0d\", value);\n"
    "        $finish;\n"
    "    end\n"
    "endmodule\n";

static const char YANC_CMM_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "\n"
    "[project]\n"
    "name = \"sapho-cmm\"\n"
    "language = \"cmm\"\n"
    "default_test = \"generated\"\n"
    "default_profile = \"debug\"\n"
    "\n"
    "[compiler]\n"
    "backend = \"yanc\"\n"
    "source = \"software/processor.cmm\"\n"
    "processor = \"sapho_demo\"\n"
    "frequency_mhz = 100\n"
    "simulation_clocks = 100000\n"
    "\n"
    "[yanc]\n"
    "diagnostics = \"pt\"\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"sapho_demo\"\n"
    "\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "defines = [\"YANC_DEBUG\"]\n";

static const char YANC_CPP_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "\n"
    "[project]\n"
    "name = \"sapho-cpp\"\n"
    "language = \"cpp\"\n"
    "default_test = \"generated\"\n"
    "default_profile = \"debug\"\n"
    "\n"
    "[compiler]\n"
    "backend = \"yanc\"\n"
    "source = \"software/processor.cpp\"\n"
    "processor = \"sapho_demo\"\n"
    "frequency_mhz = 100\n"
    "simulation_clocks = 100000\n"
    "include_dirs = [\"software/include\"]\n"
    "\n"
    "[yanc]\n"
    "diagnostics = \"pt\"\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"sapho_demo\"\n"
    "\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "defines = [\"YANC_DEBUG\"]\n";

static const char YANC_ASM_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "\n"
    "[project]\n"
    "name = \"sapho-assembly\"\n"
    "language = \"asm\"\n"
    "default_test = \"generated\"\n"
    "default_profile = \"debug\"\n"
    "\n"
    "[compiler]\n"
    "backend = \"yanc\"\n"
    "source = \"software/processor.asm\"\n"
    "processor = \"sapho_demo\"\n"
    "frequency_mhz = 100\n"
    "simulation_clocks = 100000\n"
    "\n"
    "[yanc]\n"
    "diagnostics = \"pt\"\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"sapho_demo\"\n"
    "\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "defines = [\"YANC_DEBUG\"]\n";

/* YANC fixture adaptations: copyright (c) 2026 NIPSCERN, MIT License. */
/* Adapted from YANC v5.2's CMM sw_test regression fixture. */
static const char YANC_CMM_SOURCE[] =
    "#PRNAME sapho_demo\n"
    "#NUBITS 16\n"
    "#NDSTAC 4\n"
    "#SDEPTH 4\n"
    "#NUIOIN 1\n"
    "#NUIOOU 1\n"
    "#NBMANT 10\n"
    "#NBEXPO 5\n"
    "#NUGAIN 128\n"
    "\n"
    "void main()\n"
    "{\n"
    "    int x;\n"
    "    int y;\n"
    "\n"
    "    while (1)\n"
    "    {\n"
    "        x = in(0);\n"
    "        switch (x)\n"
    "        {\n"
    "            case 1:\n"
    "                y = 10;\n"
    "                break;\n"
    "\n"
    "            case 2:\n"
    "                y = 20;\n"
    "                break;\n"
    "\n"
    "            default:\n"
    "                y = 0;\n"
    "        }\n"
    "\n"
    "        out(0, y);\n"
    "    }\n"
    "}\n";

/* Adapted from YANC v5.2's CPPComp test1 regression fixture. */
static const char YANC_CPP_SOURCE[] =
    "#pragma yanc prname sapho_demo\n"
    "#include \"solar_math.hpp\"\n"
    "\n"
    "void main(void) {\n"
    "    int s = 0;\n"
    "    for (int i = 1; i <= 10; i = i + 1) s = add(s, i);\n"
    "    out(0, s);\n"
    "    out(0, sizeof(int));\n"
    "}\n";

static const char YANC_CPP_HEADER[] =
    "#pragma once\n"
    "\n"
    "int add(int a, int b) { return a + b; }\n";

/* Generated by YANC v5.2 from its CPPComp test1 regression fixture. */
static const char YANC_ASM_SOURCE[] =
    "NOP\n"
    "#PRNAME sapho_demo\n"
    "#NUBITS 32\n"
    "#NDSTAC 128\n"
    "#SDEPTH 128\n"
    "#NUIOIN 1\n"
    "#NUIOOU 1\n"
    "#NBMANT 23\n"
    "#NBEXPO 8\n"
    "#NUGAIN 128\n"
    "JMP main\n"
    "@add NOP\n"
    "POP\n"
    "SET_P add_b\n"
    "SET add_a\n"
    "ADD add_b\n"
    "RET\n"
    "RET\n"
    "@main NOP\n"
    "LOD 0\n"
    "SET main_s\n"
    "LOD 1\n"
    "SET main_i\n"
    "@Lfor_top1 NOP\n"
    "LOD main_i\n"
    "P_LOD 10\n"
    "S_GRE\n"
    "LIN\n"
    "JIZ Lfor_end3\n"
    "LOD main_s\n"
    "P_LOD main_i\n"
    "PSH\n"
    "CAL add\n"
    "SET main_s\n"
    "@Lfor_cont2 NOP\n"
    "LOD main_i\n"
    "P_LOD 1\n"
    "S_ADD\n"
    "SET main_i\n"
    "JMP Lfor_top1\n"
    "@Lfor_end3 NOP\n"
    "LOD main_s\n"
    "OUT 0\n"
    "LOD 1\n"
    "OUT 0\n"
    "@fim JMP fim\n";

#define TEMPLATE_DIRECTORY_LIMIT 4U
#define TEMPLATE_FILE_LIMIT 8U

static const char VERILOG_GITIGNORE[] =
    ".solar/\n\n"
    "sim/*\n"
    "!sim/.gitkeep\n\n"
    "synth/*\n"
    "!synth/.gitkeep\n";

static const char YANC_GITIGNORE[] =
    ".solar/\n\n"
    "hardware/*\n"
    "!hardware/.gitkeep\n\n"
    "simulation/*\n"
    "!simulation/.gitkeep\n";

static const char VERILOG_README[] =
    "# Solar Verilog project\n\n"
    "Edit RTL in `rtl/` and testbenches in `tb/`. Run `solar check`, "
    "`solar build sim` and `solar build synth`. Generated waveforms are "
    "published in `sim/`; synthesis results are published in `synth/`.\n";

static const char YANC_README[] =
    "# Solar SAPHO project\n\n"
    "Edit the processor source in `software/`. Run `solar check`, "
    "`solar build rtl`, `solar build sim`, and `solar build synth`. Generated hardware is "
    "published in `hardware/`; generated testbenches and waveforms are "
    "published in `simulation/`.\n";

typedef struct ProjectTemplate {
    const char *name;
    const char *directories[TEMPLATE_DIRECTORY_LIMIT];
    const char *file_paths[TEMPLATE_FILE_LIMIT];
    const char *file_contents[TEMPLATE_FILE_LIMIT];
} ProjectTemplate;

static const ProjectTemplate PROJECT_TEMPLATES[] = {
    {
        "verilog",
        {"rtl", "tb", "sim", "synth"},
        {
            "solar.toml", "rtl/counter.v", "tb/basic.v", "sim/.gitkeep",
            "synth/.gitkeep", ".gitignore", "README.md", NULL
        },
        {
            VERILOG_MANIFEST, VERILOG_RTL, VERILOG_TESTBENCH, "", "",
            VERILOG_GITIGNORE, VERILOG_README, NULL
        }
    },
    {
        "yanc-cmm",
        {"software", "hardware", "simulation", NULL},
        {
            "solar.toml", "software/processor.cmm", "hardware/.gitkeep",
            "simulation/.gitkeep", ".gitignore", "README.md", NULL, NULL
        },
        {
            YANC_CMM_MANIFEST, YANC_CMM_SOURCE, "", "", YANC_GITIGNORE,
            YANC_README, NULL, NULL
        }
    },
    {
        "yanc-cpp",
        {"software", "software/include", "hardware", "simulation"},
        {
            "solar.toml", "software/processor.cpp",
            "software/include/solar_math.hpp", "hardware/.gitkeep",
            "simulation/.gitkeep", ".gitignore", "README.md", NULL
        },
        {
            YANC_CPP_MANIFEST, YANC_CPP_SOURCE, YANC_CPP_HEADER, "", "",
            YANC_GITIGNORE, YANC_README, NULL
        }
    },
    {
        "yanc-asm",
        {"software", "hardware", "simulation", NULL},
        {
            "solar.toml", "software/processor.asm", "hardware/.gitkeep",
            "simulation/.gitkeep", ".gitignore", "README.md", NULL, NULL
        },
        {
            YANC_ASM_MANIFEST, YANC_ASM_SOURCE, "", "", YANC_GITIGNORE,
            YANC_README, NULL, NULL
        }
    }
};

static SolarResult project_error(
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

void solar_project_init(SolarProject *project)
{
    if (project == NULL) {
        return;
    }
    project->root = NULL;
    project->manifest_path = NULL;
    project->legacy_build_layout_present = false;
    solar_config_init(&project->config);
    solar_discovery_report_init(&project->discovery);
}

void solar_project_free(SolarProject *project)
{
    if (project == NULL) {
        return;
    }
    free(project->root);
    free(project->manifest_path);
    solar_config_free(&project->config);
    solar_project_init(project);
}

static void move_to_parent(char *path)
{
    char *separator;
    size_t length;

    if (path == NULL) {
        return;
    }
    length = strlen(path);

    while (length > 1U && path[length - 1U] == '/') {
        path[--length] = '\0';
    }
    separator = strrchr(path, '/');
    if (separator == path) {
        path[1] = '\0';
    } else if (separator != NULL) {
        *separator = '\0';
    }
}

static SolarResult canonical_start_directory(
    const char *start_path,
    char **directory_out
)
{
    struct stat information;
    char *resolved;

    *directory_out = NULL;
    resolved = realpath(start_path, NULL);
    if (resolved == NULL) {
        return project_error(
            errno == ENOENT ? SOLAR_STATUS_NOT_FOUND : SOLAR_STATUS_IO_ERROR,
            "start from an existing project directory",
            "cannot resolve project search path %s: %s",
            start_path,
            strerror(errno)
        );
    }
    if (stat(resolved, &information) != 0) {
        SolarResult result = project_error(
            SOLAR_STATUS_IO_ERROR,
            "check permissions and try again",
            "cannot inspect %s: %s",
            resolved,
            strerror(errno)
        );
        free(resolved);
        return result;
    }
    if (!S_ISDIR(information.st_mode)) {
        move_to_parent(resolved);
    }
    *directory_out = resolved;
    return solar_result_ok();
}

static SolarResult canonical_existing_directory(
    const char *path,
    char **directory_out
)
{
    struct stat information;
    char *resolved;

    *directory_out = NULL;
    resolved = realpath(path, NULL);
    if (resolved == NULL) {
        return project_error(
            errno == ENOENT ? SOLAR_STATUS_NOT_FOUND : SOLAR_STATUS_IO_ERROR,
            "choose an existing writable directory",
            "cannot resolve initialization directory %s: %s",
            path,
            strerror(errno)
        );
    }
    if (stat(resolved, &information) != 0 || !S_ISDIR(information.st_mode)) {
        SolarResult result = project_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "pass a directory to solar init",
            "initialization target is not a directory: %s",
            path
        );
        free(resolved);
        return result;
    }
    *directory_out = resolved;
    return solar_result_ok();
}

SolarResult solar_project_find_root(const char *start_path, char **root_out)
{
    SolarResult result;
    char *directory = NULL;

    if (root_out != NULL) {
        *root_out = NULL;
    }
    if (start_path == NULL || root_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot discover a project without a start path",
            "provide an existing file or directory"
        );
    }

    result = canonical_start_directory(start_path, &directory);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    if (directory == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "project search succeeded without returning a directory",
            "report this Solar bug"
        );
    }

    for (;;) {
        char *manifest_path = NULL;
        struct stat information;
        struct stat target_information;

        result = solar_filesystem_join(directory, "solar.toml", &manifest_path);
        if (result.status != SOLAR_STATUS_OK) {
            free(directory);
            return result;
        }
        if (lstat(manifest_path, &information) == 0) {
            bool regular_manifest = S_ISREG(information.st_mode);

            if (S_ISLNK(information.st_mode) &&
                stat(manifest_path, &target_information) == 0) {
                regular_manifest = S_ISREG(target_information.st_mode);
            }
            if (regular_manifest) {
                free(manifest_path);
                *root_out = directory;
                return solar_result_ok();
            }
            result = project_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "replace the conflicting path with a readable solar.toml file",
                "project marker is not a regular file: %s",
                manifest_path
            );
            free(manifest_path);
            free(directory);
            return result;
        }
        if (errno != ENOENT && errno != ENOTDIR) {
            int saved_errno = errno;
            free(manifest_path);
            result = project_error(
                SOLAR_STATUS_IO_ERROR,
                "check directory permissions and try again",
                "cannot inspect %s: %s",
                directory,
                strerror(saved_errno)
            );
            free(directory);
            return result;
        }
        free(manifest_path);

        if (strcmp(directory, "/") == 0) {
            break;
        }
        move_to_parent(directory);
    }

    result = project_error(
        SOLAR_STATUS_NOT_FOUND,
        "run solar init or change to a directory below a Solar project",
        "no solar.toml found from %s to the filesystem root",
        start_path
    );
    free(directory);
    return result;
}

SolarResult solar_project_load(const char *start_path, SolarProject *project)
{
    SolarResult result;

    if (project != NULL) {
        solar_project_init(project);
    }
    if (project == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot load a project into null storage",
            "provide a SolarProject output value"
        );
    }
    result = solar_project_find_root(start_path, &project->root);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    result = solar_filesystem_join(
        project->root, "solar.toml", &project->manifest_path
    );
    if (result.status != SOLAR_STATUS_OK) {
        solar_project_free(project);
        return result;
    }
    result = solar_config_parse_file(project->manifest_path, &project->config);
    if (result.status == SOLAR_STATUS_OK) {
        char *legacy_path = NULL;
        struct stat information;

        result = solar_filesystem_join(
            project->root, "solar-build", &legacy_path
        );
        if (result.status == SOLAR_STATUS_OK &&
            lstat(legacy_path, &information) == 0) {
            project->legacy_build_layout_present = true;
        } else if (result.status == SOLAR_STATUS_OK && errno != ENOENT) {
            result = project_error(
                SOLAR_STATUS_IO_ERROR,
                "check permissions on the legacy solar-build path",
                "cannot inspect legacy layout %s: %s",
                legacy_path, strerror(errno)
            );
        }
        free(legacy_path);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_discovery_apply(project, &project->discovery);
    }
    if (result.status != SOLAR_STATUS_OK) {
        solar_project_free(project);
    }
    return result;
}

SolarResult solar_project_resolve_path(
    const SolarProject *project,
    const char *relative_path,
    char **path_out
)
{
    if (path_out != NULL) {
        *path_out = NULL;
    }
    if (project == NULL || project->root == NULL || relative_path == NULL ||
        path_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot resolve a path without a loaded project",
            "load the project and provide a relative path"
        );
    }
    if (!solar_filesystem_is_safe_relative(relative_path)) {
        return project_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "use a relative path without empty, '.' or '..' components",
            "unsafe project-relative path: %s",
            relative_path
        );
    }
    return solar_filesystem_join(project->root, relative_path, path_out);
}

static SolarResult validate_paths(
    const SolarProject *project,
    const SolarStringList *files,
    const char *owner,
    const char *key,
    bool require_directory
)
{
    size_t index;

    for (index = 0U; index < files->count; index++) {
        char *path = NULL;
        struct stat information;
        int stat_result;
        SolarResult result = solar_project_resolve_path(
            project, files->items[index], &path
        );

        if (result.status != SOLAR_STATUS_OK) {
            return result;
        }
        if (path == NULL) {
            return solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "path resolution succeeded without returning a path",
                "report this Solar bug"
            );
        }
        stat_result = stat(path, &information);
        if (stat_result != 0 ||
            (require_directory
                ? !S_ISDIR(information.st_mode)
                : !S_ISREG(information.st_mode)) ||
            access(path, require_directory ? (R_OK | X_OK) : R_OK) != 0) {
            int saved_errno = stat_result != 0 ? errno : 0;
            SolarStatus status = stat_result != 0 && saved_errno != ENOENT
                ? SOLAR_STATUS_IO_ERROR
                : SOLAR_STATUS_CONFIG_ERROR;
            result = project_error(
                status,
                require_directory
                    ? "correct the path or create the referenced include directory"
                    : "correct the path or create the referenced source file",
                "%s: %s.%s[%zu] does not name a readable %s: %s",
                project->manifest_path,
                owner,
                key,
                index,
                require_directory ? "directory" : "file",
                files->items[index]
            );
            free(path);
            return result;
        }
        free(path);
    }
    return solar_result_ok();
}

static SolarResult validate_single_file(
    const SolarProject *project,
    const char *relative_path,
    const char *owner,
    const char *key
)
{
    SolarStringList list;

    list.items = (char **)&relative_path;
    list.count = relative_path == NULL ? 0U : 1U;
    return validate_paths(project, &list, owner, key, false);
}

static bool has_suffix(const char *value, const char *suffix)
{
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);

    return value_length >= suffix_length &&
        strcmp(value + value_length - suffix_length, suffix) == 0;
}

static SolarResult validate_compiler_extension(const SolarProject *project)
{
    const char *language = project->config.project.language;
    const char *source = project->config.compiler.source;
    bool valid = true;

    if (source == NULL || language == NULL || strcmp(language, "verilog") == 0) {
        return solar_result_ok();
    }
    if (strcmp(language, "cmm") == 0) {
        valid = has_suffix(source, ".cmm");
    } else if (strcmp(language, "cpp") == 0) {
        valid = has_suffix(source, ".cpp") || has_suffix(source, ".cc") ||
            has_suffix(source, ".cxx");
    } else if (strcmp(language, "asm") == 0) {
        valid = has_suffix(source, ".asm");
    }
    if (!valid) {
        return project_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "use .cmm for cmm, .cpp/.cc/.cxx for cpp, or .asm for asm",
            "%s: [compiler].source extension is incompatible with language %s: %s",
            project->manifest_path,
            language,
            source
        );
    }
    return solar_result_ok();
}

static SolarResult validate_cmm_processor_identity(const SolarProject *project)
{
    char *source_path = NULL;
    char *declared_processor = NULL;
    SolarResult result;

    if (project->config.project.language == NULL ||
        strcmp(project->config.project.language, "cmm") != 0) {
        return solar_result_ok();
    }
    result = solar_project_resolve_path(
        project, project->config.compiler.source, &source_path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_cmm_read_processor_name(source_path, &declared_processor);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (strcmp(declared_processor, project->config.compiler.processor) != 0) {
        result = project_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "run solar scan to synchronize the CMM processor identity",
            "%s: [compiler].processor '%s' does not match #PRNAME '%s' in %s",
            project->manifest_path,
            project->config.compiler.processor,
            declared_processor,
            project->config.compiler.source
        );
    }

cleanup:
    free(declared_processor);
    free(source_path);
    return result;
}

static SolarResult validate_profile_and_test_paths(const SolarProject *project)
{
    size_t index;

    for (index = 0U; index < project->config.profile_count; index++) {
        SolarResult result = validate_paths(
            project, &project->config.profiles[index].include_dirs,
            "profile", "include_dirs", true
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    for (index = 0U; index < project->config.test_count; index++) {
        const SolarTest *test = &project->config.tests[index];
        SolarResult result;

        if (test->kind == SOLAR_TEST_VERILOG) {
            result = validate_paths(
                project, &test->sources, "test", "sources", false
            );
            if (result.status != SOLAR_STATUS_OK) return result;
        }
        if (test->cocotb != NULL) {
            SolarStringList module_path = {
                .items = (char **)&test->cocotb,
                .count = 1U
            };

            result = validate_paths(
                project, &module_path, "test", "cocotb", false
            );
            if (result.status != SOLAR_STATUS_OK) return result;
        }
        result = validate_paths(
            project, &test->include_dirs, "test", "include_dirs", true
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

SolarResult solar_project_validate(const SolarProject *project)
{
    SolarResult result;

    if (project == NULL || project->root == NULL || project->manifest_path == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot validate an unloaded project",
            "load the project before validating it"
        );
    }
    result = solar_config_validate(&project->config, project->manifest_path);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    result = validate_paths(
        project, &project->config.sources.rtl, "sources", "rtl", false
    );
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    if (project->config.manifest_format == 1U) {
        result = validate_paths(
            project, &project->config.sources.testbench,
            "sources", "testbench", false
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    result = validate_paths(
        project, &project->config.sources.include_dirs,
        "sources", "include_dirs", true
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_paths(
        project, &project->config.compiler.include_dirs,
        "compiler", "include_dirs", true
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_single_file(
        project, project->config.compiler.source, "compiler", "source"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_compiler_extension(project);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_cmm_processor_identity(project);
    if (result.status != SOLAR_STATUS_OK) return result;
    return validate_profile_and_test_paths(project);
}

static SolarResult ensure_directory(const char *path, bool *created)
{
    struct stat information;

    *created = false;
    if (lstat(path, &information) == 0) {
        if (S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode)) {
            return solar_result_ok();
        }
        return project_error(
            SOLAR_STATUS_IO_ERROR,
            "remove or rename the conflicting path",
            "cannot create directory because a path already exists: %s",
            path
        );
    }
    if (errno != ENOENT) {
        return project_error(
            SOLAR_STATUS_IO_ERROR,
            "check directory permissions and try again",
            "cannot inspect %s: %s",
            path,
            strerror(errno)
        );
    }
    if (mkdir(path, 0755) != 0) {
        return project_error(
            SOLAR_STATUS_IO_ERROR,
            "check directory permissions and try again",
            "cannot create %s: %s",
            path,
            strerror(errno)
        );
    }
    *created = true;
    return solar_result_ok();
}

static SolarResult write_new_file(const char *path, const char *content)
{
    int descriptor;
    size_t remaining = strlen(content);
    const char *cursor = content;

    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        return project_error(
            SOLAR_STATUS_IO_ERROR,
            "remove or rename the conflicting file, then run solar init again",
            "cannot create %s: %s",
            path,
            strerror(errno)
        );
    }

    while (remaining > 0U) {
        ssize_t written = write(descriptor, cursor, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            int saved_errno = errno;
            (void)close(descriptor);
            (void)unlink(path);
            return project_error(
                SOLAR_STATUS_IO_ERROR,
                "check available storage and try again",
                "cannot write %s: %s",
                path,
                strerror(saved_errno)
            );
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    if (close(descriptor) != 0) {
        int saved_errno = errno;
        (void)unlink(path);
        return project_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and try again",
            "cannot finish %s: %s",
            path,
            strerror(saved_errno)
        );
    }
    return solar_result_ok();
}

static SolarResult reject_existing_file(const char *path)
{
    struct stat information;

    if (lstat(path, &information) == 0) {
        return project_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "choose an empty directory; Solar never overwrites project files",
            "refusing to overwrite existing path: %s",
            path
        );
    }
    if (errno != ENOENT) {
        return project_error(
            SOLAR_STATUS_IO_ERROR,
            "check directory permissions and try again",
            "cannot inspect %s: %s",
            path,
            strerror(errno)
        );
    }
    return solar_result_ok();
}

static const ProjectTemplate *find_project_template(const char *name)
{
    size_t index;

    for (index = 0U;
         index < sizeof(PROJECT_TEMPLATES) / sizeof(PROJECT_TEMPLATES[0]);
         index++) {
        if (strcmp(PROJECT_TEMPLATES[index].name, name) == 0) {
            return &PROJECT_TEMPLATES[index];
        }
    }
    return NULL;
}

SolarResult solar_project_initialize_template(
    const char *directory,
    const char *template_name
)
{
    const ProjectTemplate *project_template;
    char *root = NULL;
    char *directory_paths[TEMPLATE_DIRECTORY_LIMIT] = {NULL};
    char *file_paths[TEMPLATE_FILE_LIMIT] = {NULL};
    bool directories_created[TEMPLATE_DIRECTORY_LIMIT] = {false};
    bool files_created[TEMPLATE_FILE_LIMIT] = {false};
    size_t index;
    SolarResult result;

    if (directory == NULL || template_name == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot initialize a project without a directory and template",
            "provide a directory and one of: verilog, yanc-cmm, yanc-cpp, yanc-asm"
        );
    }
    project_template = find_project_template(template_name);
    if (project_template == NULL) {
        return project_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "use one of: verilog, yanc-cmm, yanc-cpp, yanc-asm",
            "unknown project template: %s",
            template_name
        );
    }
    result = canonical_existing_directory(directory, &root);
    if (result.status != SOLAR_STATUS_OK) {
        goto cleanup;
    }

    for (index = 0U; index < TEMPLATE_DIRECTORY_LIMIT; index++) {
        const char *relative = project_template->directories[index];

        if (relative == NULL) continue;
        result = solar_filesystem_join(root, relative, &directory_paths[index]);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    for (index = 0U; index < TEMPLATE_FILE_LIMIT; index++) {
        const char *relative = project_template->file_paths[index];

        if (relative == NULL) continue;
        result = solar_filesystem_join(root, relative, &file_paths[index]);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        result = reject_existing_file(file_paths[index]);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    for (index = 0U; index < TEMPLATE_DIRECTORY_LIMIT; index++) {
        if (directory_paths[index] == NULL) continue;
        result = ensure_directory(
            directory_paths[index], &directories_created[index]
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    for (index = 0U; index < TEMPLATE_FILE_LIMIT; index++) {
        if (file_paths[index] == NULL) continue;
        result = write_new_file(
            file_paths[index], project_template->file_contents[index]
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        files_created[index] = true;
    }

cleanup:
    if (result.status != SOLAR_STATUS_OK) {
        for (index = TEMPLATE_FILE_LIMIT; index > 0U; index--) {
            if (files_created[index - 1U]) {
                (void)unlink(file_paths[index - 1U]);
            }
        }
        for (index = TEMPLATE_DIRECTORY_LIMIT; index > 0U; index--) {
            if (directories_created[index - 1U]) {
                (void)rmdir(directory_paths[index - 1U]);
            }
        }
    }
    for (index = 0U; index < TEMPLATE_FILE_LIMIT; index++) {
        free(file_paths[index]);
    }
    for (index = 0U; index < TEMPLATE_DIRECTORY_LIMIT; index++) {
        free(directory_paths[index]);
    }
    free(root);
    return result;
}

SolarResult solar_project_initialize(const char *directory)
{
    return solar_project_initialize_template(directory, "verilog");
}
