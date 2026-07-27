#define _POSIX_C_SOURCE 200809L

#include "commands.h"

#include "solar/artifact.h"
#include "solar/compiler.h"
#include "solar/project.h"
#include "solar/waveform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SolarResult solar_cli_open_waveform_path(
    const char *waveform_path,
    const char *layout_path,
    const char *working_directory,
    SolarWaveformViewer viewer
)
{
    SolarWaveformOpenOptions options;
    bool launched = false;

    solar_waveform_open_options_init(&options);
    options.viewer = viewer;
    options.layout_path = layout_path;
    options.working_directory = working_directory;
    options.interactive = true;
    SolarResult result = solar_waveform_open_with_options(
        waveform_path, &options, &launched
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    if (!launched) {
        return solar_result_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "waveform viewer was not launched",
            "start a graphical session, install the selected viewer, and ensure SOLAR_NO_VIEWER is not set"
        );
    }
    (void)printf(
        "Waveform viewer: %s\nWaveform: %s\n",
        solar_waveform_viewer_name(viewer),
        waveform_path
    );
    if (layout_path != NULL) {
        (void)printf("GTKWave layout: %s\n", layout_path);
    }
    return solar_result_ok();
}

static SolarResult parse_viewer(
    const char *name,
    SolarWaveformViewer *viewer_out
)
{
    if (strcmp(name, "gtkwave") == 0) {
        *viewer_out = SOLAR_WAVEFORM_VIEWER_GTKWAVE;
        return solar_result_ok();
    }
    if (strcmp(name, "surfer") == 0) {
        *viewer_out = SOLAR_WAVEFORM_VIEWER_SURFER;
        return solar_result_ok();
    }
    return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "unknown waveform viewer",
        "use --viewer surfer or --viewer gtkwave"
    );
}

static SolarResult parse_arguments(
    int argument_count,
    char *const arguments[],
    const char **test_name_out,
    const char **waveform_out,
    SolarWaveformViewer *viewer_out
)
{
    int index;

    *test_name_out = NULL;
    *waveform_out = NULL;
    *viewer_out = SOLAR_WAVEFORM_VIEWER_GTKWAVE;
    for (index = 0; index < argument_count; index++) {
        if (strcmp(arguments[index], "--viewer") == 0) {
            if (++index >= argument_count || arguments[index][0] == '\0') {
                return solar_result_error(
                    SOLAR_STATUS_INVALID_ARGUMENT,
                    "missing waveform viewer name",
                    "use --viewer surfer or --viewer gtkwave"
                );
            }
            {
                SolarResult result = parse_viewer(
                    arguments[index], viewer_out
                );

                if (result.status != SOLAR_STATUS_OK) return result;
            }
        } else if (strcmp(arguments[index], "--waveform") == 0) {
            if (++index >= argument_count || arguments[index][0] == '\0' ||
                *waveform_out != NULL || *test_name_out != NULL) {
                return solar_result_error(
                    SOLAR_STATUS_INVALID_ARGUMENT,
                    "invalid waveform selection",
                    "use either a test name or --waveform <file>"
                );
            }
            *waveform_out = arguments[index];
        } else if (arguments[index][0] != '-' && *test_name_out == NULL &&
                   *waveform_out == NULL) {
            *test_name_out = arguments[index];
        } else {
            return solar_result_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "invalid solar view arguments",
                "use solar view [<test>] [--viewer surfer|gtkwave]"
            );
        }
    }
    return solar_result_ok();
}

static SolarResult selected_test_waveform(
    const SolarProject *project,
    const char *test_name,
    char **path_out,
    char **selected_test_out
)
{
    const SolarTest *test = NULL;
    SolarArtifactList artifacts;
    const SolarArtifactRecord *record;
    SolarResult result = solar_config_select_test(
        &project->config, test_name, &test
    );

    *selected_test_out = NULL;
    if (result.status != SOLAR_STATUS_OK) return result;
    solar_artifact_list_init(&artifacts);
    result = solar_artifact_list_load(project->root, &artifacts);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    record = solar_artifact_find(&artifacts, "waveform", test->name);
    if (record == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_NOT_FOUND,
            "no generated waveform is registered for the selected test",
            "run solar build sim for this test before opening its waveform"
        );
        goto cleanup;
    }
    result = solar_project_resolve_path(project, record->path, path_out);
    if (result.status == SOLAR_STATUS_OK) {
        *selected_test_out = strdup(test->name);
        if (*selected_test_out == NULL) {
            free(*path_out);
            *path_out = NULL;
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not retain the selected waveform test",
                "free memory and try again"
            );
        }
    }

