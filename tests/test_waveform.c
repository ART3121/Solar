#define _POSIX_C_SOURCE 200809L

#include "solar/waveform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static int write_file(const char *path)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs("fake fst\n", file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static bool read_expected_line(FILE *file, const char *expected)
{
    char *line = NULL;
    size_t capacity = 0U;
    ssize_t count = getline(&line, &capacity, file);
    bool matches = false;

    if (count >= 0) {
        while (count > 0 && (line[count - 1] == '\n' || line[count - 1] == '\r')) {
            line[--count] = '\0';
        }
        matches = strcmp(line, expected) == 0;
    }
    free(line);
    return matches;
}

static int record_matches(
    const char *record,
    const char *viewer,
    const char *waveform
)
{
    FILE *file = fopen(record, "r");
    bool matches;

    if (file == NULL) return 0;
    matches = read_expected_line(file, viewer) &&
        read_expected_line(file, waveform) && fgetc(file) == EOF;
    (void)fclose(file);
    return matches ? 1 : 0;
}

static int layout_record_matches(
    const char *record,
    const char *viewer,
    const char *waveform,
    const char *layout,
    const char *working_directory
)
{
    FILE *file = fopen(record, "r");
    bool matches;

    if (file == NULL) return 0;
    matches = read_expected_line(file, viewer) &&
        read_expected_line(file, "--dark") &&
        read_expected_line(file, "-a") && read_expected_line(file, layout) &&
        read_expected_line(file, waveform) &&
        read_expected_line(file, working_directory) && fgetc(file) == EOF;
    (void)fclose(file);
    return matches ? 1 : 0;
}

static int wait_for_record(
    const char *path,
    const char *viewer,
    const char *waveform
)
{
    int attempts;

    for (attempts = 0; attempts < 100; attempts++) {
        const struct timespec delay = {0, 10000000L};

        if (record_matches(path, viewer, waveform)) return 0;
        (void)nanosleep(&delay, NULL);
    }
    return record_matches(path, viewer, waveform) ? 0 : -1;
}

int main(int argc, char **argv)
{
    char root_template[] = "/tmp/solar viewer-XXXXXX";
    char *root = mkdtemp(root_template);
    char *gtkwave = NULL;
    char *surfer = NULL;
    char *waveform = NULL;
    char *layout = NULL;
    char *record = NULL;
    char *old_path = NULL;
    bool launched = false;
    SolarResult result;
    int failed = 1;

    if (argc != 2 || root == NULL) goto cleanup;
    gtkwave = join_path(root, "gtkwave");
    surfer = join_path(root, "surfer");
    waveform = join_path(root, "wave output.fst");
    layout = join_path(root, "SAPHO layout.gtkw");
    record = join_path(root, "viewer.record");
    old_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (gtkwave == NULL || surfer == NULL || waveform == NULL || layout == NULL ||
        record == NULL ||
        symlink(argv[1], gtkwave) != 0 || symlink(argv[1], surfer) != 0 ||
        write_file(waveform) != 0 || write_file(layout) != 0 ||
        setenv("PATH", root, 1) != 0 || setenv("DISPLAY", ":test", 1) != 0 ||
        setenv("SOLAR_VIEWER_RECORD", record, 1) != 0) goto cleanup;
    result = solar_waveform_open(waveform, false, &launched);
    if (result.status != SOLAR_STATUS_OK || launched || access(record, F_OK) == 0) {
        goto cleanup;
    }
    result = solar_waveform_open(waveform, true, &launched);
    if (result.status != SOLAR_STATUS_OK || !launched) goto cleanup;
    if (wait_for_record(record, "gtkwave", waveform) != 0) goto cleanup;
    if (unlink(record) != 0) goto cleanup;
    {
        SolarWaveformOpenOptions options;
        int attempts;

        solar_waveform_open_options_init(&options);
        options.viewer = SOLAR_WAVEFORM_VIEWER_GTKWAVE;
        options.working_directory = root;
        options.layout_path = layout;
        options.interactive = true;
        if (setenv("SOLAR_VIEWER_RECORD_CWD", "1", 1) != 0) goto cleanup;
        launched = false;
        result = solar_waveform_open_with_options(
            waveform, &options, &launched
        );
        if (result.status != SOLAR_STATUS_OK || !launched) {
            (void)fprintf(
                stderr, "layout launch failed: %s\n",
                result.diagnostic.message
            );
            goto cleanup;
        }
        for (attempts = 0; attempts < 100; attempts++) {
            const struct timespec delay = {0, 10000000L};

            if (layout_record_matches(
                    record, "gtkwave", waveform, layout, root
                )) break;
            (void)nanosleep(&delay, NULL);
        }
        if (!layout_record_matches(
                record, "gtkwave", waveform, layout, root
            )) {
            FILE *debug = fopen(record, "r");
            int character;

            (void)fprintf(stderr, "layout viewer argv did not match:\n");
            if (debug != NULL) {
                while ((character = fgetc(debug)) != EOF) {
                    (void)fputc(character, stderr);
                }
                (void)fclose(debug);
            }
            goto cleanup;
        }
        (void)unsetenv("SOLAR_VIEWER_RECORD_CWD");
        if (unlink(record) != 0) goto cleanup;
    }
    launched = false;
    result = solar_waveform_open_with_viewer(
        waveform, SOLAR_WAVEFORM_VIEWER_SURFER, true, &launched
    );
    if (result.status != SOLAR_STATUS_OK || !launched ||
        wait_for_record(record, "surfer", waveform) != 0) goto cleanup;
    launched = true;
    result = solar_waveform_open_with_viewer(
        waveform, (SolarWaveformViewer)99, true, &launched
    );
    if (result.status != SOLAR_STATUS_INVALID_ARGUMENT || launched) goto cleanup;
    if (unlink(surfer) != 0 || unlink(record) != 0) goto cleanup;
    launched = true;
    result = solar_waveform_open_with_viewer(
        waveform, SOLAR_WAVEFORM_VIEWER_SURFER, true, &launched
    );
    if (result.status != SOLAR_STATUS_TOOL_MISSING || launched ||
        strstr(result.diagnostic.message, "Surfer") == NULL) goto cleanup;
    failed = 0;

cleanup:
    if (old_path != NULL) (void)setenv("PATH", old_path, 1);
    else (void)unsetenv("PATH");
    (void)unsetenv("SOLAR_VIEWER_RECORD");
    (void)unsetenv("SOLAR_VIEWER_RECORD_CWD");
    if (gtkwave != NULL) (void)unlink(gtkwave);
    if (surfer != NULL) (void)unlink(surfer);
    if (waveform != NULL) (void)unlink(waveform);
    if (layout != NULL) (void)unlink(layout);
    if (record != NULL) (void)unlink(record);
    if (root != NULL) (void)rmdir(root);
    free(old_path);
    free(record);
    free(waveform);
    free(layout);
    free(surfer);
    free(gtkwave);
    return failed;
}
