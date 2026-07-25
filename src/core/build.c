#include "solar/build.h"

#include "solar/artifact.h"
#include "solar/backend.h"
#include "solar/filesystem.h"
#include "solar/solar.h"
#include "solar/system.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef SOLAR_BUILD_COMPILER
#define SOLAR_BUILD_COMPILER "unknown"
#endif

#ifndef SOLAR_BUILD_TYPE
#define SOLAR_BUILD_TYPE "unknown"
#endif

#define REPORT_WIDTH 80
#define REPORT_SIZE_LIMIT ((off_t)16 * 1024 * 1024)

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0U;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
        (uint64_t)value.tv_nsec;
}

static uint64_t elapsed_since(uint64_t start)
{
    uint64_t end = monotonic_nanoseconds();

    return end >= start ? end - start : 0U;
}

static SolarResult copy_optional(char **destination, const char *source)
{
    char *copy;

    if (source == NULL) return solar_result_ok();
    copy = strdup(source);
    if (copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate build context data",
            "free memory and try again"
        );
    }
    free(*destination);
    *destination = copy;
    return solar_result_ok();
}

void solar_build_context_init(SolarBuildContext *context)
{
    if (context == NULL) return;
    (void)memset(context, 0, sizeof(*context));
    solar_project_init(&context->project);
    solar_compiler_result_init(&context->compiler_result);
    solar_rtl_build_result_init(&context->rtl_result);
    solar_test_result_init(&context->test_result);
    solar_test_summary_init(&context->test_summary);
    solar_synthesis_result_init(&context->synthesis_result);
    solar_project_lock_init(&context->project_lock);
    context->result = solar_result_ok();
}

void solar_build_context_free(SolarBuildContext *context)
{
    if (context == NULL) return;
    solar_project_lock_release(&context->project_lock);
    solar_project_free(&context->project);
    free(context->operation);
    free(context->requested_profile);
    free(context->profile_name);
    solar_compiler_result_free(&context->compiler_result);
    solar_rtl_build_result_free(&context->rtl_result);
    solar_test_result_free(&context->test_result);
    solar_test_summary_free(&context->test_summary);
    solar_synthesis_result_free(&context->synthesis_result);
    solar_build_context_init(context);
}

void solar_build_context_set_progress(
    SolarBuildContext *context,
    const SolarProgressObserver *progress_observer
)
{
    if (context != NULL) context->progress_observer = progress_observer;
}

static void record_step(
    SolarBuildContext *context,
    const char *name,
    SolarResult result,
    uint64_t started,
    bool skipped
)
{
    SolarBuildStepResult *step;

    if (context->step_count >= SOLAR_BUILD_MAX_STEPS) return;
    step = &context->steps[context->step_count++];
    (void)snprintf(step->name, sizeof(step->name), "%s", name);
    step->result = result;
    step->duration_ns = context->dry_run ? 0U : elapsed_since(started);
    step->duration_ms = step->duration_ns / UINT64_C(1000000);
    step->skipped = skipped;
}

SolarResult solar_build_context_load(
    SolarBuildContext *context,
    const char *start_path,
    const char *operation,
    const char *profile_name,
    bool dry_run
)
{
    char *fallback_root = NULL;
    SolarResult result;

    if (context == NULL || start_path == NULL || operation == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot create an incomplete build context",
            "provide a project path and operation name"
        );
    }
    context->started_ns = monotonic_nanoseconds();
    context->started_at = time(NULL);
    context->dry_run = dry_run;
    solar_progress_stage(
        context->progress_observer, SOLAR_PROGRESS_LOAD,
        "Loading project", NULL
    );
    result = copy_optional(&context->operation, operation);
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_optional(&context->requested_profile, profile_name);
    }
    if (result.status == SOLAR_STATUS_OK) {
        SolarResult root_result = solar_project_find_root(
            start_path, &fallback_root
        );

        if (root_result.status != SOLAR_STATUS_OK) {
            fallback_root = NULL;
            result = solar_project_load(start_path, &context->project);
        } else {
            context->lock_attempted = true;
            result = solar_project_lock_acquire(
                fallback_root, &context->project_lock
            );
            if (result.status == SOLAR_STATUS_OK) {
                context->owns_project_lock = true;
                result = solar_project_load(start_path, &context->project);
            }
        }
    }
    if (result.status != SOLAR_STATUS_OK) {
        if (context->project.root == NULL && fallback_root != NULL) {
            context->project.root = fallback_root;
            fallback_root = NULL;
            (void)solar_filesystem_join(
                context->project.root, "solar.toml",
                &context->project.manifest_path
            );
        }
    }
    context->load_duration_ns = elapsed_since(context->started_ns);
    free(fallback_root);
    return result;
}

SolarResult solar_build_context_rtl(SolarBuildContext *context)
{
    uint64_t started = monotonic_nanoseconds();
    SolarResult result;

    if (context == NULL || context->project.root == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot build RTL without a loaded project",
            "load and validate the project first"
        );
    }
    if (context->rtl_ready) {
        record_step(context, "rtl", solar_result_ok(), started, true);
        return solar_result_ok();
    }
    solar_progress_stage(
        context->progress_observer, SOLAR_PROGRESS_RESOLVE,
        "Resolving sources", NULL
    );
    if (context->dry_run) {
        context->rtl_ready = true;
        if (context->project.config.compiler.backend != NULL) {
            context->generated = true;
        }
        record_step(context, "rtl", solar_result_ok(), started, false);
        return solar_result_ok();
    }
    if (context->project.config.compiler.backend != NULL) {
        solar_progress_stage(
            context->progress_observer, SOLAR_PROGRESS_GENERATE,
            "Generating SAPHO hardware", "YANC"
        );
        result = solar_compiler_compile_with_progress(
            &context->project, context->progress_observer,
            &context->compiler_result
        );
        if (result.status == SOLAR_STATUS_OK) context->generated = true;
    } else {
        solar_progress_stage(
            context->progress_observer, SOLAR_PROGRESS_SIMULATION_COMPILE,
            context->project.config.simulation.backend != NULL &&
                strcmp(context->project.config.simulation.backend, "verilator") == 0
                ? "Compiling with Verilator"
                : "Compiling with Icarus",
            context->project.config.simulation.backend != NULL &&
                strcmp(context->project.config.simulation.backend, "verilator") == 0
                ? "Verilator" : "Icarus Verilog"
        );
        result = solar_rtl_build_with_progress(
            &context->project, context->profile_name,
            context->progress_observer, &context->rtl_result
        );
    }
    if (result.status == SOLAR_STATUS_OK) context->rtl_ready = true;
    record_step(context, "rtl", result, started, false);
    return result;
}

