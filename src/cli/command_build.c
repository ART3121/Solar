#include "commands.h"

#include "solar/build.h"

#include "progress.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    BUILD_TARGET_NONE,
    BUILD_TARGET_RTL,
    BUILD_TARGET_SIM,
    BUILD_TARGET_SYNTH,
    BUILD_TARGET_FULL
} BuildTarget;

typedef struct {
    BuildTarget target;
    const char *target_name;
    const char *simulation_name;
    const char *profile_name;
    bool all;
    bool list;
    bool dry_run;
    bool no_progress;
    bool verbose;
} BuildOptions;

static SolarResult argument_error(const char *message, const char *hint)
{
    return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT, message, hint
    );
}

static BuildTarget target_from_name(const char *name)
{
    if (strcmp(name, "rtl") == 0) return BUILD_TARGET_RTL;
    if (strcmp(name, "sim") == 0) return BUILD_TARGET_SIM;
    if (strcmp(name, "synth") == 0) return BUILD_TARGET_SYNTH;
    if (strcmp(name, "full") == 0) return BUILD_TARGET_FULL;
    return BUILD_TARGET_NONE;
}

static SolarResult parse_options(
    int argument_count,
    char *const arguments[],
    BuildOptions *options
)
{
    int index;

    (void)memset(options, 0, sizeof(*options));
    if (argument_count == 0) return argument_error(
        "solar build requires a target",
        "choose rtl, sim, synth, or full"
    );
    options->target = target_from_name(arguments[0]);
    options->target_name = arguments[0];
    if (options->target == BUILD_TARGET_NONE) return argument_error(
        "unknown solar build target",
        "choose rtl, sim, synth, or full"
    );
    for (index = 1; index < argument_count; index++) {
        const char *argument = arguments[index];

        if (strcmp(argument, "--profile") == 0) {
            if (options->profile_name != NULL || index + 1 >= argument_count ||
                arguments[index + 1][0] == '\0' ||
                strncmp(arguments[index + 1], "--", 2U) == 0) {
                return argument_error(
                    "invalid solar build profile",
                    "use --profile <profile> once"
                );
            }
            options->profile_name = arguments[++index];
        } else if (strcmp(argument, "--dry-run") == 0) {
            if (options->dry_run) return argument_error(
                "--dry-run may be specified only once",
                "use one --dry-run option"
            );
            options->dry_run = true;
        } else if (strcmp(argument, "--all") == 0) {
            if (options->all) return argument_error(
                "--all may be specified only once",
                "use solar build sim --all"
            );
            options->all = true;
        } else if (strcmp(argument, "--list") == 0) {
            if (options->list) return argument_error(
                "--list may be specified only once",
                "use solar build sim --list"
            );
            options->list = true;
        } else if (strcmp(argument, "--no-progress") == 0) {
            if (options->no_progress) return argument_error(
                "--no-progress may be specified only once",
                "use one --no-progress option"
            );
            options->no_progress = true;
        } else if (strcmp(argument, "--verbose") == 0) {
            if (options->verbose) return argument_error(
                "--verbose may be specified only once",
                "use one --verbose option"
            );
            options->verbose = true;
        } else if (argument[0] == '-') {
            return argument_error(
                "unknown solar build option",
                "run solar build --help for usage"
            );
        } else if (options->simulation_name != NULL) {
            return argument_error(
                "only one simulation name may be provided",
                "run one named simulation or use --all"
            );
        } else {
            options->simulation_name = argument;
        }
    }
    if (options->target != BUILD_TARGET_SIM &&
        (options->simulation_name != NULL || options->all || options->list)) {
        return argument_error(
            "simulation selection requires the sim target",
            "use solar build sim [<name>|--all|--list]"
        );
    }
    if (options->target != BUILD_TARGET_SIM &&
        options->target != BUILD_TARGET_FULL &&
        (options->no_progress || options->verbose)) {
        return argument_error(
            "progress options require the sim or full target",
            "use --no-progress or --verbose with solar build sim/full"
        );
    }
    if ((options->all && options->list) ||
        ((options->all || options->list) &&
         options->simulation_name != NULL)) {
        return argument_error(
            "simulation name, --all, and --list are mutually exclusive",
            "choose one simulation selection mode"
        );
    }
    if (options->list &&
        (options->profile_name != NULL || options->dry_run)) {
        return argument_error(
            "--list cannot be combined with execution options",
            "use solar build sim --list"
        );
    }
    return solar_result_ok();
}

