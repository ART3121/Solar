#define _POSIX_C_SOURCE 200809L

#include "solar/artifact.h"
#include "solar/filesystem.h"
#include "solar/runner.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int fail(const char *message)
{
    (void)fprintf(stderr, "completion: %s\n", message);
    return 1;
}

static int run_and_expect(
    const char *executable,
    const char *const arguments[],
    const char *working_directory,
    int expected_exit,
    const char *expected_output,
    const char *expected_error
)
{
    const SolarEnvironmentAddition environment[] = {
        {"PATH", "/solar-completion-test-no-tools"}
    };
    const SolarProcessSpec specification = {
        executable,
        arguments,
        working_directory,
        NULL,
        NULL,
        environment,
        1U,
        NULL
    };
    SolarProcessResult process;
    SolarResult result;
    bool status_matches;
    int failed = 0;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    status_matches = expected_exit == 0
        ? result.status == SOLAR_STATUS_OK
        : result.status == SOLAR_STATUS_PROCESS_FAILED;
    if (!status_matches || process.outcome != SOLAR_PROCESS_EXITED ||
        process.exit_code != expected_exit ||
        strcmp(process.stdout_text == NULL ? "" : process.stdout_text,
            expected_output == NULL ? "" : expected_output) != 0 ||
        (expected_error != NULL &&
         (process.stderr_text == NULL ||
          strstr(process.stderr_text, expected_error) == NULL))) {
        (void)fprintf(stderr,
            "completion command failed: status=%s exit=%d\nstdout=%s\nstderr=%s\n",
            solar_status_name(result.status), process.exit_code,
            process.stdout_text == NULL ? "" : process.stdout_text,
            process.stderr_text == NULL ? "" : process.stderr_text);
        failed = 1;
    }
    solar_process_result_free(&process);
    return failed;
}

static int prepare_history(const char *root)
{
    static const char *const ids[] = {"build-000001", "build-000003"};
    SolarResult result = solar_filesystem_prepare_generated_directory(
        root, ".solar/reports"
    );
    size_t index;

    if (result.status != SOLAR_STATUS_OK) return -1;
    for (index = 0U; index < sizeof(ids) / sizeof(ids[0]); index++) {
        char *relative = join_path(".solar/reports", ids[index]);
        char *directory = relative == NULL ? NULL : join_path(root, relative);
        char *report = directory == NULL ? NULL : join_path(directory, "report.txt");
        int failed = relative == NULL || directory == NULL || report == NULL ||
            mkdir(directory, 0700) != 0 || write_text(report, "stored report\n") != 0;

        free(report);
        free(directory);
        free(relative);
        if (failed) return -1;
    }
    return 0;
}

static int prepare_waveform(const char *root)
{
    char *temporary = NULL;
    char *published = NULL;
    SolarResult result = solar_filesystem_prepare_generated_directory(
        root, ".solar/tmp/completion"
    );
    int failed = 0;

    if (result.status != SOLAR_STATUS_OK) return -1;
    temporary = join_path(root, ".solar/tmp/completion/wave output.vcd");
    if (temporary == NULL || write_text(temporary, "$date completion $end\n") != 0) {
        failed = 1;
        goto cleanup;
    }
    result = solar_artifact_publish_file(
        root,
        temporary,
        "sim/wave output.vcd",
        "waveform",
        "simulation",
        "basic",
        "iverilog",
        "debug",
        &published
    );
    if (result.status != SOLAR_STATUS_OK || published == NULL) failed = 1;

cleanup:
    free(published);
    free(temporary);
    return failed ? -1 : 0;
}