SolarResult solar_build_context_check(SolarBuildContext *context)
{
    const SolarProfile *profile = NULL;
    uint64_t started = monotonic_nanoseconds();
    SolarResult result;

    if (context == NULL || context->project.root == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot check an unloaded build context",
            "load the project before checking it"
        );
    }
    solar_progress_stage(
        context->progress_observer, SOLAR_PROGRESS_VALIDATE,
        "Validating configuration", NULL
    );
    result = solar_project_validate(&context->project);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_config_select_profile(
            &context->project.config, context->requested_profile, &profile
        );
    }
    if (result.status == SOLAR_STATUS_OK && profile != NULL) {
        result = copy_optional(&context->profile_name, profile->name);
    }
    record_step(context, "check", result, started, false);
    context->validation_duration_ns = elapsed_since(started);
    return result;
}

static SolarResult ensure_generated(SolarBuildContext *context)
{
    return context->rtl_ready
        ? solar_result_ok()
        : solar_build_context_rtl(context);
}

SolarResult solar_build_context_test(
    SolarBuildContext *context,
    const char *test_name,
    bool run_all,
    bool simulation_operation
)
{
    const SolarGeneratedArtifacts *artifacts = NULL;
    const char *step_name = simulation_operation ? "sim" : "test";
    uint64_t started;
    SolarResult result;

    if (context == NULL || context->project.root == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot run tests without a loaded project",
            "load and validate the project first"
        );
    }
    result = ensure_generated(context);
    if (result.status != SOLAR_STATUS_OK) return result;
    started = monotonic_nanoseconds();
    if (context->dry_run) {
        record_step(context, step_name, solar_result_ok(), started, false);
        return solar_result_ok();
    }
    if (context->generated) artifacts = &context->compiler_result.artifacts;
    if (run_all) {
        result = solar_test_run_all_with_artifacts_and_progress(
            &context->project, context->profile_name, artifacts,
            context->progress_observer,
            &context->test_summary
        );
        context->has_test_summary = true;
        if (result.status == SOLAR_STATUS_OK &&
            context->test_summary.failed > 0U) {
            size_t index;

            for (index = 0U; index < context->test_summary.count; index++) {
                if (context->test_summary.results[index].result.status !=
                    SOLAR_STATUS_OK) {
                    result = context->test_summary.results[index].result;
                    break;
                }
            }
        }
    } else {
        solar_progress_item(context->progress_observer, 0U, 1U, test_name);
        result = solar_test_run_with_artifacts_and_progress(
            &context->project, test_name, context->profile_name, artifacts,
            context->progress_observer,
            &context->test_result
        );
        context->has_test_result = true;
    }
    record_step(context, step_name, result, started, false);
    return result;
}

static SolarResult append_test_result(
    SolarTestSummary *summary,
    SolarTestResult *test_result
)
{
    SolarTestResult *items = realloc(
        summary->results, (summary->count + 1U) * sizeof(*items)
    );

    if (items == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not grow build simulation results",
        "free memory and try again"
    );
    summary->results = items;
    summary->results[summary->count++] = *test_result;
    if (test_result->result.status == SOLAR_STATUS_OK) summary->passed++;
    else summary->failed++;
    solar_test_result_init(test_result);
    return solar_result_ok();
}

SolarResult solar_build_context_test_sequence(
    SolarBuildContext *context,
    bool stop_on_failure
)
{
    const SolarGeneratedArtifacts *artifacts = NULL;
    SolarResult first_failure = solar_result_ok();
    SolarResult result;
    uint64_t started;
    size_t index;

    if (context == NULL || context->project.root == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot run simulations without a loaded project",
            "load and validate the project first"
        );
    }
    result = ensure_generated(context);
    if (result.status != SOLAR_STATUS_OK) return result;
    started = monotonic_nanoseconds();
    if (context->dry_run) {
        record_step(context, "sim", solar_result_ok(), started, false);
        return solar_result_ok();
    }
    solar_test_summary_free(&context->test_summary);
    solar_test_summary_init(&context->test_summary);
    if (context->generated) artifacts = &context->compiler_result.artifacts;
    for (index = 0U; index < context->project.config.test_count; index++) {
        SolarTestResult simulation;

        solar_test_result_init(&simulation);
        solar_progress_item(
            context->progress_observer, index,
            context->project.config.test_count,
            context->project.config.tests[index].name
        );
        result = solar_test_run_with_artifacts_and_progress(
            &context->project,
            context->project.config.tests[index].name,
            context->profile_name,
            artifacts,
            context->progress_observer,
            &simulation
        );
        if (result.status != SOLAR_STATUS_OK &&
            first_failure.status == SOLAR_STATUS_OK) {
            first_failure = result;
        }
        result = append_test_result(&context->test_summary, &simulation);
        solar_test_result_free(&simulation);
        if (result.status != SOLAR_STATUS_OK) {
            solar_test_summary_free(&context->test_summary);
            record_step(context, "sim", result, started, false);
            return result;
        }
        if (stop_on_failure && first_failure.status != SOLAR_STATUS_OK) break;
    }
    context->has_test_summary = true;
    record_step(context, "sim", first_failure, started, false);
    return first_failure;
}

