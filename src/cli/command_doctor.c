#include "commands.h"

#include "solar/backend.h"
#include "solar/project.h"
#include "solar/yanc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_eda_report(
    const SolarToolReport *report,
    bool *missing_required
)
{
    if (report->available) {
        (void)printf(
            "%-10s available  %s%s%s\n",
            report->name,
            report->path,
            report->version == NULL ? "" : "  ",
            report->version == NULL ? "" : report->version
        );
        return;
    }
    (void)printf(
        "%-10s missing%s\n",
        report->name,
        report->optional ? " (optional)" : ""
    );
    if (!report->optional) *missing_required = true;
}

static const char *availability(const char *path)
{
    return path == NULL ? "missing" : "available";
}

static void print_yanc_toolchain(const SolarYancToolchain *toolchain)
{
    (void)printf(
        "\nYANC toolchain\n"
        "  root:      %s\n"
        "  source:    %s\n"
        "  version:   %s\n"
        "  cmmcomp:   %s\n"
        "  cpppp:     %s\n"
        "  cppcomp:   %s\n"
        "  appcomp:   %s\n"
        "  asmcomp:   %s\n"
        "  gen_gtkw:  %s (optional waveform formatter)\n"
        "  HDL:       %s\n"
        "  macros:    %s\n"
        "  headers:   %s\n",
        toolchain->root,
        toolchain->bundled ? "bundled with Solar" : "development override",
        toolchain->version == NULL ? "unknown" : toolchain->version,
        availability(toolchain->cmmcomp),
        availability(toolchain->cpppp),
        availability(toolchain->cppcomp),
        availability(toolchain->appcomp),
        availability(toolchain->asmcomp),
        availability(toolchain->gen_gtkw),
        availability(toolchain->hdl_directory),
        availability(toolchain->macros_directory),
        availability(toolchain->headers_directory)
    );
}

SolarResult solar_cli_command_doctor(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    SolarToolReport *reports = NULL;
    size_t report_count = 0U;
    size_t index;
    bool missing_required = false;
    SolarProject project;
    bool project_loaded = false;
    SolarYancToolchain toolchain;
    SolarResult yanc_result = solar_result_ok();
    SolarResult result;
    bool all_tools = false;
    bool inspect_yanc;

    if (argument_count == 1 && strcmp(arguments[0], "--all") == 0) {
        all_tools = true;
    } else if (argument_count != 0) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "invalid solar doctor arguments",
            "use solar doctor or solar doctor --all"
        );
    }

    solar_project_init(&project);
    solar_yanc_toolchain_init(&toolchain);
    result = solar_project_load(start_path, &project);
    if (result.status == SOLAR_STATUS_OK) {
        project_loaded = true;
    } else if (result.status == SOLAR_STATUS_NOT_FOUND) {
        result = solar_result_ok();
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_backend_doctor_project(
        project_loaded ? &project : NULL,
        all_tools || !project_loaded,
        &reports,
        &report_count
    );

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < report_count; index++) {
        print_eda_report(&reports[index], &missing_required);
    }
    solar_backend_tool_reports_free(reports, report_count);

    reports = NULL;
    report_count = 0U;
    inspect_yanc = all_tools || !project_loaded ||
        project.config.compiler.backend != NULL;
    if (inspect_yanc) {
        yanc_result = solar_yanc_resolve(
            project_loaded ? &project : NULL, &toolchain
        );
        if (yanc_result.status == SOLAR_STATUS_OK) {
            print_yanc_toolchain(&toolchain);
        } else if (yanc_result.status == SOLAR_STATUS_TOOL_MISSING) {
            (void)printf(
                "\nYANC toolchain\n"
                "  status: not found\n"
                "\n"
                "hint: rebuild Solar with the vendored YANC toolchain\n"
            );
        } else {
            (void)printf("\nYANC toolchain\n  status: invalid\n");
        }
    }

    if (missing_required) {
        result = solar_result_error(
            SOLAR_STATUS_TOOL_MISSING,
            "one or more required EDA tools are unavailable",
            "install missing tools with your system package manager; Solar does not install them"
        );
        goto cleanup;
    }
    if (inspect_yanc && project_loaded &&
        project.config.compiler.backend != NULL &&
        yanc_result.status != SOLAR_STATUS_OK) {
        result = yanc_result;
        goto cleanup;
    }
    if (inspect_yanc && yanc_result.status != SOLAR_STATUS_OK &&
        yanc_result.status != SOLAR_STATUS_TOOL_MISSING) {
        result = yanc_result;
        goto cleanup;
    }
    result = solar_result_ok();

cleanup:
    solar_backend_tool_reports_free(reports, report_count);
    solar_yanc_toolchain_free(&toolchain);
    solar_project_free(&project);
    return result;
}
