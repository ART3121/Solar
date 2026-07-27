#ifndef SOLAR_WAVEFORM_H
#define SOLAR_WAVEFORM_H

#include "solar/result.h"

#include <stdbool.h>

typedef enum {
    SOLAR_WAVEFORM_VIEWER_GTKWAVE = 0,
    SOLAR_WAVEFORM_VIEWER_SURFER
} SolarWaveformViewer;

typedef struct {
    SolarWaveformViewer viewer;
    /* Borrowed optional paths used only while launching the viewer. */
    const char *working_directory;
    const char *layout_path;
    bool interactive;
} SolarWaveformOpenOptions;

const char *solar_waveform_viewer_name(SolarWaveformViewer viewer);
void solar_waveform_open_options_init(SolarWaveformOpenOptions *options);

SolarResult solar_waveform_open_with_options(
    const char *waveform_path,
    const SolarWaveformOpenOptions *options,
    bool *launched_out
);

/*
 * Launches the selected external viewer without waiting for its GUI process.
 * interactive should describe the calling frontend; false makes this a no-op
 * for automation. Solar does not own or install the viewer executable.
 */
SolarResult solar_waveform_open_with_viewer(
    const char *waveform_path,
    SolarWaveformViewer viewer,
    bool interactive,
    bool *launched_out
);

/*
 * Compatibility entry point. It retains the pre-Surfer GTKWave behavior.
 */
SolarResult solar_waveform_open(
    const char *waveform_path,
    bool interactive,
    bool *launched_out
);

#endif