SolarResult solar_build_context_synthesize(SolarBuildContext *context)
{
    const SolarGeneratedArtifacts *artifacts = NULL;
    uint64_t started;
    SolarResult result;

    if (context == NULL || context->project.root == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot synthesize without a loaded project",
            "load and validate the project first"
        );
    }
    result = ensure_generated(context);
    if (result.status != SOLAR_STATUS_OK) return result;
    started = monotonic_nanoseconds();
    if (context->dry_run) {
        record_step(context, "synth", solar_result_ok(), started, false);
        return solar_result_ok();
    }
    if (context->generated) artifacts = &context->compiler_result.artifacts;
    result = solar_synthesis_run_with_artifacts(
        &context->project, context->profile_name, artifacts,
        &context->synthesis_result
    );
    context->has_synthesis_result = true;
    record_step(context, "synth", result, started, false);
    return result;
}

void solar_build_context_finish(
    SolarBuildContext *context,
    SolarResult result
)
{
    if (context == NULL) return;
    context->result = result;
    context->duration_ns = context->dry_run
        ? 0U
        : elapsed_since(context->started_ns);
    context->duration_ms = context->duration_ns / UINT64_C(1000000);
    context->finished_at = time(NULL);
    solar_progress_stage(
        context->progress_observer,
        result.status == SOLAR_STATUS_OK
            ? SOLAR_PROGRESS_COMPLETE : SOLAR_PROGRESS_FAILED,
        result.status == SOLAR_STATUS_OK
            ? "Simulation completed" : "Simulation failed",
        context->project.config.simulation.backend
    );
}

static const char *step_status(const SolarBuildStepResult *step)
{
    if (step->skipped) return "SKIP";
    return step->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL";
}

static void write_rule(FILE *file, char character)
{
    int index;

    for (index = 0; index < REPORT_WIDTH; index++) (void)fputc(character, file);
    (void)fputc('\n', file);
}

static void write_section(FILE *file, const char *title)
{
    (void)fprintf(file, "\n%s\n", title);
    write_rule(file, '-');
}

static void write_field(FILE *file, const char *label, const char *value)
{
    (void)fprintf(file, "  %-20s %s\n", label, value == NULL ? "unavailable" : value);
}

static void format_duration(uint64_t nanoseconds, char *text, size_t capacity)
{
    if (nanoseconds < UINT64_C(1000)) {
        (void)snprintf(text, capacity, "%" PRIu64 " ns", nanoseconds);
    } else if (nanoseconds < UINT64_C(1000000)) {
        (void)snprintf(
            text, capacity, "%.3f us", (double)nanoseconds / 1000.0
        );
    } else if (nanoseconds < UINT64_C(1000000000)) {
        (void)snprintf(
            text, capacity, "%.3f ms", (double)nanoseconds / 1000000.0
        );
    } else {
        (void)snprintf(
            text, capacity, "%.3f s", (double)nanoseconds / 1000000000.0
        );
    }
}

static void write_timing(
    FILE *file,
    const char *status,
    const char *label,
    uint64_t nanoseconds
)
{
    char human[32];

    format_duration(nanoseconds, human, sizeof(human));
    (void)fprintf(
        file, "  %-5s %-35s %12s  (%" PRIu64 " ns)\n",
        status, label, human, nanoseconds
    );
}

static void write_string_list(
    FILE *file,
    const char *label,
    const SolarStringList *list
)
{
    size_t index;

    (void)fprintf(file, "  %-20s %zu item(s)\n", label, list->count);
    for (index = 0U; index < list->count; index++) {
        (void)fprintf(file, "      - %s\n", list->items[index]);
    }
}

static void write_utc_time(FILE *file, const char *label, time_t value)
{
    struct tm utc;
    char text[32];

    if (value == (time_t)-1 || gmtime_r(&value, &utc) == NULL ||
        strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0U) {
        write_field(file, label, "unavailable");
        return;
    }
    write_field(file, label, text);
}

static void write_artifact(FILE *file, const char *type, const char *path)
{
    struct stat information;

    if (path == NULL) return;
    if (stat(path, &information) == 0 && S_ISREG(information.st_mode)) {
        (void)fprintf(
            file, "  %-20s %s (%llu bytes)\n", type, path,
            (unsigned long long)information.st_size
        );
    } else {
        (void)fprintf(file, "  %-20s %s (unavailable)\n", type, path);
    }
}

static void write_system(FILE *file)
{
    SolarSystemInfo information;
    SolarResult result = solar_system_info_collect(&information);
    char count[64];
    char memory[96];

    if (result.status != SOLAR_STATUS_OK) {
        write_field(file, "Host", "unavailable");
        return;
    }
    write_field(file, "Hostname", information.hostname);
    write_field(file, "Operating system", information.operating_system);
    write_field(file, "Kernel", information.kernel);
    write_field(file, "Architecture", information.architecture);
    write_field(file, "CPU model", information.cpu_model);
    (void)snprintf(count, sizeof(count), "%ld online", information.logical_cpus);
    write_field(file, "Logical CPUs", count);
    (void)snprintf(
        memory, sizeof(memory), "%.2f GiB (%" PRIu64 " bytes)",
        (double)information.memory_total_bytes / 1073741824.0,
        information.memory_total_bytes
    );
    write_field(file, "Memory", memory);
    (void)snprintf(
        count, sizeof(count), "%ld bytes", information.page_size_bytes
    );
    write_field(file, "Page size", count);
    write_field(file, "Solar compiler", SOLAR_BUILD_COMPILER);
    write_field(
        file, "Solar build type",
        SOLAR_BUILD_TYPE[0] == '\0' ? "unspecified" : SOLAR_BUILD_TYPE
    );
}

static void write_tool_value(
    FILE *file,
    const char *name,
    const char *kind,
    const char *value
)
{
    const size_t width = 47U;
    const char *cursor = value == NULL ? "unavailable" : value;
    bool first = true;

    while (*cursor != '\0') {
        size_t remaining = strlen(cursor);
        size_t length = remaining > width ? width : remaining;

        if (remaining > width) {
            size_t split = length;

            while (split > 0U && cursor[split] != ' ') split--;
            if (split > 0U) {
                length = split;
            } else {
                while (length < remaining && cursor[length] != ' ') length++;
            }
        }
        (void)fprintf(
            file, "  %-18s %-9s %.*s\n",
            first ? name : "", first ? kind : "", (int)length, cursor
        );
        cursor += length;
        while (*cursor == ' ') cursor++;
        first = false;
    }
    if (first) (void)fprintf(file, "  %-18s %-9s -\n", name, kind);
}

