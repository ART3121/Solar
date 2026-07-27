#ifndef SOLAR_YANC_H
#define SOLAR_YANC_H

#include "solar/project.h"
#include "solar/result.h"

#include <stdbool.h>

typedef enum {
    SOLAR_YANC_LAYOUT_UNKNOWN = 0,
    SOLAR_YANC_LAYOUT_CHECKOUT,
    SOLAR_YANC_LAYOUT_RELEASE
} SolarYancLayoutKind;

typedef struct {
    SolarYancLayoutKind layout;
    char *root;
    char *bin_directory;
    char *hdl_directory;
    char *macros_directory;
    char *headers_directory;
    char *cmmcomp;
    char *cpppp;
    char *cppcomp;
    char *appcomp;
    char *asmcomp;
    /* Optional presentation helper; compilation remains valid without it. */
    char *gen_gtkw;
    char *version;
    bool bundled;
} SolarYancToolchain;

void solar_yanc_toolchain_init(SolarYancToolchain *toolchain);
void solar_yanc_toolchain_free(SolarYancToolchain *toolchain);

/* A null project still discovers the YANC bundle installed with Solar. */
SolarResult solar_yanc_resolve(
    const SolarProject *project,
    SolarYancToolchain *toolchain
);

#endif
