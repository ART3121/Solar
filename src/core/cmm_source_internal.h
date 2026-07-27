#ifndef SOLAR_CMM_SOURCE_INTERNAL_H
#define SOLAR_CMM_SOURCE_INTERNAL_H

#include "solar/result.h"

#include <stdbool.h>

/* Returned strings are owned by the caller and must be freed. */
SolarResult solar_cmm_discover_source(
    const char *project_root,
    const char *configured_source,
    char **source_out
);

SolarResult solar_cmm_read_processor_name(
    const char *source_path,
    char **processor_out
);

bool solar_processor_name_is_safe(const char *name);

#endif
