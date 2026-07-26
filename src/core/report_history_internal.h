#ifndef SOLAR_REPORT_HISTORY_INTERNAL_H
#define SOLAR_REPORT_HISTORY_INTERNAL_H

#include "solar/build.h"

SolarResult solar_report_history_store(
    const SolarBuildContext *context,
    const char *latest_report_path
);

#endif