cleanup:
    solar_artifact_list_free(&artifacts);
    return result;
}

static SolarResult explicit_waveform(
    const SolarProject *project,
    const char *argument,
    char **path_out,
    char **selected_test_out
)
{
    SolarArtifactList artifacts;
    const char *relative = argument;
    size_t root_length = strlen(project->root);
    size_t index;
    SolarResult result;

    *selected_test_out = NULL;
    if (argument[0] == '/') {
        if (strncmp(argument, project->root, root_length) != 0 ||
            argument[root_length] != '/') {
            return solar_result_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "waveform is outside the active Solar project",
                "select a waveform registered by this project"
            );
        }
        relative = argument + root_length + 1U;
    }
    solar_artifact_list_init(&artifacts);
    result = solar_artifact_list_load(project->root, &artifacts);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < artifacts.count; index++) {
        if (strcmp(artifacts.items[index].type, "waveform") == 0 &&
            strcmp(artifacts.items[index].path, relative) == 0) {
            result = solar_project_resolve_path(project, relative, path_out);
            if (result.status == SOLAR_STATUS_OK) {
                *selected_test_out = strdup(artifacts.items[index].test_name);
                if (*selected_test_out == NULL) {
                    free(*path_out);
                    *path_out = NULL;
                    result = solar_result_error(
                        SOLAR_STATUS_INTERNAL_ERROR,
                        "could not retain the selected waveform test",
                        "free memory and try again"
                    );
                }
            }
            goto cleanup;
        }
    }
    result = solar_result_error(
        SOLAR_STATUS_NOT_FOUND,
        "waveform is not registered as a generated Solar artifact",
        "run solar build sim first or select a path shown by solar report"
    );

cleanup:
    solar_artifact_list_free(&artifacts);
    return result;
}

static bool is_generated_test(const SolarProject *project, const char *test_name)
{
    const SolarTest *test;

    if (test_name == NULL) return false;
    test = solar_config_find_test(&project->config, test_name);

    return test != NULL && test->kind == SOLAR_TEST_GENERATED;
}

SolarResult solar_cli_command_view(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    const char *test_name = NULL;
    const char *waveform_argument = NULL;
    SolarWaveformViewer viewer = SOLAR_WAVEFORM_VIEWER_GTKWAVE;
    SolarProject project;
    char *waveform_path = NULL;
    char *layout_path = NULL;
    char *selected_test = NULL;
    SolarResult result;

    result = parse_arguments(
        argument_count,
        arguments,
        &test_name,
        &waveform_argument,
        &viewer
    );
    if (result.status != SOLAR_STATUS_OK) return result;

    solar_project_init(&project);
    result = solar_project_load(start_path, &project);
    if (result.status == SOLAR_STATUS_OK) {
        solar_cli_print_legacy_warning(&project);
        result = solar_project_validate(&project);
    }
    if (result.status == SOLAR_STATUS_OK && waveform_argument != NULL) {
        result = explicit_waveform(
            &project, waveform_argument, &waveform_path, &selected_test
        );
    } else if (result.status == SOLAR_STATUS_OK) {
        result = selected_test_waveform(
            &project, test_name, &waveform_path, &selected_test
        );
    }
    if (result.status == SOLAR_STATUS_OK &&
        viewer == SOLAR_WAVEFORM_VIEWER_GTKWAVE &&
        is_generated_test(&project, selected_test)) {
        result = solar_compiler_find_waveform_layout(
            &project, selected_test, waveform_path, &layout_path
        );
        if (result.status == SOLAR_STATUS_OK && layout_path == NULL) {
            (void)fprintf(
                stderr,
                "solar: warning: decoded SAPHO Assembly layout is unavailable; "
                "opening the raw waveform\n"
                "hint: run solar build sim with Solar's bundled YANC or an "
                "external root containing compatible gen_gtkw\n"
            );
        }
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_cli_open_waveform_path(
            waveform_path, layout_path,
            layout_path == NULL ? NULL : project.root,
            viewer
        );
    }
    free(selected_test);
    free(layout_path);
    free(waveform_path);
    solar_project_free(&project);
    return result;
}
