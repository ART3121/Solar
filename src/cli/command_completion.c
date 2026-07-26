#include "commands.h"

#include "solar/artifact.h"
#include "solar/report_history.h"

#include <stdio.h>
#include <string.h>

static SolarResult completion_ok(void)
{
    return solar_result_ok();
}

static SolarResult complete_project_names(
    const char *start_path,
    bool profiles
)
{
    SolarProject project;
    SolarResult result;
    size_t index;

    solar_project_init(&project);
    result = solar_project_load(start_path, &project);
    if (result.status != SOLAR_STATUS_OK) {
        solar_project_free(&project);
        return completion_ok();
    }
    if (profiles) {
        for (index = 0U; index < project.config.profile_count; index++) {
            const char *name = project.config.profiles[index].name;

            if (name != NULL && name[0] != '\0') (void)printf("%s\n", name);
        }
    } else {
        for (index = 0U; index < project.config.test_count; index++) {
            const char *name = project.config.tests[index].name;

            if (name != NULL && name[0] != '\0') (void)printf("%s\n", name);
        }
    }
    solar_project_free(&project);
    return completion_ok();
}

static bool find_completion_root(
    const char *start_path,
    SolarProject *project
)
{
    SolarResult result;

    solar_project_init(project);
    result = solar_project_find_root(start_path, &project->root);
    if (result.status != SOLAR_STATUS_OK) {
        solar_project_free(project);
        return false;
    }
    return true;
}

static SolarResult complete_build_ids(const char *start_path)
{
    SolarProject project;
    SolarBuildReportList reports;
    SolarResult result;
    size_t index;

    solar_build_report_list_init(&reports);
    if (!find_completion_root(start_path, &project)) goto cleanup;
    result = solar_report_history_list(&project, &reports);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < reports.count; index++) {
        if (reports.items[index].build_id != NULL) {
            (void)printf("%s\n", reports.items[index].build_id);
        }
    }

cleanup:
    solar_build_report_list_free(&reports);
    solar_project_free(&project);
    return completion_ok();
}

static SolarResult complete_waveforms(const char *start_path)
{
    SolarProject project;
    SolarArtifactList artifacts;
    SolarResult result;
    size_t index;

    solar_artifact_list_init(&artifacts);
    if (!find_completion_root(start_path, &project)) goto cleanup;
    result = solar_artifact_list_load(project.root, &artifacts);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < artifacts.count; index++) {
        const SolarArtifactRecord *record = &artifacts.items[index];

        if (record->type != NULL && record->path != NULL &&
            strcmp(record->type, "waveform") == 0) {
            (void)printf("%s\n", record->path);
        }
    }

cleanup:
    solar_artifact_list_free(&artifacts);
    solar_project_free(&project);
    return completion_ok();
}

SolarResult solar_cli_command_complete(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    if (argument_count != 1 || arguments == NULL || arguments[0] == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "invalid internal completion request",
            "shell completion must request exactly one candidate kind"
        );
    }
    if (strcmp(arguments[0], "tests") == 0) {
        return complete_project_names(start_path, false);
    }
    if (strcmp(arguments[0], "profiles") == 0) {
        return complete_project_names(start_path, true);
    }
    if (strcmp(arguments[0], "build-ids") == 0) {
        return complete_build_ids(start_path);
    }
    if (strcmp(arguments[0], "waveforms") == 0) {
        return complete_waveforms(start_path);
    }
    return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "unknown internal completion candidate kind",
        "use a completion definition installed with this Solar version"
    );
}
