#ifndef SOLAR_SYNTHESIS_STATISTICS_H
#define SOLAR_SYNTHESIS_STATISTICS_H

#include "solar/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *type;
    uint64_t count;
} SolarSynthesisCellUsage;

typedef enum {
    SOLAR_SYNTHESIS_STATISTICS_NONE = 0,
    SOLAR_SYNTHESIS_STATISTICS_PARTIAL,
    SOLAR_SYNTHESIS_STATISTICS_COMPLETE
} SolarSynthesisStatisticsCompleteness;

typedef enum {
    SOLAR_SYNTHESIS_FIELD_MODULES = 1U << 0,
    SOLAR_SYNTHESIS_FIELD_WIRES = 1U << 1,
    SOLAR_SYNTHESIS_FIELD_WIRE_BITS = 1U << 2,
    SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES = 1U << 3,
    SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS = 1U << 4,
    SOLAR_SYNTHESIS_FIELD_MEMORIES = 1U << 5,
    SOLAR_SYNTHESIS_FIELD_MEMORY_BITS = 1U << 6,
    SOLAR_SYNTHESIS_FIELD_PROCESSES = 1U << 7,
    SOLAR_SYNTHESIS_FIELD_CELLS = 1U << 8
} SolarSynthesisStatisticsField;

typedef struct {
    bool available;
    SolarSynthesisStatisticsCompleteness completeness;
    uint32_t reported_fields;

    uint64_t modules;
    uint64_t wires;
    uint64_t wire_bits;
    uint64_t public_wires;
    uint64_t public_wire_bits;
    uint64_t memories;
    uint64_t memory_bits;
    uint64_t processes;
    uint64_t cells;

    SolarSynthesisCellUsage *cell_types;
    size_t cell_type_count;

    char *tool;
    char *tool_version;
    char *top;
    char *report_path;
} SolarGenericSynthesisStatistics;

/* The structure owns all strings and cell entries until free is called. */
void solar_synthesis_statistics_init(
    SolarGenericSynthesisStatistics *statistics
);
void solar_synthesis_statistics_free(
    SolarGenericSynthesisStatistics *statistics
);
SolarResult solar_synthesis_statistics_copy(
    SolarGenericSynthesisStatistics *destination,
    const SolarGenericSynthesisStatistics *source
);

bool solar_synthesis_statistics_has_field(
    const SolarGenericSynthesisStatistics *statistics,
    SolarSynthesisStatisticsField field
);
size_t solar_synthesis_statistics_cell_count(
    const SolarGenericSynthesisStatistics *statistics
);
const SolarSynthesisCellUsage *solar_synthesis_statistics_cell_at(
    const SolarGenericSynthesisStatistics *statistics,
    size_t index
);
const char *solar_synthesis_statistics_report_path(
    const SolarGenericSynthesisStatistics *statistics
);

/* Outputs must be initialized. Analyze replaces any prior owned contents. */
SolarResult solar_synthesis_statistics_analyze(
    const char *report_path,
    SolarGenericSynthesisStatistics *statistics
);

/* Outputs must be initialized. Load replaces any prior owned contents. */
SolarResult solar_synthesis_statistics_load_last_report(
    const char *start_path,
    SolarGenericSynthesisStatistics *statistics
);

#endif