static void write_generated_artifacts(
    FILE *file,
    const SolarGeneratedArtifacts *artifacts,
    bool assembly_is_generated
)
{
    size_t index;

    if (assembly_is_generated) {
        write_artifact(file, "generated assembly", artifacts->assembly_path);
    }
    write_artifact(file, "processor RTL", artifacts->rtl_sources.count > 0U
        ? artifacts->rtl_sources.items[0] : NULL);
    for (index = 1U; index < artifacts->rtl_sources.count; index++) {
        write_artifact(file, "support RTL", artifacts->rtl_sources.items[index]);
    }
    write_artifact(file, "instruction image", artifacts->instruction_image_path);
    write_artifact(file, "data image", artifacts->data_image_path);
    write_artifact(file, "generated testbench", artifacts->testbench_path);
}

static void write_context_artifacts(
    FILE *file,
    const SolarBuildContext *context
)
{
    size_t index;

    if (context->generated) {
        write_generated_artifacts(
            file, &context->compiler_result.artifacts,
            strcmp(context->project.config.project.language, "asm") != 0
        );
    }
    if (context->has_test_result) {
        if (context->test_result.waveform_path == NULL &&
            context->test_result.result.status == SOLAR_STATUS_OK) {
            write_field(file, "waveform", "not generated");
        } else {
            write_artifact(file, "waveform", context->test_result.waveform_path);
        }
    }
    if (context->has_test_summary) {
        for (index = 0U; index < context->test_summary.count; index++) {
            const SolarTestResult *simulation =
                &context->test_summary.results[index];
            const char *label = simulation->name == NULL
                ? "waveform" : simulation->name;

            if (simulation->waveform_path == NULL &&
                simulation->result.status == SOLAR_STATUS_OK) {
                write_field(file, label, "not generated");
            } else {
                write_artifact(file, label, simulation->waveform_path);
            }
        }
    }
    if (context->has_synthesis_result) {
        write_artifact(file, "netlist", context->synthesis_result.netlist_path);
        write_artifact(file, "synthesis report", context->synthesis_result.report_path);
    }
}

static void write_tools(FILE *file, const SolarBuildContext *context)
{
    SolarToolReport *reports = NULL;
    size_t count = 0U;
    size_t index;
    SolarResult result;

    if (context->dry_run) {
        (void)fprintf(file, "  %-18s %-9s %s\n", "simulation", "NOT PROBED",
            context->project.config.simulation.backend == NULL
                ? "unavailable" : context->project.config.simulation.backend);
        (void)fprintf(file, "  %-18s %-9s %s\n", "synthesis", "NOT PROBED",
            context->project.config.synthesis.backend == NULL
                ? "unavailable" : context->project.config.synthesis.backend);
        if (context->project.config.compiler.backend != NULL) {
            (void)fprintf(file, "  %-18s %-9s %s\n", "compiler", "NOT PROBED",
                context->project.config.compiler.backend);
        }
        return;
    }
    if (context->project.config.simulation.backend == NULL) {
        (void)fprintf(file, "  unavailable (project configuration did not load)\n");
        return;
    }
    result = solar_backend_doctor_project(
        &context->project, false, &reports, &count
    );
    if (result.status != SOLAR_STATUS_OK) {
        (void)fprintf(file, "  %-18s %-9s %s\n", "tool probe", "ERROR",
            result.diagnostic.message);
        return;
    }
    (void)fprintf(file, "  %-18s %-9s %s\n", "Tool", "Status", "Executable");
    for (index = 0U; index < count; index++) {
        write_tool_value(
            file, reports[index].name,
            reports[index].available ? "READY" : "MISSING",
            reports[index].available ? reports[index].path : "-"
        );
        if (reports[index].version != NULL) {
            write_tool_value(file, "", "version", reports[index].version);
        }
    }
    solar_backend_tool_reports_free(reports, count);
    if (context->project.config.compiler.backend != NULL) {
        write_tool_value(file, "YANC", "root",
            context->compiler_result.toolchain_root == NULL
                ? "unavailable" : context->compiler_result.toolchain_root);
        write_tool_value(file, "", "version",
            context->compiler_result.toolchain_version == NULL
                ? "unknown" : context->compiler_result.toolchain_version);
        for (index = 0U; index < context->compiler_result.stage_count; index++) {
            write_tool_value(file,
                context->compiler_result.stages[index].name,
                "used",
                context->compiler_result.stages[index].executable_path == NULL
                    ? "unknown"
                    : context->compiler_result.stages[index].executable_path);
        }
    }
}

static const char *failed_log_path(const SolarBuildContext *context)
{
    size_t index;

    if (context->compiler_result.stage_count > 0U) {
        for (index = context->compiler_result.stage_count; index > 0U; index--) {
            if (context->compiler_result.stages[index - 1U].result.status !=
                SOLAR_STATUS_OK) {
                return context->compiler_result.stages[index - 1U].log_path;
            }
        }
    }
    if (context->rtl_result.result.status != SOLAR_STATUS_OK) {
        return context->rtl_result.stderr_log;
    }
    return NULL;
}