static bool is_default_simulation(
    const SolarConfig *config,
    const SolarTest *simulation
)
{
    if (config->project.default_test != NULL) {
        return strcmp(config->project.default_test, simulation->name) == 0;
    }
    return config->test_count == 1U;
}

static SolarResult list_simulations(const char *start_path)
{
    SolarProject project;
    SolarResult result;
    size_t index;

    solar_project_init(&project);
    result = solar_project_load(start_path, &project);
    if (result.status == SOLAR_STATUS_OK) {
        solar_cli_print_legacy_warning(&project);
        result = solar_project_validate(&project);
    }
    if (result.status == SOLAR_STATUS_OK) {
        (void)printf("Simulations:\n");
        for (index = 0U; index < project.config.test_count; index++) {
            const SolarTest *simulation = &project.config.tests[index];

            (void)printf("  %-12s top: %s", simulation->name, simulation->top);
            if (simulation->kind == SOLAR_TEST_GENERATED) (void)printf(" [YANC]");
            if (simulation->driver != NULL &&
                strcmp(simulation->driver, "cocotb") == 0) {
                (void)printf(" [cocotb]");
            }
            if (is_default_simulation(&project.config, simulation)) {
                (void)printf(" [default]");
            }
            (void)printf("\n");
        }
    }
    solar_project_free(&project);
    return result;
}

static void print_simulation_output(const SolarTestResult *simulation)
{
    size_t length;

    if (simulation->output == NULL || simulation->output[0] == '\0') return;
    length = strlen(simulation->output);
    (void)printf("Simulation output (%s):\n%s",
        simulation->name == NULL ? "?" : simulation->name,
        simulation->output);
    if (simulation->output[length - 1U] != '\n') (void)printf("\n");
    (void)printf("\n");
}

static void print_waveform_warning(
    const SolarConfig *config,
    const SolarTestResult *simulation
)
{
    const SolarTest *test;

    if (simulation->result.status != SOLAR_STATUS_OK ||
        simulation->waveform_path != NULL) {
        return;
    }
    test = solar_config_find_test(config, simulation->name);
    (void)fprintf(
        stderr,
        "solar: warning: simulation \"%s\" passed without generating a waveform\n"
        "expected waveform: %s\n"
        "hint: make $dumpfile use this exact VCD/FST path, or run solar scan "
        "after changing $dumpfile\n",
        simulation->name == NULL ? "?" : simulation->name,
        test == NULL || test->waveform == NULL ? "unknown" : test->waveform
    );
}

static void print_waveform_layout_warning(const SolarTestResult *simulation)
{
    const SolarDiagnostic *diagnostic =
        &simulation->waveform_layout_diagnostic;

    if (diagnostic->severity != SOLAR_DIAGNOSTIC_WARNING) return;
    (void)fprintf(
        stderr, "solar: warning: %s\n",
        diagnostic->message[0] == '\0'
            ? "the SAPHO waveform layout was not generated"
            : diagnostic->message
    );
    if (diagnostic->hint[0] != '\0') {
        (void)fprintf(stderr, "hint: %s\n", diagnostic->hint);
    }
}

static uint64_t compiler_duration(const SolarCompilerResult *compiler)
{
    uint64_t duration = 0U;
    size_t index;

    for (index = 0U; index < compiler->stage_count; index++) {
        duration += compiler->stages[index].duration_ns;
    }
    return duration;
}

static void add_simulation_timings(
    const SolarTestResult *simulation,
    uint64_t *resolution,
    uint64_t *compilation,
    uint64_t *execution,
    uint64_t *publication
)
{
    *resolution += simulation->source_resolution_duration_ns;
    *compilation += simulation->compile_duration_ns;
    *execution += simulation->simulation_duration_ns;
    *publication += simulation->publication_duration_ns;
}

