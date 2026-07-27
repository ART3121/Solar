#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"
#include "solar/runner.h"
#include "solar/solar.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char *join_path(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    char *path = malloc(directory_length + name_length + 2U);

    if (path == NULL) {
        return NULL;
    }
    (void)snprintf(
        path,
        directory_length + name_length + 2U,
        "%s/%s",
        directory,
        name
    );
    return path;
}

static bool text_contains(const char *text, const char *fragment)
{
    return fragment == NULL || (text != NULL && strstr(text, fragment) != NULL);
}

static bool is_regular_file(const char *path)
{
    struct stat information;

    return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static bool path_is_missing(const char *path)
{
    struct stat information;

    return lstat(path, &information) != 0 && errno == ENOENT;
}

static bool wait_for_file(const char *path)
{
    int attempt;

    for (attempt = 0; attempt < 100; attempt++) {
        const struct timespec delay = {0, 10000000L};
        struct stat information;

        /* The detached helper creates the file before stdio flushes its
         * contents. Wait for observable output before unlinking it. */
        if (stat(path, &information) == 0 &&
            S_ISREG(information.st_mode) && information.st_size > 0) return true;
        (void)nanosleep(&delay, NULL);
    }
    {
        struct stat information;

        return stat(path, &information) == 0 &&
            S_ISREG(information.st_mode) && information.st_size > 0;
    }
}

static int write_text_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return -1;
    }
    if (fputs(content, file) == EOF) {
        (void)fclose(file);
        return -1;
    }
    return fclose(file);
}

