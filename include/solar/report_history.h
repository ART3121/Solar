#ifndef SOLAR_REPORT_HISTORY_H
#define SOLAR_REPORT_HISTORY_H

#include "solar/project.h"
#include "solar/result.h"
#include "solar/synthesis_statistics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool present;
    uint64_t value;
} SolarStoredMetric;

typedef struct {
    SolarStoredMetric simulation_compile;
    SolarStoredMetric simulation_elaboration;
    SolarStoredMetric simulation_execution;
    SolarStoredMetric simulation_total;
    SolarStoredMetric simulated_duration;
    char *simulated_duration_unit;
    bool simulation_succeeded;
    bool simulation_status_present;
} SolarSimulationTimings;

typedef struct {
    char *build_id;
    char *timestamp;
    char *project_id;
    char *status;
    char *solar_version;
    char *top;
    char *profile;
    char *synthesis_backend;
    char *synthesis_tool_name;
    char *synthesis_tool_version;
    char *synthesis_source_fingerprint;
    char *synthesis_defines_fingerprint;
    char *synthesis_options_fingerprint;
    char *synthesis_script_fingerprint;
    char *simulation_backend;
    char *simulation_tool_name;
    char *simulation_tool_version;
    char *simulation_testbench;
    char *simulation_testbench_fingerprint;
    char *simulation_source_fingerprint;
    char *simulation_options_fingerprint;
    char *simulation_input_fingerprint;
    char *environment_os;
    char *environment_arch;
    char *environment_host_fingerprint;
    uint64_t environment_cpu_count;
    bool environment_cpu_count_present;
    bool waveform_enabled;
    bool waveform_enabled_present;
} SolarBuildMetadata;

typedef struct {
    SolarBuildMetadata metadata;
    SolarGenericSynthesisStatistics synthesis_statistics;
    SolarSimulationTimings timings;
    char *report_path;
    bool has_report;
    bool has_synthesis_statistics;
    bool has_timings;
} SolarStoredBuildReport;

typedef enum {
    SOLAR_TIMINGS_UNAVAILABLE = 0,
    SOLAR_TIMINGS_PARTIAL,
    SOLAR_TIMINGS_AVAILABLE
} SolarTimingsAvailability;

typedef struct {
    char *build_id;
    char *timestamp;
    char *status;
    char *top;
    bool gss_available;
    SolarTimingsAvailability timings_availability;
} SolarBuildReportInfo;

typedef struct {
    SolarBuildReportInfo *items;
    size_t count;
} SolarBuildReportList;

typedef enum {
    SOLAR_COMPARISON_UNCHANGED = 0,
    SOLAR_COMPARISON_INCREASED,
    SOLAR_COMPARISON_DECREASED,
    SOLAR_COMPARISON_ADDED,
    SOLAR_COMPARISON_REMOVED,
    SOLAR_COMPARISON_NOT_COMPARABLE
} SolarComparisonStatus;

typedef struct {
    bool baseline_available;
    bool current_available;
    uint64_t baseline;
    uint64_t current;
    bool absolute_change_available;
    bool absolute_change_negative;
    uint64_t absolute_change_magnitude;
    bool percentage_available;
    double percentage_change;
    SolarComparisonStatus status;
} SolarMetricComparison;

typedef struct {
    char *cell_type;
    SolarMetricComparison usage;
} SolarCellComparison;

typedef struct {
    SolarMetricComparison modules;
    SolarMetricComparison wires;
    SolarMetricComparison wire_bits;
    SolarMetricComparison public_wires;
    SolarMetricComparison public_wire_bits;
    SolarMetricComparison memories;
    SolarMetricComparison memory_bits;
    SolarMetricComparison processes;
    SolarMetricComparison cells;
    SolarCellComparison *cell_types;
    size_t cell_type_count;
    size_t added_cell_types;
    size_t removed_cell_types;
    size_t increased_metrics;
    size_t decreased_metrics;
    size_t unchanged_metrics;
    size_t not_comparable_metrics;
} SolarGenericSynthesisComparison;

typedef struct {
    SolarMetricComparison simulation_compile;
    SolarMetricComparison simulation_elaboration;
    SolarMetricComparison simulation_execution;
    SolarMetricComparison simulation_total;
    SolarMetricComparison simulated_duration;
    char *simulated_duration_unit;
    bool context_compatible;
    bool environment_changed;
    size_t increased_timings;
    size_t decreased_timings;
    size_t unchanged_timings;
    size_t not_comparable_timings;
} SolarSimulationTimingComparison;

typedef struct {
    SolarBuildMetadata baseline_metadata;
    SolarBuildMetadata current_metadata;
    bool synthesis_comparison_available;
    SolarGenericSynthesisComparison synthesis;
    bool simulation_comparison_available;
    SolarSimulationTimingComparison simulation;
    char **warnings;
    size_t warning_count;
} SolarBuildReportComparison;

/*
 * Every init/free pair manages all nested strings and arrays. Load, list, and
 * comparison functions replace an initialized output; callers retain
 * ownership until the matching free function is called.
 */
void solar_simulation_timings_init(SolarSimulationTimings *timings);
void solar_simulation_timings_free(SolarSimulationTimings *timings);
void solar_build_metadata_init(SolarBuildMetadata *metadata);
void solar_build_metadata_free(SolarBuildMetadata *metadata);
void solar_stored_build_report_init(SolarStoredBuildReport *report);
void solar_stored_build_report_free(SolarStoredBuildReport *report);
void solar_build_report_list_init(SolarBuildReportList *list);
void solar_build_report_list_free(SolarBuildReportList *list);
void solar_gss_comparison_init(SolarGenericSynthesisComparison *comparison);
void solar_gss_comparison_free(SolarGenericSynthesisComparison *comparison);
void solar_simulation_timing_comparison_init(
    SolarSimulationTimingComparison *comparison
);
void solar_simulation_timing_comparison_free(
    SolarSimulationTimingComparison *comparison
);
void solar_build_report_comparison_init(SolarBuildReportComparison *comparison);
void solar_build_report_comparison_free(SolarBuildReportComparison *comparison);

SolarResult solar_report_history_list(
    const SolarProject *project,
    SolarBuildReportList *result
);
SolarResult solar_report_history_load(
    const SolarProject *project,
    const char *build_id,
    SolarStoredBuildReport *result
);
SolarResult solar_report_history_load_latest(
    const SolarProject *project,
    SolarStoredBuildReport *result
);
SolarResult solar_report_history_read_text(
    const SolarProject *project,
    const char *build_id,
    char **report_text_out
);
SolarResult solar_report_history_find_previous_comparable(
    const SolarProject *project,
    const SolarStoredBuildReport *current,
    SolarStoredBuildReport *baseline
);

/* Pure comparison helpers: these functions do not read files or run tools. */
SolarMetricComparison solar_metric_compare(
    bool baseline_available,
    uint64_t baseline,
    bool current_available,
    uint64_t current
);
SolarResult solar_gss_compare(
    const SolarGenericSynthesisStatistics *baseline,
    const SolarGenericSynthesisStatistics *current,
    SolarGenericSynthesisComparison *result
);
SolarResult solar_simulation_timings_compare(
    const SolarSimulationTimings *baseline,
    const SolarSimulationTimings *current,
    SolarSimulationTimingComparison *result
);
SolarResult solar_build_reports_compare(
    const SolarStoredBuildReport *baseline,
    const SolarStoredBuildReport *current,
    SolarBuildReportComparison *result
);

#endif
