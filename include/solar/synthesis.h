#ifndef SOLAR_SYNTHESIS_H
#define SOLAR_SYNTHESIS_H

#include "solar/compiler.h"
#include "solar/project.h"
#include "solar/result.h"
#include "solar/synthesis_statistics.h"

#include <stdint.h>

typedef struct {
    char *profile_name;
    char *script_path;
    char *netlist_path;
    char *report_path;
    SolarGenericSynthesisStatistics statistics;
    SolarResult statistics_result;
    uint64_t duration_ns;
    SolarResult result;
} SolarSynthesisResult;

void solar_synthesis_result_init(SolarSynthesisResult *result);
void solar_synthesis_result_free(SolarSynthesisResult *result);

SolarResult solar_synthesis_run(
    const SolarProject *project,
    const char *profile_name,
    SolarSynthesisResult *synthesis_result
);

SolarResult solar_synthesis_run_with_artifacts(
    const SolarProject *project,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarSynthesisResult *synthesis_result
);

#endif