static bool file_contains(const char *path, const char *fragment)
{
    FILE *file = fopen(path, "r");
    char line[1024];
    bool found = false;

    if (file == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, fragment) != NULL) {
            found = true;
            break;
        }
    }
    (void)fclose(file);
    return found;
}

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static int run_arguments_and_expect_output(
    const char *test_name,
    const char *solar_path,
    const char *working_directory,
    const char *const arguments[],
    int expected_exit,
    const char *stdout_fragment,
    const char *stderr_fragment,
    const char *forbidden_stdout_fragment
)
{
    SolarProcessSpec specification = {
        solar_path, arguments, working_directory, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;
    bool expected_runner_status;
    int failed = 0;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    expected_runner_status = expected_exit == 0
        ? result.status == SOLAR_STATUS_OK
        : result.status == SOLAR_STATUS_PROCESS_FAILED;
    if (!expected_runner_status || process.outcome != SOLAR_PROCESS_EXITED ||
        process.exit_code != expected_exit ||
        !text_contains(process.stdout_text, stdout_fragment) ||
        !text_contains(process.stderr_text, stderr_fragment) ||
        (forbidden_stdout_fragment != NULL &&
         text_contains(process.stdout_text, forbidden_stdout_fragment))) {
        (void)fprintf(
            stderr,
            "%s: expected exit %d, got outcome %d and exit %d\n",
            test_name,
            expected_exit,
            (int)process.outcome,
            process.exit_code
        );
        if (result.diagnostic.message[0] != '\0') {
            (void)fprintf(stderr, "runner: %s\n", result.diagnostic.message);
        }
        if (process.stdout_text != NULL) {
            (void)fprintf(stderr, "stdout: %s", process.stdout_text);
        }
        if (process.stderr_text != NULL) {
            (void)fprintf(stderr, "stderr: %s", process.stderr_text);
        }
        failed = 1;
    }
    solar_process_result_free(&process);
    return failed;
}

static int run_arguments_and_expect(
    const char *test_name,
    const char *solar_path,
    const char *working_directory,
    const char *const arguments[],
    int expected_exit,
    const char *stdout_fragment,
    const char *stderr_fragment
)
{
    return run_arguments_and_expect_output(
        test_name, solar_path, working_directory, arguments, expected_exit,
        stdout_fragment, stderr_fragment, NULL
    );
}

static int run_and_expect(
    const char *test_name,
    const char *solar_path,
    const char *working_directory,
    const char *argument,
    int expected_exit,
    const char *stdout_fragment,
    const char *stderr_fragment
)
{
    const char *arguments[] = {solar_path, argument, NULL};

    return run_arguments_and_expect(
        test_name,
        solar_path,
        working_directory,
        arguments,
        expected_exit,
        stdout_fragment,
        stderr_fragment
    );
}

static int write_v2_project(const char *manifest, const char *overflow_testbench)
{
    static const char project_manifest[] =
        "[solar]\n"
        "format = 2\n"
        "\n"
        "[project]\n"
        "name = \"counter\"\n"
        "language = \"verilog\"\n"
        "default_test = \"basic\"\n"
        "default_profile = \"debug\"\n"
        "\n"
        "[sources]\n"
        "rtl = [\"rtl/counter.v\"]\n"
        "\n"
        "[simulation]\n"
        "backend = \"iverilog\"\n"
        "\n"
        "[synthesis]\n"
        "backend = \"yosys\"\n"
        "top = \"counter\"\n"
        "\n"
        "[[profile]]\n"
        "name = \"debug\"\n"
        "defines = [\"DEBUG\"]\n"
        "\n"
        "[[test]]\n"
        "name = \"basic\"\n"
        "top = \"counter_tb\"\n"
        "sources = [\"tb/basic.v\"]\n"
        "waveform = \"basic.vcd\"\n"
        "\n"
        "[[test]]\n"
        "name = \"overflow\"\n"
        "top = \"counter_overflow_tb\"\n"
        "sources = [\"tb/counter_overflow_tb.v\"]\n"
        "waveform = \"overflow.vcd\"\n";

    if (write_text_file(overflow_testbench,
            "module counter_overflow_tb; endmodule\n") != 0) {
        return -1;
    }
    return write_text_file(manifest, project_manifest);
}

static void remove_initialized_project(const char *directory)
{
    const char *relative_files[] = {
        "tb/counter_overflow_tb.v",
        "tb/basic.v",
        "rtl/counter.v",
        "sim/.gitkeep",
        "synth/.gitkeep",
        ".gitignore",
        "README.md",
        "solar.toml"
    };
    const char *relative_directories[] = {"tb", "rtl", "sim", "synth"};
    bool removed = false;
    size_t index;

    (void)solar_filesystem_clean_project(directory, &removed);
    for (index = 0U;
         index < sizeof(relative_files) / sizeof(relative_files[0]);
         index++) {
        char *path = join_path(directory, relative_files[index]);

        if (path != NULL) {
            (void)unlink(path);
        }
        free(path);
    }
    for (index = 0U;
         index < sizeof(relative_directories) / sizeof(relative_directories[0]);
         index++) {
        char *path = join_path(directory, relative_directories[index]);

        if (path != NULL) {
            (void)rmdir(path);
        }
        free(path);
    }
    (void)rmdir(directory);
}

static int test_rtl_only_scan_cli(const char *solar_path)
{
    const char *test_name = "CLI scan RTL-only project";
    char directory_template[] = "/tmp/solar-cli-scan-rtl-only-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *testbench = NULL;
    char *manifest = NULL;
    const char *scan_arguments[] = {solar_path, "scan", NULL};
    int failures = 0;

    if (directory == NULL) return report_failure(test_name, "mkdtemp failed");
    failures += run_and_expect(
        test_name, solar_path, directory, "init", 0,
        "Initialized Solar project", NULL
    );
    testbench = join_path(directory, "tb/basic.v");
    manifest = join_path(directory, "solar.toml");
    if (testbench == NULL || manifest == NULL || unlink(testbench) != 0) {
        failures += report_failure(test_name, "could not remove the testbench");
        goto cleanup;
    }
    failures += run_arguments_and_expect(
        test_name, solar_path, directory, scan_arguments, 0,
        "Tests: 0 total", NULL
    );
    failures += run_and_expect(
        "CLI check RTL-only project", solar_path, directory, "check", 0,
        "is valid", NULL
    );
    failures += run_and_expect(
        "CLI check RTL-only default", solar_path, directory, "check", 0,
        "Default test: none", NULL
    );
    failures += run_and_expect(
        "CLI check RTL-only test list", solar_path, directory, "check", 0,
        "Configured tests: none", NULL
    );
    if (file_contains(manifest, "default_test") ||
        file_contains(manifest, "[[test]]")) {
        failures += report_failure(
            test_name, "scan retained managed test configuration"
        );
    }

cleanup:
    free(manifest);
    free(testbench);
    remove_initialized_project(directory);
    return failures;
}

static int test_check_multiple_tests_without_default(const char *solar_path)
{
    const char *name = "CLI check multiple tests without default";
    char directory_template[] = "/tmp/solar-cli-check-tests-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    char *rtl_directory = NULL;
    char *tb_directory = NULL;
    char *rtl = NULL;
    char *basic = NULL;
    char *overflow = NULL;
    int failures = 0;

    if (directory == NULL) return report_failure(name, "mkdtemp failed");
    manifest = join_path(directory, "solar.toml");
    rtl_directory = join_path(directory, "rtl");
    tb_directory = join_path(directory, "tb");
    rtl = join_path(directory, "rtl/counter.v");
    basic = join_path(directory, "tb/basic.v");
    overflow = join_path(directory, "tb/overflow.v");
    if (manifest == NULL || rtl_directory == NULL || tb_directory == NULL ||
        rtl == NULL || basic == NULL || overflow == NULL ||
        mkdir(rtl_directory, 0700) != 0 || mkdir(tb_directory, 0700) != 0 ||
        write_text_file(manifest,
            "[solar]\nformat = 2\n\n"
            "[project]\nname = \"counter\"\nlanguage = \"verilog\"\n\n"
            "[simulation]\nbackend = \"iverilog\"\n\n"
            "[synthesis]\nbackend = \"yosys\"\ntop = \"counter\"\n") != 0 ||
        write_text_file(rtl, "module counter; endmodule\n") != 0 ||
        write_text_file(basic, "module basic; endmodule\n") != 0 ||
        write_text_file(overflow, "module overflow; endmodule\n") != 0) {
        failures += report_failure(name, "could not create fixture");
        goto cleanup;
    }
    failures += run_and_expect(
        name, solar_path, directory, "check", 0, "Default test: none", NULL
    );
    failures += run_and_expect(
        name, solar_path, directory, "check", 0,
        "overflow     top: overflow", NULL
    );

cleanup:
    if (overflow != NULL) (void)unlink(overflow);
    if (basic != NULL) (void)unlink(basic);
    if (rtl != NULL) (void)unlink(rtl);
    if (manifest != NULL) (void)unlink(manifest);
    if (tb_directory != NULL) (void)rmdir(tb_directory);
    if (rtl_directory != NULL) (void)rmdir(rtl_directory);
    (void)rmdir(directory);
    free(overflow);
    free(basic);
    free(rtl);
    free(tb_directory);
    free(rtl_directory);
    free(manifest);
    return failures;
}

static int test_yanc_template_cli(const char *solar_path)
{
    const char *test_name = "CLI YANC CMM template";
    char directory_template[] = "/tmp/solar-cli-yanc-template-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    char *source = NULL;
    char *source_directory = NULL;
    const char *init_arguments[] = {
        solar_path, "init", "--template", "sapho", NULL
    };
    const char *legacy_arguments[] = {
        solar_path, "init", "--template", "yanc-cmm", NULL
    };
    const char *check_arguments[] = {solar_path, "check", NULL};
    const char *scan_arguments[] = {solar_path, "scan", NULL};
    int failures = 0;

    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    failures += run_arguments_and_expect(
        test_name,
        solar_path,
        directory,
        init_arguments,
        0,
        "template: sapho",
        NULL
    );
    manifest = join_path(directory, "solar.toml");
    source = join_path(directory, "software/processor.cmm");
    source_directory = join_path(directory, "software");
    if (manifest == NULL || source == NULL || !is_regular_file(manifest) ||
        !is_regular_file(source)) {
        failures += report_failure(test_name, "generated files are incomplete");
        goto cleanup;
    }
    failures += run_arguments_and_expect(
        "CLI check generated YANC template",
        solar_path,
        directory,
        check_arguments,
        0,
        "is valid",
        NULL
    );
    failures += run_arguments_and_expect(
        "CLI check generated YANC top",
        solar_path,
        directory,
        check_arguments,
        0,
        "Synthesis top: sapho_demo",
        NULL
    );
    failures += run_arguments_and_expect(
        "CLI check generated YANC test",
        solar_path,
        directory,
        check_arguments,
        0,
        "generated    top: sapho_demo_tb [default] [generated]",
        NULL
    );
    failures += run_arguments_and_expect(
        "CLI YANC template refuses overwrite",
        solar_path,
        directory,
        init_arguments,
        3,
        NULL,
        "refusing to overwrite"
    );
    failures += run_arguments_and_expect(
        "CLI rejects legacy YANC template name",
        solar_path, directory, legacy_arguments, 2, NULL,
        "use solar init [--template verilog|sapho]"
    );
    if (write_text_file(
            source,
            "#PRNAME renamed_cpu\n"
            "#NUBITS 16\n#NDSTAC 4\n#SDEPTH 4\n"
            "#NUIOIN 1\n#NUIOOU 1\n#NBMANT 10\n#NBEXPO 5\n"
            "void main() {}\n"
        ) != 0) {
        failures += report_failure(test_name, "could not edit the CMM identity");
        goto cleanup;
    }
    failures += run_arguments_and_expect(
        "CLI CMM scan",
        solar_path,
        directory,
        scan_arguments,
        0,
        "Processor:     renamed_cpu",
        NULL
    );
    if (!file_contains(manifest, "name = \"renamed_cpu\"") ||
        !file_contains(manifest, "source = \"software/processor.cmm\"") ||
        !file_contains(manifest, "processor = \"renamed_cpu\"") ||
        !file_contains(manifest, "top = \"renamed_cpu\"")) {
        failures += report_failure(
            test_name, "CMM scan did not synchronize managed manifest fields"
        );
    }

cleanup:
    if (source != NULL) (void)unlink(source);
    if (manifest != NULL) (void)unlink(manifest);
    if (source_directory != NULL) (void)rmdir(source_directory);
    (void)rmdir(directory);
    free(source_directory);
    free(source);
    free(manifest);
    return failures;
}

int main(int argc, char *argv[])
{
    char directory_template[] = "/tmp/solar-cli-XXXXXX";
    char *solar_path;
    char *directory;
    char *manifest = NULL;
    char *rtl_file = NULL;
    char *testbench_file = NULL;
    char *overflow_testbench_file = NULL;
    char *solar_directory = NULL;
    char *visible_build_directory = NULL;
    char *generated_log = NULL;
    char *saved_path = NULL;
    char *tool_directory = NULL;
    char *iverilog_tool = NULL;
    char *vvp_tool = NULL;
    char *yosys_tool = NULL;
    char *gtkwave_tool = NULL;
    char *surfer_tool = NULL;
    char *backend_record = NULL;
    char *viewer_record = NULL;
    SolarResult result;
    int failures = 0;

    if (argc != 4) {
        return report_failure(
            "CLI tests", "expected Solar, backend helper, and viewer helper"
        );
    }
    solar_path = realpath(argv[1], NULL);
    if (solar_path == NULL) {
        return report_failure("CLI tests", "could not resolve the solar executable");
    }
    directory = mkdtemp(directory_template);
    if (directory == NULL) {
        free(solar_path);
        return report_failure("CLI tests", "mkdtemp failed");
    }

    failures += run_and_expect(
        "CLI --version", solar_path, directory, "--version", 0,
        SOLAR_VERSION, NULL
    );
    {
        const char *arguments[] = {solar_path, "build", "--help", NULL};

        failures += run_arguments_and_expect(
            "CLI command help", solar_path, directory, arguments, 0,
            "solar build rtl", NULL
        );
    }
    {
        const char *arguments[] = {solar_path, "view", "--help", NULL};

        failures += run_arguments_and_expect(
            "CLI view help", solar_path, directory, arguments, 0,
            "--viewer surfer|gtkwave", NULL
        );
    }
    {
        const char *arguments[] = {solar_path, "config", "--help", NULL};

        failures += run_arguments_and_expect(
            "CLI config help", solar_path, directory, arguments, 0,
            "solar config set --name", NULL
        );
    }
    failures += run_and_expect(
        "CLI unknown command", solar_path, directory, "not-a-solar-command", 2,
        NULL, "unknown command"
    );
    failures += run_and_expect(
        "CLI check outside project", solar_path, directory, "check", 3,
        NULL, "no solar.toml found"
    );
    saved_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (setenv("PATH", directory, 1) != 0) {
        failures += report_failure("CLI doctor", "could not isolate PATH");
    } else {
        failures += run_and_expect(
            "CLI doctor missing tools", solar_path, directory, "doctor", 4,
            "missing", "required EDA tools are unavailable"
        );
        {
            const char *arguments[] = {
                solar_path, "doctor", "--all", NULL
            };

            failures += run_arguments_and_expect(
                "CLI doctor reports Surfer", solar_path, directory,
                arguments, 4, "surfer", NULL
            );
        }
    }
    if (saved_path != NULL) (void)setenv("PATH", saved_path, 1);
    else (void)unsetenv("PATH");
    free(saved_path);
    saved_path = NULL;
    {
        const char *arguments[] = {
            solar_path, "init", "--template", "unknown", NULL
        };

        failures += run_arguments_and_expect(
            "CLI unknown init template",
            solar_path,
            directory,
            arguments,
            2,
            NULL,
            "unknown project template"
        );
    }
    {
        const char *arguments[] = {solar_path, "init", "--template", NULL};

        failures += run_arguments_and_expect(
            "CLI missing init template value",
            solar_path,
            directory,
            arguments,
            2,
            NULL,
            "invalid solar init arguments"
        );
    }
    failures += test_yanc_template_cli(solar_path);
    failures += test_rtl_only_scan_cli(solar_path);
    failures += test_check_multiple_tests_without_default(solar_path);
    if (run_and_expect(
            "CLI init", solar_path, directory, "init", 0,
            "Initialized Solar project", NULL
        ) != 0) {
        failures++;
        goto cleanup;
    }

    manifest = join_path(directory, "solar.toml");
    rtl_file = join_path(directory, "rtl/counter.v");
    testbench_file = join_path(directory, "tb/basic.v");
    if (manifest == NULL || rtl_file == NULL || testbench_file == NULL ||
        !is_regular_file(manifest) || !is_regular_file(rtl_file) ||
        !is_regular_file(testbench_file)) {
        failures += report_failure("CLI init", "generated project is incomplete");
        goto cleanup;
    }
    {
        const char *arguments[] = {
            solar_path, "config", "set",
            "--name", "cli counter",
            "--top", "counter",
            "--test", "basic",
            NULL
        };
        const char *invalid_arguments[] = {
            solar_path, "config", "set", "--default-test", "basic", NULL
        };

        failures += run_arguments_and_expect(
            "CLI config combined update", solar_path, directory, arguments, 0,
            "Solar config: updated", NULL
        );
        if (!file_contains(manifest, "name = \"cli counter\"") ||
            !file_contains(manifest, "top = \"counter\"") ||
            !file_contains(manifest, "default_test = \"basic\"")) {
            failures += report_failure(
                "CLI config combined update",
                "solar.toml does not contain the requested settings"
            );
        }
        failures += run_arguments_and_expect(
            "CLI config rejects removed default-test option",
            solar_path, directory, invalid_arguments, 2,
            NULL, "unknown solar config option"
        );
    }
    failures += run_and_expect(
        "CLI check", solar_path, directory, "check", 0,
        "is valid", NULL
    );
    failures += run_and_expect(
        "CLI check synthesis top", solar_path, directory, "check", 0,
        "Synthesis top: counter", NULL
    );
    failures += run_and_expect(
        "CLI check default test", solar_path, directory, "check", 0,
        "Default test: basic (top: counter_tb)", NULL
    );
    failures += run_and_expect(
        "CLI check configured tests", solar_path, directory, "check", 0,
        "basic        top: counter_tb [default]", NULL
    );
    failures += run_and_expect(
        "CLI report before build", solar_path, directory, "report", 3,
        NULL, "no Solar build report is available"
    );
    failures += run_and_expect(
        "CLI build requires target", solar_path, directory, "build", 2,
        NULL, "requires a target"
    );
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "--list", NULL
        };

        failures += run_arguments_and_expect(
            "CLI initialized format-2 test list",
            solar_path,
            directory,
            arguments,
            0,
            "basic        top: counter_tb [default]",
            NULL
        );
    }
    failures += run_and_expect(
        "CLI second init", solar_path, directory, "init", 3,
        NULL, "refusing to overwrite"
    );

    {
        const char *compile_arguments[] = {solar_path, "compile", NULL};
        const char *dry_run_arguments[] = {
            solar_path, "build", "full", "--dry-run", NULL
        };
        const char *report_arguments[] = {solar_path, "report", NULL};

        failures += run_arguments_and_expect(
            "CLI removed compile command",
            solar_path, directory, compile_arguments, 2, NULL,
            "unknown command"
        );
        failures += run_arguments_and_expect(
            "CLI build dry run",
            solar_path, directory, dry_run_arguments, 0,
            "DRY RUN", NULL
        );
        failures += run_arguments_and_expect(
            "CLI operation report",
            solar_path, directory, report_arguments, 0,
            "Command              solar build full", NULL
        );
        failures += run_arguments_and_expect(
            "CLI complete operation report",
            solar_path, directory, report_arguments, 0,
            "effective.defines", NULL
        );
        failures += run_arguments_and_expect(
            "CLI report host environment",
            solar_path, directory, report_arguments, 0,
            "HOST ENVIRONMENT", NULL
        );
        failures += run_arguments_and_expect(
            "CLI report timing section",
            solar_path, directory, report_arguments, 0,
            "TIMING BREAKDOWN", NULL
        );
    }

    tool_directory = join_path(directory, "fake tools");
    iverilog_tool = join_path(tool_directory, "iverilog");
    vvp_tool = join_path(tool_directory, "vvp");
    yosys_tool = join_path(tool_directory, "yosys");
    gtkwave_tool = join_path(tool_directory, "gtkwave");
    surfer_tool = join_path(tool_directory, "surfer");
    backend_record = join_path(directory, "backend.record");
    viewer_record = join_path(directory, "viewer.record");
    saved_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (tool_directory == NULL || iverilog_tool == NULL || vvp_tool == NULL ||
        yosys_tool == NULL || gtkwave_tool == NULL || surfer_tool == NULL ||
        backend_record == NULL ||
        viewer_record == NULL || mkdir(tool_directory, 0700) != 0 ||
        symlink(argv[2], iverilog_tool) != 0 ||
        symlink(argv[2], vvp_tool) != 0 ||
        symlink(argv[2], yosys_tool) != 0 ||
        symlink(argv[3], gtkwave_tool) != 0 ||
        symlink(argv[3], surfer_tool) != 0 ||
        setenv("PATH", tool_directory, 1) != 0 ||
        setenv("SOLAR_BACKEND_RECORD", backend_record, 1) != 0 ||
        setenv("SOLAR_VIEWER_RECORD", viewer_record, 1) != 0 ||
        setenv("DISPLAY", ":solar-test", 1) != 0) {
        failures += report_failure(
            "CLI viewer isolation", "could not prepare fake tools"
        );
        goto cleanup;
    }
    {
        const char *dry_arguments[] = {
            solar_path, "build", "full", "--dry-run", NULL
        };
        const char *sim_arguments[] = {
            solar_path, "build", "sim", "basic", NULL
        };
        const char *full_arguments[] = {
            solar_path, "build", "full", NULL
        };
        const char *full_no_progress_arguments[] = {
            solar_path, "build", "full", "--no-progress", NULL
        };
        const char *view_arguments[] = {
            solar_path, "view", "basic", NULL
        };
        const char *surfer_view_arguments[] = {
            solar_path, "view", "basic", "--viewer", "surfer", NULL
        };
        const char *invalid_viewer_arguments[] = {
            solar_path, "view", "basic", "--viewer", "unknown", NULL
        };
        const char *no_progress_arguments[] = {
            solar_path, "build", "sim", "basic", "--no-progress", NULL
        };
        const char *verbose_arguments[] = {
            solar_path, "build", "sim", "basic", "--verbose", NULL
        };

        (void)unlink(backend_record);
        failures += run_arguments_and_expect(
            "CLI dry run executes no tools",
            solar_path, directory, dry_arguments, 0,
            "DRY RUN", NULL
        );
        if (!path_is_missing(backend_record)) {
            failures += report_failure(
                "CLI dry run executes no tools",
                "an EDA executable was invoked"
            );
        }
        failures += run_arguments_and_expect(
            "CLI sim does not auto-open viewer",
            solar_path, directory, sim_arguments, 0,
            "PASS  basic", NULL
        );
        if (!path_is_missing(viewer_record)) {
            failures += report_failure(
                "CLI sim does not auto-open viewer",
                "GTKWave was invoked without --view"
            );
        }
        failures += run_arguments_and_expect(
            "CLI full reports simulation progress",
            solar_path, directory, full_arguments, 0,
            "[sim] running simulation", NULL
        );
        failures += run_arguments_and_expect_output(
            "CLI full accepts no progress",
            solar_path, directory, full_no_progress_arguments, 0,
            "PASS  synth", NULL, "[sim]"
        );
        failures += run_arguments_and_expect(
            "CLI sim no progress", solar_path, directory,
            no_progress_arguments, 0, "Simulation summary:", NULL
        );
        if (setenv("SOLAR_BACKEND_CHATTER", "1", 1) != 0) {
            failures += report_failure(
                "CLI sim verbose", "could not enable fake tool output"
            );
        } else {
            failures += run_arguments_and_expect(
                "CLI sim verbose", solar_path, directory, verbose_arguments, 0,
                "iverilog verbose stdout", "iverilog verbose stderr"
            );
            (void)unsetenv("SOLAR_BACKEND_CHATTER");
        }
        if (setenv("SOLAR_BACKEND_NO_WAVEFORM", "1", 1) != 0) {
            failures += report_failure(
                "CLI simulation without waveform",
                "could not configure fake simulation"
            );
        } else {
            failures += run_arguments_and_expect(
                "CLI simulation without waveform",
                solar_path, directory, sim_arguments, 0,
                "PASS  basic", "run solar scan after changing $dumpfile"
            );
            (void)unsetenv("SOLAR_BACKEND_NO_WAVEFORM");
        }
        (void)unlink(backend_record);
        failures += run_arguments_and_expect(
            "CLI view opens existing waveform",
            solar_path, directory, view_arguments, 0,
            "Waveform viewer: GTKWave", NULL
        );
        if (!wait_for_file(viewer_record) || !path_is_missing(backend_record)) {
            failures += report_failure(
                "CLI view opens existing waveform",
                "view did not launch GTKWave or executed a simulation backend"
            );
        }
        (void)unlink(viewer_record);
        failures += run_arguments_and_expect(
            "CLI view opens Surfer",
            solar_path, directory, surfer_view_arguments, 0,
            "Waveform viewer: Surfer", NULL
        );
        if (!wait_for_file(viewer_record) || !path_is_missing(backend_record) ||
            !file_contains(viewer_record, "surfer")) {
            failures += report_failure(
                "CLI view opens Surfer",
                "view did not launch Surfer or executed a simulation backend"
            );
        }
        failures += run_arguments_and_expect(
            "CLI rejects unknown viewer",
            solar_path, directory, invalid_viewer_arguments, 2,
            NULL, "unknown waveform viewer"
        );
    }
    if (saved_path != NULL) (void)setenv("PATH", saved_path, 1);
    else (void)unsetenv("PATH");
    free(saved_path);
    saved_path = NULL;
    (void)unsetenv("SOLAR_BACKEND_RECORD");
    (void)unsetenv("SOLAR_BACKEND_NO_WAVEFORM");
    (void)unsetenv("SOLAR_BACKEND_CHATTER");
    (void)unsetenv("SOLAR_VIEWER_RECORD");
    (void)unsetenv("DISPLAY");

    overflow_testbench_file = join_path(directory, "tb/counter_overflow_tb.v");
    if (overflow_testbench_file == NULL ||
        write_v2_project(manifest, overflow_testbench_file) != 0) {
        failures += report_failure("CLI format-2 fixture", "could not write fixture");
        goto cleanup;
    }
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "--list", NULL
        };

        failures += run_arguments_and_expect(
            "CLI format-2 default test list",
            solar_path,
            directory,
            arguments,
            0,
            "basic        top: counter_tb [default]",
            NULL
        );
        failures += run_arguments_and_expect(
            "CLI format-2 complete test list",
            solar_path,
            directory,
            arguments,
            0,
            "overflow     top: counter_overflow_tb",
            NULL
        );
    }
    saved_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (setenv("PATH", tool_directory, 1) != 0 ||
        setenv("SOLAR_BACKEND_RECORD", backend_record, 1) != 0 ||
        setenv("SOLAR_BACKEND_FAIL_TEST", "overflow", 1) != 0) {
        failures += report_failure(
            "CLI full failure stop", "could not configure fake tools"
        );
    } else {
        const char *arguments[] = {solar_path, "build", "full", NULL};

        (void)unlink(backend_record);
        failures += run_arguments_and_expect(
            "CLI full failure stop", solar_path, directory, arguments, 5,
            "FAIL  sim", "vvp exited with status 67"
        );
        {
            const char *report_arguments[] = {solar_path, "report", NULL};

            failures += run_arguments_and_expect(
                "CLI failed-build stderr report", solar_path, directory,
                report_arguments, 0, "intentional simulation failure", NULL
            );
            failures += run_arguments_and_expect(
                "CLI per-simulation timing report", solar_path, directory,
                report_arguments, 0, "Sim overflow: vvp runtime", NULL
            );
            failures += run_arguments_and_expect(
                "CLI raw nanosecond report", solar_path, directory,
                report_arguments, 0, " ns)", NULL
            );
        }
        if (file_contains(backend_record, "yosys\t-s")) {
            failures += report_failure(
                "CLI full failure stop",
                "synthesis executed after a failed simulation"
            );
        }
    }
    if (saved_path != NULL) (void)setenv("PATH", saved_path, 1);
    else (void)unsetenv("PATH");
    free(saved_path);
    saved_path = NULL;
    (void)unsetenv("SOLAR_BACKEND_RECORD");
    (void)unsetenv("SOLAR_BACKEND_FAIL_TEST");
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "--all", "--list", NULL
        };

        failures += run_arguments_and_expect(
            "CLI incompatible test modes",
            solar_path,
            directory,
            arguments,
            2,
            NULL,
            "mutually exclusive"
        );
    }
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "basic", "--all", NULL
        };

        failures += run_arguments_and_expect(
            "CLI named test with all",
            solar_path,
            directory,
            arguments,
            2,
            NULL,
            "mutually exclusive"
        );
    }
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "--list", "--profile", "debug", NULL
        };

        failures += run_arguments_and_expect(
            "CLI list with profile",
            solar_path,
            directory,
            arguments,
            2,
            NULL,
            "--list cannot be combined"
        );
    }
    {
        const char *arguments[] = {
            solar_path, "build", "synth", "--verbose", NULL
        };

        failures += run_arguments_and_expect(
            "CLI progress option requires sim", solar_path, directory,
            arguments, 2, NULL,
            "progress options require the sim or full target"
        );
    }
    {
        const char *arguments[] = {solar_path, "simulate", "--all", NULL};

        failures += run_arguments_and_expect(
            "CLI removed simulate command",
            solar_path,
            directory,
            arguments,
            2,
            NULL,
            "unknown command"
        );
    }
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "--profile", NULL
        };

        failures += run_arguments_and_expect(
            "CLI missing profile value",
            solar_path,
            directory,
            arguments,
            2,
            NULL,
            "invalid solar build profile"
        );
    }
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "missing", NULL
        };

        failures += run_arguments_and_expect(
            "CLI missing named test",
            solar_path,
            directory,
            arguments,
            3,
            NULL,
            "test \"missing\" does not exist"
        );
    }
    {
        const char *arguments[] = {
            solar_path, "build", "sim", "basic", "--profile", "missing", NULL
        };

        failures += run_arguments_and_expect(
            "CLI missing profile",
            solar_path,
            directory,
            arguments,
            3,
            NULL,
            "profile \"missing\" does not exist"
        );
    }

    saved_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (setenv("PATH", directory, 1) != 0) {
        failures += report_failure("CLI test execution", "could not isolate PATH");
    } else {
        const char *named_arguments[] = {
            solar_path, "build", "sim", "basic", "--profile", "debug", NULL
        };
        const char *simulate_arguments[] = {
            solar_path, "build", "sim", "overflow", "--profile", "debug", NULL
        };
        const char *all_arguments[] = {
            solar_path, "build", "sim", "--all", NULL
        };

        failures += run_arguments_and_expect(
            "CLI named test with profile",
            solar_path,
            directory,
            named_arguments,
            4,
            NULL,
            "required executable is unavailable: iverilog"
        );
        failures += run_arguments_and_expect(
            "CLI build sim uses Test Service",
            solar_path,
            directory,
            simulate_arguments,
            4,
            NULL,
            "required executable is unavailable: iverilog"
        );
        failures += run_arguments_and_expect(
            "CLI run all summary",
            solar_path,
            directory,
            all_arguments,
            4,
            NULL,
            "required executable is unavailable: iverilog"
        );
    }
    if (saved_path != NULL) (void)setenv("PATH", saved_path, 1);
    else (void)unsetenv("PATH");
    free(saved_path);
    saved_path = NULL;

    if (write_text_file(manifest, "this is not a Solar manifest\n") != 0) {
        failures += report_failure(
            "CLI report after invalid manifest", "could not corrupt fixture"
        );
    } else {
        const char *report_arguments[] = {solar_path, "report", NULL};

        failures += run_arguments_and_expect(
            "CLI report after invalid manifest",
            solar_path, directory, report_arguments, 0,
            "SOLAR LAST BUILD REPORT", NULL
        );
    }
    if (write_v2_project(manifest, overflow_testbench_file) != 0) {
        failures += report_failure(
            "CLI report fixture restore", "could not restore manifest"
        );
        goto cleanup;
    }

    result = solar_filesystem_prepare_generated_directory(
        directory, ".solar/logs"
    );
    generated_log = join_path(directory, ".solar/logs/test-cli.log");
    solar_directory = join_path(directory, ".solar");
    visible_build_directory = join_path(directory, "solar-build");
    if (result.status != SOLAR_STATUS_OK || generated_log == NULL ||
        solar_directory == NULL || visible_build_directory == NULL ||
        write_text_file(generated_log, "generated\n") != 0) {
        failures += report_failure("CLI clean", "could not prepare generated data");
        goto cleanup;
    }
    if (run_and_expect(
            "CLI clean", solar_path, directory, "clean", 0,
            "registered artifact(s), temporary data, and disposable state", NULL
        ) != 0) {
        failures++;
        goto cleanup;
    }
    if (path_is_missing(solar_directory) ||
        !path_is_missing(visible_build_directory) || !is_regular_file(manifest) ||
        !is_regular_file(rtl_file) || !is_regular_file(testbench_file)) {
        failures += report_failure(
            "CLI clean", "clean removed sources or retained generated data"
        );
    }

