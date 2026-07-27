#ifndef SOLAR_SCAN_H
#define SOLAR_SCAN_H

#include "solar/result.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SOLAR_SCAN_VERILOG = 0,
    SOLAR_SCAN_CMM
} SolarScanKind;

typedef struct {
    SolarScanKind kind;
    size_t rtl_added;
    size_t rtl_removed;
    size_t rtl_total;
    size_t tests_added;
    size_t tests_removed;
    size_t tests_total;
    bool migrated_v1;
    bool changed;
    char *compiler_source;
    char *processor;
} SolarScanResult;

void solar_scan_result_init(SolarScanResult *result);
void solar_scan_result_free(SolarScanResult *result);

/*
 * Discovers conventional Verilog sources or one CMM processor source and
 * transactionally synchronizes the corresponding managed solar.toml fields.
 * The manifest is unchanged on every error path. On success, result owns any
 * returned strings until solar_scan_result_free().
 */
SolarResult solar_scan_project(const char *start_path, SolarScanResult *result);

#endif