static void write_log_paths(FILE *file, const SolarBuildContext *context)
{
    size_t index;

    if (context->rtl_result.stdout_log != NULL) {
        (void)fprintf(file, "  RTL stdout: %s\n", context->rtl_result.stdout_log);
        (void)fprintf(file, "  RTL stderr: %s\n", context->rtl_result.stderr_log);
    }
    for (index = 0U; index < context->compiler_result.stage_count; index++) {
        (void)fprintf(file, "  YANC %s: %s\n",
            context->compiler_result.stages[index].name,
            context->compiler_result.stages[index].log_path);
    }
    if (context->has_test_result && context->test_result.name != NULL) {
        (void)fprintf(file, "  simulation %s: %s/.solar/logs/tests/%s/\n",
            context->test_result.name, context->project.root,
            context->test_result.name);
    }
    if (context->has_test_summary) {
        for (index = 0U; index < context->test_summary.count; index++) {
            const char *name = context->test_summary.results[index].name;

            if (name != NULL) (void)fprintf(
                file, "  simulation %s: %s/.solar/logs/tests/%s/\n",
                name, context->project.root, name
            );
        }
    }
    if (context->has_synthesis_result) {
        (void)fprintf(file, "  Yosys stdout: %s/.solar/logs/yosys.stdout.log\n",
            context->project.root);
        (void)fprintf(file, "  Yosys stderr: %s/.solar/logs/yosys.stderr.log\n",
            context->project.root);
    }
}

static void write_failure_tail(FILE *file, const char *path)
{
    int descriptor;
    struct stat information;
    char buffer[8193];
    off_t offset;
    ssize_t count;
    char *start;
    char *cursor;
    size_t lines = 0U;

    if (path == NULL) return;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) return;
    if (fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0) {
        (void)close(descriptor);
        return;
    }
    offset = information.st_size > 8192 ? information.st_size - 8192 : 0;
    if (lseek(descriptor, offset, SEEK_SET) < 0) {
        (void)close(descriptor);
        return;
    }
    count = read(descriptor, buffer, sizeof(buffer) - 1U);
    (void)close(descriptor);
    if (count <= 0) return;
    buffer[(size_t)count] = '\0';
    for (cursor = buffer; *cursor != '\0'; cursor++) {
        unsigned char value = (unsigned char)*cursor;

        if (*cursor == '\r') *cursor = '\n';
        else if (value < 32U && *cursor != '\n' && *cursor != '\t') *cursor = '?';
        else if (value == 127U) *cursor = '?';
    }
    start = buffer + strlen(buffer);
    while (start > buffer && lines < 20U) {
        start--;
        if (*start == '\n') lines++;
    }
    if (start > buffer) start++;
    (void)fprintf(file, "stderr_tail: %s\n", path);
    (void)fprintf(file, "%s", start);
    if (*start != '\0' && start[strlen(start) - 1U] != '\n') {
        (void)fprintf(file, "\n");
    }
}

static const SolarTestResult *failed_simulation(
    const SolarBuildContext *context
)
{
    size_t index;

    if (context->has_test_result &&
        context->test_result.result.status != SOLAR_STATUS_OK) {
        return &context->test_result;
    }
    if (!context->has_test_summary) return NULL;
    for (index = 0U; index < context->test_summary.count; index++) {
        if (context->test_summary.results[index].result.status !=
            SOLAR_STATUS_OK) {
            return &context->test_summary.results[index];
        }
    }
    return NULL;
}

static void write_context_failure_tail(
    FILE *file,
    const SolarBuildContext *context
)
{
    const char *path = failed_log_path(context);
    const SolarTestResult *simulation;
    char *relative = NULL;
    char *absolute = NULL;

    if (path != NULL) {
        write_failure_tail(file, path);
        return;
    }
    simulation = failed_simulation(context);
    if (simulation != NULL && simulation->name != NULL) {
        const char *backend = context->project.config.simulation.backend;
        const char *filename = simulation->failure_kind ==
            SOLAR_TEST_FAILURE_SIMULATION_COMPILE
            ? (backend != NULL && strcmp(backend, "verilator") == 0
                ? "verilator.stderr.log" : "iverilog.stderr.log")
            : (backend != NULL && strcmp(backend, "verilator") == 0
                ? "simulation.stderr.log" : "vvp.stderr.log");
        size_t length = strlen(".solar/logs/tests//") +
            strlen(simulation->name) + strlen(filename) + 1U;

        relative = malloc(length);
        if (relative != NULL) {
            (void)snprintf(relative, length, ".solar/logs/tests/%s/%s",
                simulation->name, filename);
            if (solar_filesystem_join(
                    context->project.root, relative, &absolute
                ).status == SOLAR_STATUS_OK) {
                write_failure_tail(file, absolute);
            }
        }
    } else if (context->has_synthesis_result) {
        (void)solar_filesystem_join(
            context->project.root, ".solar/logs/yosys.stderr.log", &absolute
        );
        write_failure_tail(file, absolute);
    }
    free(absolute);
    free(relative);
}

static void write_configuration(FILE *file, const SolarBuildContext *context)
{
    const SolarConfig *config = &context->project.config;
    const SolarProfile *profile = NULL;
    SolarEffectiveConfig effective;
    size_t index;
    SolarResult result;

    write_field(file, "Project root", context->project.root);
    write_field(file, "Manifest",
        context->project.manifest_path == NULL
            ? "unavailable" : context->project.manifest_path);
    if (config->project.name == NULL) {
        write_field(file, "Configuration", "unavailable");
        return;
    }
    write_field(file, "Project", config->project.name);
    write_field(file, "Language", config->project.language);
    {
        char format[32];

        (void)snprintf(format, sizeof(format), "%u", config->manifest_format);
        write_field(file, "Manifest format", format);
    }
    write_field(file, "Simulation backend", config->simulation.backend);
    write_field(file, "Synthesis backend", config->synthesis.backend);
    write_field(file, "Synthesis top",
        config->synthesis.top == NULL ? "none" : config->synthesis.top);
    write_string_list(file, "sources.rtl", &config->sources.rtl);
    (void)fprintf(file, "  %-20s %zu configured\n", "Simulations",
        config->test_count);
    for (index = 0U; index < config->test_count; index++) {
        size_t item;

        (void)fprintf(file, "      - %s | top=%s | driver=%s\n",
            config->tests[index].name, config->tests[index].top,
            config->tests[index].driver == NULL
                ? "verilog" : config->tests[index].driver);
        for (item = 0U; item < config->tests[index].sources.count; item++) {
            (void)fprintf(file, "          source: %s\n",
                config->tests[index].sources.items[item]);
        }
        for (item = 0U; item < config->tests[index].include_dirs.count; item++) {
            (void)fprintf(file, "          include: %s\n",
                config->tests[index].include_dirs.items[item]);
        }
        for (item = 0U; item < config->tests[index].defines.count; item++) {
            (void)fprintf(file, "          define: %s\n",
                config->tests[index].defines.items[item]);
        }
        (void)fprintf(file, "          waveform: %s\n",
            config->tests[index].waveform == NULL
                ? "none" : config->tests[index].waveform);
    }
    if (context->profile_name != NULL) {
        profile = solar_config_find_profile(config, context->profile_name);
    }
    solar_effective_config_init(&effective);
    result = solar_config_build_effective(config, profile, NULL, &effective);
    if (result.status == SOLAR_STATUS_OK) {
        write_string_list(file, "effective.include_dirs", &effective.include_dirs);
        write_string_list(file, "effective.defines", &effective.defines);
    } else {
        (void)fprintf(file, "effective configuration: unavailable (%s)\n",
            result.diagnostic.message);
    }
    solar_effective_config_free(&effective);
}

