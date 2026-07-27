#include "solar/waveform.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    SolarWaveformViewer viewer;
    const char *executable;
    const char *display_name;
    const char *missing_hint;
} SolarViewerDefinition;

static const SolarViewerDefinition VIEWERS[] = {
    {
        SOLAR_WAVEFORM_VIEWER_GTKWAVE,
        "gtkwave",
        "GTKWave",
        "install GTKWave or select Surfer with --viewer surfer"
    },
    {
        SOLAR_WAVEFORM_VIEWER_SURFER,
        "surfer",
        "Surfer",
        "install Surfer and ensure surfer is on PATH, or select GTKWave with --viewer gtkwave"
    }
};

static const SolarViewerDefinition *viewer_definition(
    SolarWaveformViewer viewer
)
{
    size_t index;

    for (index = 0U; index < sizeof(VIEWERS) / sizeof(VIEWERS[0]); index++) {
        if (VIEWERS[index].viewer == viewer) return &VIEWERS[index];
    }
    return NULL;
}

const char *solar_waveform_viewer_name(SolarWaveformViewer viewer)
{
    const SolarViewerDefinition *definition = viewer_definition(viewer);

    return definition == NULL ? "unknown" : definition->display_name;
}

void solar_waveform_open_options_init(SolarWaveformOpenOptions *options)
{
    if (options == NULL) return;
    (void)memset(options, 0, sizeof(*options));
    options->viewer = SOLAR_WAVEFORM_VIEWER_GTKWAVE;
}

static bool disabled_by_environment(void)
{
    const char *disabled = getenv("SOLAR_NO_VIEWER");

    return disabled != NULL && disabled[0] != '\0' &&
        strcmp(disabled, "0") != 0;
}

static bool graphical_session_available(void)
{
    const char *display = getenv("DISPLAY");
    const char *wayland = getenv("WAYLAND_DISPLAY");

    return (display != NULL && display[0] != '\0') ||
        (wayland != NULL && wayland[0] != '\0');
}

static bool supported_waveform(const char *path)
{
    size_t length = strlen(path);

    return (length >= 4U && strcmp(path + length - 4U, ".vcd") == 0) ||
        (length >= 4U && strcmp(path + length - 4U, ".fst") == 0);
}

static void report_child_error(int descriptor, int error_number)
{
    const unsigned char *cursor = (const unsigned char *)&error_number;
    size_t remaining = sizeof(error_number);

    while (remaining > 0U) {
        ssize_t count = write(descriptor, cursor, remaining);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        cursor += count;
        remaining -= (size_t)count;
    }
}

static _Noreturn void detached_viewer(
    int error_descriptor,
    const SolarViewerDefinition *definition,
    const char *waveform_path,
    const char *working_directory,
    const char *layout_path
)
{
    const char *plain_arguments[] = {
        definition->executable, waveform_path, NULL
    };
    const char *layout_arguments[] = {
        definition->executable,
        "--dark", "-a", layout_path, waveform_path, NULL
    };
    const char *const *arguments = layout_path != NULL &&
        definition->viewer == SOLAR_WAVEFORM_VIEWER_GTKWAVE
        ? layout_arguments : plain_arguments;
    int null_descriptor = open("/dev/null", O_RDWR | O_CLOEXEC);

    if ((working_directory != NULL && chdir(working_directory) != 0) ||
        null_descriptor < 0 ||
        dup2(null_descriptor, STDIN_FILENO) < 0 ||
        dup2(null_descriptor, STDOUT_FILENO) < 0 ||
        dup2(null_descriptor, STDERR_FILENO) < 0) {
        report_child_error(error_descriptor, errno);
        _exit(127);
    }
    if (null_descriptor > STDERR_FILENO) (void)close(null_descriptor);
    execvp(arguments[0], (char *const *)arguments);
    report_child_error(error_descriptor, errno);
    _exit(127);
}