cleanup:
    (void)unsetenv("SOLAR_BACKEND_RECORD");
    (void)unsetenv("SOLAR_VIEWER_RECORD");
    (void)unsetenv("SOLAR_BACKEND_CHATTER");
    (void)unlink(viewer_record == NULL ? "" : viewer_record);
    (void)unlink(backend_record == NULL ? "" : backend_record);
    (void)unlink(gtkwave_tool == NULL ? "" : gtkwave_tool);
    (void)unlink(surfer_tool == NULL ? "" : surfer_tool);
    (void)unlink(yosys_tool == NULL ? "" : yosys_tool);
    (void)unlink(vvp_tool == NULL ? "" : vvp_tool);
    (void)unlink(iverilog_tool == NULL ? "" : iverilog_tool);
    if (tool_directory != NULL) (void)rmdir(tool_directory);
    free(saved_path);
    free(generated_log);
    free(solar_directory);
    free(visible_build_directory);
    free(overflow_testbench_file);
    free(testbench_file);
    free(rtl_file);
    free(manifest);
    free(viewer_record);
    free(backend_record);
    free(gtkwave_tool);
    free(surfer_tool);
    free(yosys_tool);
    free(vvp_tool);
    free(iverilog_tool);
    free(tool_directory);
    remove_initialized_project(directory);
    free(solar_path);
    return failures == 0 ? 0 : 1;
}