static void write_statistics_value(
    FILE *file,
    const char *label,
    const SolarGenericSynthesisStatistics *statistics,
    SolarSynthesisStatisticsField field,
    uint64_t value
)
{
    if (solar_synthesis_statistics_has_field(statistics, field)) {
        (void)fprintf(file, "  %-32s %12" PRIu64 "\n", label, value);
    } else {
        (void)fprintf(file, "  %-32s %12s\n", label, "not reported");
    }
}

static const char *statistics_status(
    const SolarBuildContext *context,
    const SolarGenericSynthesisStatistics *statistics
)
{
    if (!context->has_synthesis_result ||
        context->synthesis_result.result.status != SOLAR_STATUS_OK ||
        !statistics->available) {
        return "NOT AVAILABLE";
    }
    return statistics->completeness == SOLAR_SYNTHESIS_STATISTICS_COMPLETE
        ? "COMPLETE" : "PARTIAL";
}

static void write_statistics_metadata(
    FILE *file,
    const char *label,
    const char *value
)
{
    const size_t width = 54U;
    const char *cursor = value == NULL ? "not reported" : value;
    bool first = true;

    do {
        size_t remaining = strlen(cursor);
        size_t length = remaining;

        if (length > width) {
            size_t split = width - 1U;

            while (split > 0U && cursor[split] != ' ' &&
                   cursor[split] != '/') {
                split--;
            }
            length = split > 0U ? split + 1U : width;
        }
        (void)fprintf(
            file, "  %-20s %.*s\n", first ? label : "",
            (int)length, cursor
        );
        cursor += length;
        first = false;
    } while (*cursor != '\0');
}

static void write_synthesis_statistics(
    FILE *file,
    const SolarBuildContext *context
)
{
    const SolarGenericSynthesisStatistics *statistics =
        &context->synthesis_result.statistics;
    size_t index;

    write_section(file, "GENERIC SYNTHESIS STATISTICS");
    write_statistics_metadata(
        file, "Status", statistics_status(context, statistics)
    );
    write_statistics_metadata(file, "Tool",
        statistics->tool == NULL ? "Yosys" : statistics->tool);
    write_statistics_metadata(file, "Version", statistics->tool_version);
    write_statistics_metadata(file, "Top",
        statistics->top == NULL
            ? context->project.config.synthesis.top : statistics->top);
    write_statistics_metadata(
        file, "Source", statistics->report_path
    );
    write_statistics_metadata(
        file, "Data schema", "solar-generic-synthesis-statistics/1"
    );
    if (!statistics->available) {
        if (context->has_synthesis_result &&
            context->synthesis_result.result.status != SOLAR_STATUS_OK) {
            write_statistics_metadata(file, "Reason", "synthesis failed");
        } else if (context->has_synthesis_result &&
                   context->synthesis_result.statistics_result.status !=
                       SOLAR_STATUS_OK) {
            write_statistics_metadata(
                file, "Reason",
                context->synthesis_result.statistics_result.diagnostic.message
            );
        } else {
            write_statistics_metadata(file, "Reason", "synthesis was not run");
        }
        return;
    }
    (void)fprintf(
        file,
        "\n  DESIGN SUMMARY\n"
        "  %-32s %12s\n"
        "  -------------------------------- ------------\n",
        "Metric", "Value"
    );
    write_statistics_value(file, "Modules", statistics,
        SOLAR_SYNTHESIS_FIELD_MODULES, statistics->modules);
    write_statistics_value(file, "Wires", statistics,
        SOLAR_SYNTHESIS_FIELD_WIRES, statistics->wires);
    write_statistics_value(file, "Wire bits", statistics,
        SOLAR_SYNTHESIS_FIELD_WIRE_BITS, statistics->wire_bits);
    write_statistics_value(file, "Public wires", statistics,
        SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES, statistics->public_wires);
    write_statistics_value(file, "Public wire bits", statistics,
        SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS, statistics->public_wire_bits);
    write_statistics_value(file, "Memories", statistics,
        SOLAR_SYNTHESIS_FIELD_MEMORIES, statistics->memories);
    write_statistics_value(file, "Memory bits", statistics,
        SOLAR_SYNTHESIS_FIELD_MEMORY_BITS, statistics->memory_bits);
    write_statistics_value(file, "Processes", statistics,
        SOLAR_SYNTHESIS_FIELD_PROCESSES, statistics->processes);
    write_statistics_value(file, "Cells", statistics,
        SOLAR_SYNTHESIS_FIELD_CELLS, statistics->cells);
    (void)fprintf(
        file,
        "\n  CELL USAGE\n"
        "  %-32s %12s\n"
        "  -------------------------------- ------------\n",
        "Cell type", "Count"
    );
    if (statistics->cell_type_count == 0U) {
        (void)fprintf(file, "  %-32s %12s\n", "-", "not reported");
    }
    for (index = 0U; index < statistics->cell_type_count; index++) {
        (void)fprintf(
            file, "  %-32s %12" PRIu64 "\n",
            statistics->cell_types[index].type,
            statistics->cell_types[index].count
        );
    }
}