static void print_simulation_summary(const SolarBuildContext *context)
{
    uint64_t resolution = 0U;
    uint64_t compilation = context->rtl_result.duration_ns;
    uint64_t execution = 0U;
    uint64_t publication = 0U;
    const char *backend = context->project.config.simulation.backend;
    size_t index;

    if (context->has_test_result) {
        add_simulation_timings(
            &context->test_result, &resolution, &compilation,
            &execution, &publication
        );
    }
    for (index = 0U; index < context->test_summary.count; index++) {
        add_simulation_timings(
            &context->test_summary.results[index], &resolution, &compilation,
            &execution, &publication
        );
    }
    (void)printf("\nSimulation summary:\n");
    (void)printf("  %-24s %8.2f s\n", "Project validation",
        (double)(context->load_duration_ns + context->validation_duration_ns) /
            1000000000.0);
    (void)printf("  %-24s %8.2f s\n", "Source resolution",
        (double)resolution / 1000000000.0);
    if (context->project.config.compiler.backend != NULL) {
        (void)printf("  %-24s %8.2f s\n", "YANC generation",
            (double)compiler_duration(&context->compiler_result) /
                1000000000.0);
    }
    (void)printf("  %-24s %8.2f s\n",
        backend != NULL && strcmp(backend, "verilator") == 0
            ? "Verilator compilation" : "Icarus compilation",
        (double)compilation / 1000000000.0);
    (void)printf("  %-24s %8.2f s\n", "Simulation execution",
        (double)execution / 1000000000.0);
    (void)printf("  %-24s %8.2f s\n", "Artifact publication",
        (double)publication / 1000000000.0);
    (void)printf("  %-24s %8.2f s\n", "Total",
        (double)context->duration_ns / 1000000000.0);
}

