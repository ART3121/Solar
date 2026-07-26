#include "solar/solar.h"

#include "commands.h"

#include <stdio.h>
#include <string.h>

static void print_help(FILE *stream)
{
    (void)fprintf(
        stream,
        "Solar %s - lightweight EDA workflow orchestrator\n"
        "\n"
        "Usage:\n"
        "  solar <command> [arguments]\n"
        "  solar <command> --help\n"
        "\n"
        "Project:\n"
        "  init       Create a project from a built-in template\n"
        "  scan       Detect hierarchy and synchronize solar.toml\n"
        "  config     Update selected solar.toml project settings\n"
        "  check      Validate the current project\n"
        "  doctor     Check tools required by the current project\n"
        "  clean      Remove generated project data safely\n"
        "\n"
        "Build:\n"
        "  build rtl    Generate or elaborate RTL\n"
        "  build sim    Build and run simulations\n"
        "  build synth  Build and synthesize hardware\n"
        "  build full   Run the complete project pipeline\n"
        "\n"
        "Artifacts:\n"
        "  view       Open an existing waveform\n"
        "  report     Show the complete last-build report\n"
        "\n"
        "Use 'solar <command> --help' for command usage.\n",
        SOLAR_VERSION
    );
}

static bool print_command_help(const char *command, FILE *stream)
{
    if (strcmp(command, "init") == 0) {
        (void)fprintf(stream,
            "Usage: solar init [--template verilog|sapho]\n");
    } else if (strcmp(command, "scan") == 0) {
        (void)fprintf(stream,
            "Usage: solar scan\nDetect .v/.sv hierarchy and synchronize sources and tops.\n");
    } else if (strcmp(command, "check") == 0) {
        (void)fprintf(stream,
            "Usage: solar check\nValidate the manifest and discovered project sources.\n");
    } else if (strcmp(command, "config") == 0) {
        (void)fprintf(stream,
            "Usage:\n"
            "  solar config set --name <name>\n"
            "  solar config set --top <module>\n"
            "  solar config set --test <test>\n"
            "Options may be combined in one atomic manifest update.\n");
    } else if (strcmp(command, "doctor") == 0) {
        (void)fprintf(stream,
            "Usage: solar doctor [--all]\nCheck project tools, or every backend with --all.\n");
    } else if (strcmp(command, "clean") == 0) {
        (void)fprintf(stream,
            "Usage: solar clean [--cache|--all]\nRemove registered artifacts and internal state safely.\n");
    } else if (strcmp(command, "build") == 0) {
        (void)fprintf(stream,
            "Usage:\n"
            "  solar build rtl [--profile <profile>] [--dry-run]\n"
            "  solar build sim [<name>|--all|--list] [--profile <profile>] [--dry-run]\n"
            "                  [--no-progress] [--verbose]\n"
            "  solar build synth [--profile <profile>] [--dry-run]\n"
            "  solar build full [--profile <profile>] [--dry-run]\n"
            "                   [--no-progress] [--verbose]\n");
    } else if (strcmp(command, "view") == 0) {
        (void)fprintf(stream,
            "Usage:\n"
            "  solar view [<simulation>] [--viewer surfer|gtkwave]\n"
            "  solar view --waveform <file> [--viewer surfer|gtkwave]\n"
            "Open a registered waveform without building. GTKWave is the default.\n");
    } else if (strcmp(command, "report") == 0) {
        (void)fprintf(stream,
            "Usage:\n"
            "  solar report\n"
            "  solar report show <build-id>\n"
            "  solar report list [--limit <count>]\n"
            "  solar report compare [<build-id>] [--against <build-id>] [--summary]\n"
            "\nCommands:\n"
            "  show       Display a stored build report.\n"
            "  list       List stored build reports.\n"
            "  compare    Compare synthesis statistics and simulation timings.\n"
            "\nCompare options:\n"
            "  --against <build-id>    Select the baseline build.\n"
            "  --summary               Display a compact comparison.\n"
            "\nList options:\n"
            "  --limit <count>         Limit the number of displayed builds.\n");
    } else {
        return false;
    }
    return true;
}