static bool simulation_uses_cocotb(
    const SolarBuildContext *context,
    const SolarTestResult *simulation
)
{
    size_t index;

    if (simulation->name == NULL) return false;
    for (index = 0U; index < context->project.config.test_count; index++) {
        const SolarTest *test = &context->project.config.tests[index];

        if (strcmp(test->name, simulation->name) == 0) {
            return test->driver != NULL && strcmp(test->driver, "cocotb") == 0;
        }
    }
    return false;
}

static const char *simulation_compile_tool(
    const SolarBuildContext *context,
    const SolarTestResult *simulation
)
{
    if (simulation_uses_cocotb(context, simulation)) return "cocotb/Verilator";
    return context->project.config.simulation.backend == NULL
        ? "unknown" : context->project.config.simulation.backend;
}

static const char *simulation_runtime_tool(
    const SolarBuildContext *context,
    const SolarTestResult *simulation
)
{
    const char *backend = context->project.config.simulation.backend;

    if (simulation_uses_cocotb(context, simulation)) return "cocotb test";
    if (backend != NULL && strcmp(backend, "iverilog") == 0) return "vvp";
    if (backend != NULL && strcmp(backend, "verilator") == 0) {
        return "Verilator executable";
    }
    return "unknown";
}

static void write_stage_results(FILE *file, const SolarBuildContext *context)
{
    size_t index;

    (void)fprintf(file, "  Pipeline stages\n");
    for (index = 0U; index < context->step_count; index++) {
        write_timing(
            file,
            step_status(&context->steps[index]),
            context->steps[index].name,
            context->steps[index].duration_ns
        );
    }
    write_timing(file, "TOTAL", "Solar operation", context->duration_ns);

    (void)fprintf(file, "\n  External tool invocations\n");
    if (context->rtl_result.backend != NULL) {
        char label[96];

        (void)snprintf(
            label, sizeof(label), "RTL elaboration: %s",
            context->rtl_result.backend
        );
        write_timing(
            file,
            context->rtl_result.result.status == SOLAR_STATUS_OK
                ? "PASS" : "FAIL",
            label,
            context->rtl_result.duration_ns
        );
    }
    if (context->compiler_result.stage_count > 0U) {
        for (index = 0U; index < context->compiler_result.stage_count; index++) {
            char label[96];

            (void)snprintf(
                label, sizeof(label), "YANC: %s",
                context->compiler_result.stages[index].name
            );
            write_timing(
                file,
                context->compiler_result.stages[index].result.status ==
                    SOLAR_STATUS_OK ? "PASS" : "FAIL",
                label,
                context->compiler_result.stages[index].duration_ns
            );
        }
    }
    if (context->has_test_result) {
        const SolarTestResult *simulation = &context->test_result;
        char label[128];

        (void)snprintf(label, sizeof(label), "Sim %s: %s compile",
            simulation->name == NULL ? "unknown" : simulation->name,
            simulation_compile_tool(context, simulation));
        write_timing(file, "TOOL", label, simulation->compile_duration_ns);
        (void)snprintf(label, sizeof(label), "Sim %s: %s runtime",
            simulation->name == NULL ? "unknown" : simulation->name,
            simulation_runtime_tool(context, simulation));
        write_timing(file,
            simulation->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL",
            label, simulation->simulation_duration_ns);
        (void)snprintf(label, sizeof(label), "Sim %s: process total",
            simulation->name == NULL ? "unknown" : simulation->name);
        write_timing(file, "TOTAL", label,
            simulation->total_process_duration_ns);
        (void)fprintf(file, "        result: %s | failure: %s\n",
            simulation->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL",
            solar_test_failure_kind_name(simulation->failure_kind));
    } else if (context->has_test_summary) {
        for (index = 0U; index < context->test_summary.count; index++) {
            const SolarTestResult *simulation =
                &context->test_summary.results[index];
            char label[128];

            (void)snprintf(label, sizeof(label), "Sim %s: %s compile",
                simulation->name == NULL ? "unknown" : simulation->name,
                simulation_compile_tool(context, simulation));
            write_timing(file, "TOOL", label, simulation->compile_duration_ns);
            (void)snprintf(label, sizeof(label), "Sim %s: %s runtime",
                simulation->name == NULL ? "unknown" : simulation->name,
                simulation_runtime_tool(context, simulation));
            write_timing(file,
                simulation->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL",
                label, simulation->simulation_duration_ns);
            (void)snprintf(label, sizeof(label), "Sim %s: process total",
                simulation->name == NULL ? "unknown" : simulation->name);
            write_timing(file, "TOTAL", label,
                simulation->total_process_duration_ns);
            (void)fprintf(file, "        result: %s | failure: %s\n",
                simulation->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL",
                solar_test_failure_kind_name(simulation->failure_kind));
        }
        (void)fprintf(file, "\n  Simulation summary: %zu passed, %zu failed\n",
            context->test_summary.passed, context->test_summary.failed);
    }
    if (context->has_synthesis_result) {
        write_timing(file,
            context->synthesis_result.result.status == SOLAR_STATUS_OK
                ? "PASS" : "FAIL",
            "Synthesis: yosys",
            context->synthesis_result.duration_ns
        );
    }
    (void)fprintf(
        file,
        "\n  Scope: monotonic wall time. Process measurements include launch, "
        "wait, and output drainage.\n"
        "  Pipeline times also include Solar validation, staging, and artifact "
        "publication.\n"
        "  A single run is not a statistical benchmark; repeat under controlled "
        "load for publication.\n"
    );
}