static void print_context(
    const SolarBuildContext *context,
    const BuildOptions *options
)
{
    size_t index;

    (void)printf(
        "%sSolar build: %s\n",
        context->dry_run ? "DRY RUN\n" : "",
        context->operation == NULL ? "unknown" : context->operation
    );
    for (index = 0U; index < context->step_count; index++) {
        const SolarBuildStepResult *step = &context->steps[index];
        const char *status = step->skipped ? "SKIP" :
            (step->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL");

        (void)printf("%-4s  %-8s", status, step->name);
        if (!context->dry_run) {
            (void)printf(" %llu ms", (unsigned long long)step->duration_ms);
        }
        (void)printf("\n");
    }
    if (context->generated &&
        strcmp(context->project.config.project.language, "asm") != 0 &&
        context->compiler_result.artifacts.assembly_path != NULL) {
        (void)printf(
            "Assembly: %s\n",
            context->compiler_result.artifacts.assembly_path
        );
    }
    if (context->has_test_result) {
        if (!options->verbose) print_simulation_output(&context->test_result);
        (void)printf("%-4s  %s\n",
            context->test_result.result.status == SOLAR_STATUS_OK
                ? "PASS" : "FAIL",
            context->test_result.name == NULL
                ? "?" : context->test_result.name);
        if (context->test_result.waveform_path != NULL) {
            (void)printf("Waveform: %s\n", context->test_result.waveform_path);
        }
        if (context->test_result.waveform_layout_path != NULL) {
            (void)printf(
                "GTKWave layout: %s\n",
                context->test_result.waveform_layout_path
            );
        }
        print_waveform_warning(
            &context->project.config, &context->test_result
        );
        print_waveform_layout_warning(&context->test_result);
    }
    if (context->has_test_summary) {
        for (index = 0U; index < context->test_summary.count; index++) {
            const SolarTestResult *simulation =
                &context->test_summary.results[index];

            if (!options->verbose) print_simulation_output(simulation);
            (void)printf("%-4s  %s",
                simulation->result.status == SOLAR_STATUS_OK ? "PASS" : "FAIL",
                simulation->name == NULL ? "?" : simulation->name);
            if (simulation->result.status != SOLAR_STATUS_OK) {
                (void)printf(" (%s)",
                    solar_test_failure_kind_name(simulation->failure_kind));
            }
            (void)printf("\n");
            print_waveform_warning(&context->project.config, simulation);
            print_waveform_layout_warning(simulation);
        }
        (void)printf("%zu passed, %zu failed\n",
            context->test_summary.passed, context->test_summary.failed);
    }
    if (context->has_synthesis_result &&
        context->synthesis_result.netlist_path != NULL) {
        (void)printf("Netlist: %s\nReport: %s\n",
            context->synthesis_result.netlist_path,
            context->synthesis_result.report_path);
    }
    if (options->target == BUILD_TARGET_SIM && !context->dry_run &&
        context->project.root != NULL) {
        print_simulation_summary(context);
    }
}

static SolarResult execute_target(
    SolarBuildContext *context,
    const BuildOptions *options,
    SolarCliProgress *progress
)
{
    SolarResult result = solar_build_context_check(context);

    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_build_context_rtl(context);
    if (result.status != SOLAR_STATUS_OK || options->target == BUILD_TARGET_RTL) {
        return result;
    }
    if (options->target == BUILD_TARGET_SIM) {
        if (options->all) {
            return solar_build_context_test_sequence(context, false);
        }
        return solar_build_context_test(
            context, options->simulation_name, false, true
        );
    }
    if (options->target == BUILD_TARGET_SYNTH) {
        return solar_build_context_synthesize(context);
    }
    if (!context->dry_run) {
        solar_cli_progress_init(
            progress, stdout, stderr, !options->no_progress, options->verbose,
            false, -1
        );
        solar_build_context_set_progress(
            context, solar_cli_progress_observer(progress)
        );
    }
    result = solar_build_context_test_sequence(context, true);
    if (!context->dry_run) {
        if (result.status == SOLAR_STATUS_OK) {
            solar_progress_stage(
                context->progress_observer, SOLAR_PROGRESS_FINALIZE,
                "Finalizing simulation",
                context->project.config.simulation.backend
            );
        }
        solar_progress_stage(
            context->progress_observer,
            result.status == SOLAR_STATUS_OK
                ? SOLAR_PROGRESS_COMPLETE : SOLAR_PROGRESS_FAILED,
            result.status == SOLAR_STATUS_OK
                ? "Simulation completed" : "Simulation failed",
            context->project.config.simulation.backend
        );
        solar_build_context_set_progress(context, NULL);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_build_context_synthesize(context);
    }
    return result;
}

SolarResult solar_cli_command_build(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    BuildOptions options;
    SolarBuildContext context;
    SolarCliProgress progress;
    char operation[32];
    SolarResult report_result;
    SolarResult result = parse_options(argument_count, arguments, &options);

    if (result.status != SOLAR_STATUS_OK) return result;
    if (options.list) return list_simulations(start_path);
    solar_build_context_init(&context);
    solar_cli_progress_init(
        &progress, stdout, stderr, !options.no_progress, options.verbose,
        false, -1
    );
    if (options.target == BUILD_TARGET_SIM) {
        solar_build_context_set_progress(
            &context, solar_cli_progress_observer(&progress)
        );
    }
    (void)snprintf(
        operation, sizeof(operation), "build %s", options.target_name
    );
    result = solar_build_context_load(
        &context, start_path, operation, options.profile_name, options.dry_run
    );
    if (result.status == SOLAR_STATUS_OK) {
        progress.yanc_project =
            context.project.config.compiler.backend != NULL;
        solar_cli_print_legacy_warning(&context.project);
        result = execute_target(&context, &options, &progress);
    }
    if (result.status == SOLAR_STATUS_OK &&
        options.target == BUILD_TARGET_SIM) {
        solar_progress_stage(
            context.progress_observer, SOLAR_PROGRESS_FINALIZE,
            "Finalizing simulation", context.project.config.simulation.backend
        );
    }
    solar_build_context_finish(&context, result);
    if (context.project.root != NULL &&
        (!context.lock_attempted || context.owns_project_lock)) {
        report_result = solar_build_report_write(&context);
        if (result.status == SOLAR_STATUS_OK &&
            report_result.status != SOLAR_STATUS_OK) {
            result = report_result;
        }
    }
    print_context(&context, &options);
    solar_build_context_free(&context);
    return result;
}
