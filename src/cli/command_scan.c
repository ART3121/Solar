#include "commands.h"

#include "solar/scan.h"

#include <stdio.h>

SolarResult solar_cli_command_scan(const char *start_path)
{
    SolarScanResult scan;
    SolarResult result;

    solar_scan_result_init(&scan);
    result = solar_scan_project(start_path, &scan);
    if (result.status != SOLAR_STATUS_OK) {
        solar_scan_result_free(&scan);
        return result;
    }

    if (scan.kind == SOLAR_SCAN_CMM) {
        (void)printf(
            "Solar scan\n\n"
            "Source:        %s\n"
            "Processor:     %s\n"
            "Project name:  %s\n"
            "Synthesis top: %s\n"
            "Manifest:      %s\n",
            scan.compiler_source,
            scan.processor,
            scan.processor,
            scan.processor,
            scan.changed ? "updated" : "unchanged"
        );
    } else {
        (void)printf(
            "Solar scan\n\n"
            "RTL:   %zu total, %zu added, %zu removed\n"
            "Tests: %zu total, %zu added, %zu removed\n"
            "Manifest: %s%s\n",
            scan.rtl_total, scan.rtl_added, scan.rtl_removed,
            scan.tests_total, scan.tests_added, scan.tests_removed,
            scan.changed ? "updated" : "unchanged",
            scan.migrated_v1 ? " (format 1 -> 2)" : ""
        );
    }
    solar_scan_result_free(&scan);
    return solar_result_ok();
}