static SolarResult report_paths(
    const SolarProject *project,
    char **final_path,
    char **temporary_path
)
{
    SolarResult result = solar_filesystem_prepare_generated_directory(
        project->root, ".solar/state"
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_filesystem_join(
        project->root, ".solar/state/last-report.txt", final_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_filesystem_join(
        project->root, ".solar/state/last-report.txt.tmp", temporary_path
    );
}

SolarResult solar_build_report_write(const SolarBuildContext *context)
{
    char *path = NULL;
    char *temporary = NULL;
    FILE *file = NULL;
    int descriptor = -1;
    SolarResult result;

    if (context == NULL || context->project.root == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot write a report without a loaded project",
            "run a Solar operation from a project first"
        );
    }
    result = report_paths(&context->project, &path, &temporary);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    descriptor = open(
        temporary,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    if (descriptor >= 0) file = fdopen(descriptor, "w");
    if (descriptor < 0 || file == NULL) {
        if (descriptor >= 0) (void)close(descriptor);
        descriptor = -1;
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot create the Solar operation report",
            "check write permissions for the project .solar directory"
        );
        goto cleanup;
    }
    descriptor = -1;
    write_rule(file, '=');
    (void)fprintf(file, "SOLAR LAST BUILD REPORT\n");
    write_rule(file, '=');
    write_section(file, "OVERVIEW");
    write_field(file, "Status",
        context->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL");
    {
        char command[128];
        char status_code[32];
        char duration[96];

        (void)snprintf(command, sizeof(command), "solar %s",
            context->operation == NULL ? "unknown" : context->operation);
        write_field(file, "Command", command);
        (void)snprintf(status_code, sizeof(status_code), "%d",
            (int)context->result.status);
        write_field(file, "Status code", status_code);
        format_duration(context->duration_ns, duration, sizeof(duration));
        (void)snprintf(
            duration + strlen(duration), sizeof(duration) - strlen(duration),
            " (%" PRIu64 " ns)", context->duration_ns
        );
        write_field(file, "Total wall time", duration);
    }
    write_field(file, "Solar version", SOLAR_VERSION);
    write_field(file, "Profile",
        context->profile_name != NULL
            ? context->profile_name
            : (context->requested_profile == NULL
                ? "none" : context->requested_profile));
    write_field(file, "Dry run", context->dry_run ? "yes" : "no");
    write_utc_time(file, "Started (UTC)", context->started_at);
    write_utc_time(file, "Finished (UTC)", context->finished_at);

    write_section(file, "HOST ENVIRONMENT");
    write_system(file);
    (void)fprintf(file,
        "  Host data is a snapshot collected when this report was written.\n");

    write_section(file, "EDA TOOLCHAIN");
    write_tools(file, context);
    (void)fprintf(file,
        "  Versions are captured after the build; configured tools without a "
        "timing row were not executed.\n");

    write_section(file, "PROJECT CONFIGURATION");
    write_configuration(file, context);

    write_section(file, "TIMING BREAKDOWN");
    write_stage_results(file, context);

    write_synthesis_statistics(file, context);

    write_section(file, "ARTIFACTS");
    write_context_artifacts(file, context);

    write_section(file, "LOGS");
    write_log_paths(file, context);
    if (context->result.status != SOLAR_STATUS_OK) {
        write_section(file, "FAILURE DIAGNOSTIC");
        (void)fprintf(
            file, "  Error: %s\n",
            context->result.diagnostic.message[0] == '\0'
                ? solar_status_name(context->result.status)
                : context->result.diagnostic.message
        );
        if (context->result.diagnostic.hint[0] != '\0') {
            (void)fprintf(
                file, "  Hint: %s\n", context->result.diagnostic.hint
            );
        }
        write_context_failure_tail(file, context);
    }
    (void)fprintf(file, "\n");
    write_rule(file, '=');
    (void)fprintf(file,
        "Report file: .solar/state/last-report.txt\n"
        "Use controlled repetitions and report aggregation for benchmark claims.\n"
    );
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) {
        (void)fclose(file);
        file = NULL;
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot finish the Solar operation report",
            "check free space and project filesystem permissions"
        );
        goto cleanup;
    }
    if (fclose(file) != 0) {
        file = NULL;
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot finish the Solar operation report",
            "check free space and project filesystem permissions"
        );
        goto cleanup;
    }
    file = NULL;
    if (rename(temporary, path) != 0) {
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot publish the Solar operation report",
            "check project filesystem permissions"
        );
        goto cleanup;
    }
    result = solar_result_ok();

cleanup:
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    if (result.status != SOLAR_STATUS_OK && temporary != NULL) {
        (void)unlink(temporary);
    }
    free(temporary);
    free(path);
    return result;
}

SolarResult solar_build_report_read(
    const char *start_path,
    char **report_text_out
)
{
    char *root = NULL;
    char *path = NULL;
    FILE *file = NULL;
    int descriptor = -1;
    struct stat information;
    char *text = NULL;
    size_t count;
    SolarResult result;

    if (report_text_out != NULL) *report_text_out = NULL;
    if (start_path == NULL || report_text_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot read a report without output storage",
            "provide the current project path"
        );
    }
    result = solar_project_find_root(start_path, &root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_join(
        root, ".solar/state/last-report.txt", &path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        result = solar_result_error(
            errno == ENOENT ? SOLAR_STATUS_NOT_FOUND : SOLAR_STATUS_IO_ERROR,
            errno == ENOENT ? "no Solar build report is available" :
                "cannot open the Solar operation report",
            errno == ENOENT ?
                "run solar build rtl, sim, synth, or full first" :
                "check project filesystem permissions"
        );
        goto cleanup;
    }
    if (fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        information.st_size > REPORT_SIZE_LIMIT) {
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "the Solar operation report is not a regular file below 16 MiB",
            "remove the invalid .solar/state/last-report.txt file"
        );
        goto cleanup;
    }
    text = malloc((size_t)information.st_size + 1U);
    if (text == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate report text",
            "free memory and try again"
        );
        goto cleanup;
    }
    file = fdopen(descriptor, "r");
    if (file == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot open the Solar operation report",
            "check project filesystem permissions"
        );
        goto cleanup;
    }
    descriptor = -1;
    count = fread(text, 1U, (size_t)information.st_size, file);
    if (count != (size_t)information.st_size || ferror(file) != 0) {
        result = solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot read the Solar operation report",
            "check project filesystem permissions"
        );
        goto cleanup;
    }
    text[count] = '\0';
    *report_text_out = text;
    text = NULL;
    result = solar_result_ok();

cleanup:
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    free(text);
    free(path);
    free(root);
    return result;
}