static SolarResult launch_detached(
    const SolarViewerDefinition *definition,
    const char *waveform_path,
    const char *working_directory,
    const char *layout_path
)
{
    int descriptors[2] = {-1, -1};
    pid_t first_child;
    int child_status;
    int child_error = 0;
    ssize_t error_bytes;

    if (pipe(descriptors) != 0 ||
        fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) != 0) {
        if (descriptors[0] >= 0) (void)close(descriptors[0]);
        if (descriptors[1] >= 0) (void)close(descriptors[1]);
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "cannot prepare the waveform viewer process",
            "reduce open files and try again"
        );
    }
    first_child = fork();
    if (first_child < 0) {
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "cannot fork the waveform viewer process",
            "reduce running processes and try again"
        );
    }
    if (first_child == 0) {
        pid_t viewer;

        (void)close(descriptors[0]);
        if (setsid() < 0) {
            report_child_error(descriptors[1], errno);
            _exit(127);
        }
        viewer = fork();
        if (viewer < 0) {
            report_child_error(descriptors[1], errno);
            _exit(127);
        }
        if (viewer > 0) _exit(0);
        detached_viewer(
            descriptors[1], definition, waveform_path,
            working_directory, layout_path
        );
    }
    (void)close(descriptors[1]);
    while (waitpid(first_child, &child_status, 0) < 0) {
        if (errno != EINTR) {
            (void)close(descriptors[0]);
            return solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "cannot reap the waveform launcher process",
                "try opening the waveform manually with the selected viewer"
            );
        }
    }
    do {
        error_bytes = read(descriptors[0], &child_error, sizeof(child_error));
    } while (error_bytes < 0 && errno == EINTR);
    (void)close(descriptors[0]);
    if (error_bytes > 0) {
        SolarStatus status = child_error == ENOENT
            ? SOLAR_STATUS_TOOL_MISSING
            : SOLAR_STATUS_PROCESS_FAILED;
        SolarResult result = solar_result_error(
            status,
            "could not launch the waveform viewer",
            definition->missing_hint
        );

        (void)snprintf(
            result.diagnostic.message,
            sizeof(result.diagnostic.message),
            "could not launch %s: %s",
            definition->display_name,
            strerror(child_error)
        );
        return result;
    }
    return solar_result_ok();
}

SolarResult solar_waveform_open_with_options(
    const char *waveform_path,
    const SolarWaveformOpenOptions *options,
    bool *launched_out
)
{
    const SolarViewerDefinition *definition;
    struct stat information;
    SolarResult result;

    if (launched_out != NULL) *launched_out = false;
    if (waveform_path == NULL || options == NULL || launched_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot open a missing waveform path",
            "provide a generated VCD or FST artifact"
        );
    }
    definition = viewer_definition(options->viewer);
    if (definition == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "unknown waveform viewer",
            "select surfer or gtkwave"
        );
    }
    if (!options->interactive || disabled_by_environment() ||
        !graphical_session_available()) {
        return solar_result_ok();
    }
    if (!supported_waveform(waveform_path) ||
        stat(waveform_path, &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "waveform viewer received an invalid artifact",
            "generate a readable .vcd or .fst waveform first"
        );
    }
    if (options->layout_path != NULL &&
        (stat(options->layout_path, &information) != 0 ||
         !S_ISREG(information.st_mode))) {
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "waveform viewer received an invalid layout",
            "regenerate the SAPHO simulation layout or open the raw waveform"
        );
    }
    if (options->working_directory != NULL &&
        (stat(options->working_directory, &information) != 0 ||
         !S_ISDIR(information.st_mode))) {
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "waveform viewer received an invalid working directory",
            "load the waveform from an active Solar project"
        );
    }
    result = launch_detached(
        definition, waveform_path, options->working_directory,
        options->layout_path
    );
    if (result.status == SOLAR_STATUS_OK) *launched_out = true;
    return result;
}

SolarResult solar_waveform_open_with_viewer(
    const char *waveform_path,
    SolarWaveformViewer viewer,
    bool interactive,
    bool *launched_out
)
{
    SolarWaveformOpenOptions options;

    solar_waveform_open_options_init(&options);
    options.viewer = viewer;
    options.interactive = interactive;
    return solar_waveform_open_with_options(
        waveform_path, &options, launched_out
    );
}

SolarResult solar_waveform_open(
    const char *waveform_path,
    bool interactive,
    bool *launched_out
)
{
    return solar_waveform_open_with_viewer(
        waveform_path,
        SOLAR_WAVEFORM_VIEWER_GTKWAVE,
        interactive,
        launched_out
    );
}