static int test_candidates(
    const char *solar,
    const char *root,
    const char *nested,
    const char *empty
)
{
    const char *tests[] = {solar, "__complete", "tests", NULL};
    const char *profiles[] = {solar, "__complete", "profiles", NULL};
    const char *builds[] = {solar, "__complete", "build-ids", NULL};
    const char *waveforms[] = {solar, "__complete", "waveforms", NULL};
    const char *unknown[] = {solar, "__complete", "unknown", NULL};
    const char *help[] = {solar, "--help", NULL};
    SolarProcessSpec help_specification = {
        solar, help, root, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult help_process;
    SolarResult result;
    int failed = 0;

    failed += run_and_expect(solar, tests, nested, 0,
        "basic\noverflow\n", NULL);
    failed += run_and_expect(solar, profiles, nested, 0,
        "debug\nrelease\n", NULL);
    failed += run_and_expect(solar, builds, nested, 0,
        "build-000003\nbuild-000001\n", NULL);
    failed += run_and_expect(solar, waveforms, nested, 0,
        "sim/wave output.vcd\n", NULL);
    failed += run_and_expect(solar, builds, empty, 0, "", NULL);
    failed += run_and_expect(solar, unknown, root, 2, "",
        "unknown internal completion candidate kind");

    solar_process_result_init(&help_process);
    result = solar_runner_run(&help_specification, &help_process);
    if (result.status != SOLAR_STATUS_OK || help_process.exit_code != 0 ||
        help_process.stdout_text == NULL ||
        strstr(help_process.stdout_text, "__complete") != NULL) {
        failed += fail("internal completion appeared in public help");
    }
    solar_process_result_free(&help_process);
    return failed;
}

static int test_bash_completion(const char *solar, const char *root)
{
    static const char script[] =
        "source \"$1\"; solar_bin=\"$2\"; "
        "run_completion() { local label=\"$1\"; shift; "
        "COMP_WORDS=(\"$solar_bin\" \"$@\"); "
        "COMP_CWORD=$((${#COMP_WORDS[@]} - 1)); _solar_completion; "
        "printf '<%s>\\n' \"$label\"; printf '%s\\n' \"${COMPREPLY[@]}\"; }; "
        "run_completion root bu; "
        "run_completion target build s; "
        "run_completion template init --template s; "
        "run_completion test build sim ov; "
        "run_completion profile build synth --profile r; "
        "run_completion full-option build full --no; "
        "run_completion config-test config set --test b; "
        "run_completion viewer view --viewer s; "
        "run_completion waveform view --waveform sim/; "
        "run_completion show report show build-000; "
        "run_completion current report compare build-000; "
        "run_completion against report compare --against build-000";
    const char *arguments[] = {
        SOLAR_TEST_BASH, "--noprofile", "--norc", "-c", script,
        "solar-completion-test", SOLAR_BASH_COMPLETION, solar, NULL
    };

    if (SOLAR_TEST_BASH[0] == '\0') {
        (void)printf("SKIP: Bash is unavailable for functional completion\n");
        return 0;
    }
    return run_and_expect(SOLAR_TEST_BASH, arguments, root, 0,
        "<root>\nbuild\n"
        "<target>\nsim\nsynth\n"
        "<template>\nsapho\n"
        "<test>\noverflow\n"
        "<profile>\nrelease\n"
        "<full-option>\n--no-progress\n"
        "<config-test>\nbasic\n"
        "<viewer>\nsurfer\n"
        "<waveform>\nsim/wave output.vcd\n"
        "<show>\nbuild-000003\nbuild-000001\n"
        "<current>\nbuild-000003\nbuild-000001\n"
        "<against>\nbuild-000003\nbuild-000001\n", NULL);
}

static int test_zsh_completion(const char *solar, const char *root)
{
    static const char script[] =
        "compadd() { [[ \"$1\" == -- ]] && shift; "
        "local item; for item in \"$@\"; do "
        "[[ \"$item\" == \"$PREFIX\"* ]] && print -r -- \"$item\"; done; }; "
        "completion_file=\"$1\"; solar_bin=\"$2\"; "
        "run_completion() { local label=\"$1\"; shift; "
        "words=(\"$solar_bin\" \"$@\"); CURRENT=${#words[@]}; PREFIX=${words[-1]}; "
        "print -r -- \"<$label>\"; source \"$completion_file\"; }; "
        "run_completion root bu; "
        "run_completion profile build synth --profile r; "
        "run_completion waveform view --waveform sim/; "
        "run_completion against report compare --against build-000";
    const char *arguments[] = {
        SOLAR_TEST_ZSH, "-f", "-c", script,
        "solar-completion-test", SOLAR_ZSH_COMPLETION, solar, NULL
    };

    if (SOLAR_TEST_ZSH[0] == '\0') {
        (void)printf("SKIP: Zsh is unavailable for functional completion\n");
        return 0;
    }
    return run_and_expect(SOLAR_TEST_ZSH, arguments, root, 0,
        "<root>\nbuild\n"
        "<profile>\nrelease\n"
        "<waveform>\nsim/wave output.vcd\n"
        "<against>\nbuild-000003\nbuild-000001\n", NULL);
}

static void cleanup_project(const char *root, const char *nested, const char *manifest)
{
    size_t removed_count = 0U;
    bool removed = false;
    char *nested_parent = join_path(root, "nested");
    char *sim = join_path(root, "sim");

    (void)solar_artifact_remove_registered(root, &removed_count);
    (void)solar_filesystem_clean_project(root, &removed);
    (void)unlink(manifest);
    (void)rmdir(nested);
    if (nested_parent != NULL) (void)rmdir(nested_parent);
    if (sim != NULL) (void)rmdir(sim);
    (void)rmdir(root);
    free(sim);
    free(nested_parent);
}

int main(int argc, char **argv)
{
    static const char manifest_text[] =
        "[solar]\nformat = 2\n\n"
        "[project]\nname = \"completion\"\nlanguage = \"verilog\"\n"
        "default_test = \"basic\"\ndefault_profile = \"debug\"\n\n"
        "[sources]\nrtl = []\n\n"
        "[simulation]\nbackend = \"iverilog\"\n\n"
        "[synthesis]\nbackend = \"yosys\"\ntop = \"counter\"\n\n"
        "[[profile]]\nname = \"debug\"\n\n"
        "[[profile]]\nname = \"release\"\n\n"
        "[[test]]\nname = \"basic\"\ntop = \"basic_tb\"\n"
        "sources = [\"tb/basic.v\"]\n\n"
        "[[test]]\nname = \"overflow\"\ntop = \"overflow_tb\"\n"
        "sources = [\"tb/overflow.v\"]\n";
    char root_template[] = "/tmp/solar completion-XXXXXX";
    char empty_template[] = "/tmp/solar completion empty-XXXXXX";
    char *root;
    char *empty;
    char *manifest = NULL;
    char *nested_parent = NULL;
    char *nested = NULL;
    char *sim = NULL;
    int failed = 0;

    if (argc == 2 && strcmp(argv[1], "--skip-fish") == 0) {
        (void)printf("SKIP: Fish is not installed\n");
        return 77;
    }
    if (argc != 2) return fail("expected the Solar executable path");
    root = mkdtemp(root_template);
    empty = mkdtemp(empty_template);
    if (root == NULL || empty == NULL) return fail("mkdtemp failed");
    manifest = join_path(root, "solar.toml");
    nested_parent = join_path(root, "nested");
    nested = join_path(root, "nested/project child");
    sim = join_path(root, "sim");
    if (manifest == NULL || nested_parent == NULL || nested == NULL || sim == NULL ||
        write_text(manifest, manifest_text) != 0 ||
        mkdir(nested_parent, 0700) != 0 || mkdir(nested, 0700) != 0 ||
        mkdir(sim, 0700) != 0 || prepare_history(root) != 0 ||
        prepare_waveform(root) != 0) {
        failed = fail("could not prepare the completion fixture");
        goto cleanup;
    }
    failed += test_candidates(argv[1], root, nested, empty);
    failed += test_bash_completion(argv[1], root);
    failed += test_zsh_completion(argv[1], root);

cleanup:
    if (manifest != NULL && nested != NULL) cleanup_project(root, nested, manifest);
    (void)rmdir(empty);
    free(sim);
    free(nested);
    free(nested_parent);
    free(manifest);
    if (failed == 0) (void)printf("completion tests passed\n");
    return failed == 0 ? 0 : 1;
}