void solar_cli_print_legacy_warning(const SolarProject *project)
{
    if (project == NULL) return;
    if (project->config.manifest_format == 1U) {
        (void)fprintf(
            stderr,
            "solar: warning: solar.toml uses project format 1\n"
            "hint: run solar scan to migrate and synchronize format 2\n"
        );
    }
    if (project->legacy_build_layout_present) {
        (void)fprintf(
            stderr,
            "solar: warning: legacy solar-build/ was preserved\n"
            "hint: inspect and remove it manually when no longer needed\n"
        );
    }
}

static int exit_code_for_status(SolarStatus status)
{
    switch (status) {
    case SOLAR_STATUS_OK: return 0;
    case SOLAR_STATUS_INVALID_ARGUMENT: return 2;
    case SOLAR_STATUS_CONFIG_ERROR:
    case SOLAR_STATUS_NOT_FOUND: return 3;
    case SOLAR_STATUS_TOOL_MISSING: return 4;
    case SOLAR_STATUS_PROCESS_FAILED: return 5;
    case SOLAR_STATUS_IO_ERROR:
    case SOLAR_STATUS_INTERNAL_ERROR: return 1;
    }
    return 1;
}

static void print_failure(const SolarResult *result)
{
    const char *message = result->diagnostic.message[0] == '\0'
        ? solar_status_name(result->status)
        : result->diagnostic.message;

    (void)fprintf(stderr, "solar: error: %s\n", message);
    if (result->diagnostic.hint[0] != '\0') {
        (void)fprintf(stderr, "hint: %s\n", result->diagnostic.hint);
    }
}

static SolarResult no_arguments(const char *command, int count)
{
    if (count == 0) return solar_result_ok();
    return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "command does not accept arguments",
        command
    );
}

static SolarResult dispatch_command(
    const char *command,
    int argument_count,
    char **arguments
)
{
    SolarResult result;

    if (strcmp(command, "init") == 0) {
        return solar_cli_command_init(".", argument_count, arguments);
    }
    if (strcmp(command, "scan") == 0) {
        result = no_arguments("use solar scan", argument_count);
        return result.status == SOLAR_STATUS_OK
            ? solar_cli_command_scan(".") : result;
    }
    if (strcmp(command, "check") == 0) {
        result = no_arguments("use solar check", argument_count);
        return result.status == SOLAR_STATUS_OK
            ? solar_cli_command_check(".") : result;
    }
    if (strcmp(command, "config") == 0) {
        return solar_cli_command_config(".", argument_count, arguments);
    }
    if (strcmp(command, "doctor") == 0) {
        return solar_cli_command_doctor(".", argument_count, arguments);
    }
    if (strcmp(command, "clean") == 0) {
        return solar_cli_command_clean(".", argument_count, arguments);
    }
    if (strcmp(command, "build") == 0) {
        return solar_cli_command_build(".", argument_count, arguments);
    }
    if (strcmp(command, "view") == 0) {
        return solar_cli_command_view(".", argument_count, arguments);
    }
    if (strcmp(command, "report") == 0) {
        return solar_cli_command_report(".", argument_count, arguments);
    }
    if (strcmp(command, "__complete") == 0) {
        return solar_cli_command_complete(".", argument_count, arguments);
    }
    return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "unknown command",
        "run solar --help to list available commands"
    );
}

int main(int argc, char **argv)
{
    SolarResult result;

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        (void)printf("solar %s\n", SOLAR_VERSION);
        return 0;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_help(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[2], "--help") == 0) {
        if (!print_command_help(argv[1], stdout)) {
            (void)fprintf(stderr, "solar: error: unknown command: %s\n", argv[1]);
            return 2;
        }
        return 0;
    }
    result = dispatch_command(argv[1], argc - 2, argv + 2);
    if (result.status != SOLAR_STATUS_OK) print_failure(&result);
    return exit_code_for_status(result.status);
}
