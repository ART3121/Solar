#ifndef SOLAR_CLI_COMMANDS_H
#define SOLAR_CLI_COMMANDS_H

#include "solar/project.h"
#include "solar/result.h"
#include "solar/waveform.h"

#include <stdbool.h>

SolarResult solar_cli_command_init(
    const char *directory,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_check(const char *start_path);
SolarResult solar_cli_command_config(
    const char *start_path,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_clean(
    const char *start_path,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_doctor(
    const char *start_path,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_view(
    const char *start_path,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_build(
    const char *start_path,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_report(
    const char *start_path,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_complete(
    const char *start_path,
    int argument_count,
    char *const arguments[]
);
SolarResult solar_cli_command_scan(const char *start_path);

SolarResult solar_cli_open_waveform_path(
    const char *waveform_path,
    const char *layout_path,
    const char *working_directory,
    SolarWaveformViewer viewer
);

void solar_cli_print_legacy_warning(const SolarProject *project);

#endif
