#include "solar/synthesis.h"

#include "solar/artifact.h"
#include "solar/backend.h"
#include "solar/compiler.h"
#include "solar/filesystem.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void solar_synthesis_request_init(SolarSynthesisRequest *request)
{
    if (request != NULL) (void)memset(request, 0, sizeof(*request));
}

void solar_synthesis_request_free(SolarSynthesisRequest *request)
{
    if (request == NULL) return;
    free(request->backend);
    free(request->project_root);
    free(request->working_directory);
    free(request->top);
    solar_string_list_free(&request->rtl_sources);
    solar_string_list_free(&request->include_dirs);
    solar_string_list_free(&request->defines);
    free(request->script_path);
    free(request->netlist_path);
    free(request->report_path);
    free(request->stdout_log);
    free(request->stderr_log);
    solar_synthesis_request_init(request);
}

void solar_synthesis_result_init(SolarSynthesisResult *result)
{
    if (result == NULL) return;
    (void)memset(result, 0, sizeof(*result));
    result->result = solar_result_ok();
    result->statistics_result = solar_result_ok();
    solar_synthesis_statistics_init(&result->statistics);
}

void solar_synthesis_result_free(SolarSynthesisResult *result)
{
    if (result == NULL) return;
    free(result->profile_name);
    free(result->script_path);
    free(result->netlist_path);
    free(result->report_path);
    solar_synthesis_statistics_free(&result->statistics);
    solar_synthesis_result_init(result);
}

static SolarResult copy_string(char **destination, const char *source)
{
    if (source == NULL) return solar_result_ok();
    *destination = strdup(source);
    if (*destination == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not copy synthesis configuration",
            "free memory and try again"
        );
    }
    return solar_result_ok();
}

static SolarResult copy_list(
    const SolarStringList *source,
    SolarStringList *destination
)
{
    size_t index;

    for (index = 0U; index < source->count; index++) {
        SolarResult result = solar_string_list_append_copy(
            destination, source->items[index]
        );

        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static SolarResult resolve_list(
    const SolarProject *project,
    const SolarStringList *relative,
    SolarStringList *absolute
)
{
    size_t index;

    for (index = 0U; index < relative->count; index++) {
        char *resolved = NULL;
        SolarResult result = solar_project_resolve_path(
            project, relative->items[index], &resolved
        );

        if (result.status == SOLAR_STATUS_OK) {
            result = solar_string_list_append_copy(absolute, resolved);
        }
        free(resolved);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static bool project_uses_compiler(const SolarProject *project)
{
    return project->config.compiler.backend != NULL;
}

static SolarResult add_synthesis_sources(
    const SolarProject *project,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarSynthesisRequest *request
)
{
    SolarResult result;

    if (!project_uses_compiler(project)) {
        return resolve_list(
            project, &project->config.sources.rtl, &request->rtl_sources
        );
    }
    result = copy_list(
        &generated_artifacts->rtl_sources, &request->rtl_sources
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return copy_list(
        &generated_artifacts->hdl_sources, &request->rtl_sources
    );
}

static SolarResult resolve_synthesis_paths(
    const SolarProject *project,
    SolarSynthesisRequest *request
)
{
    SolarResult result = solar_project_resolve_path(
        project, ".solar/tmp/synth", &request->working_directory
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/tmp/synth/synthesis.ys", &request->script_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/tmp/synth/netlist.v", &request->netlist_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/tmp/synth/statistics.txt", &request->report_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/logs/yosys/yosys.stdout.log", &request->stdout_log
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_project_resolve_path(
        project, ".solar/logs/yosys/yosys.stderr.log", &request->stderr_log
    );
}

static SolarResult stage_synthesis_include_aliases(
    const SolarProject *project,
    SolarSynthesisRequest *request
)
{
    static const char *const DIRECTORY =
        ".solar/tmp/synth/includes";
    bool removed = false;
    size_t index;
    SolarResult result = solar_filesystem_remove_generated_tree(
        project->root, DIRECTORY, &removed
    );

    if (result.status != SOLAR_STATUS_OK ||
        request->include_dirs.count == 0U) {
        return result;
    }
    result = solar_filesystem_prepare_generated_directory(
        project->root, DIRECTORY
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    for (index = 0U; index < request->include_dirs.count; index++) {
        char name[32];
        char relative[96];
        char alias[64];
        char *replacement;

        (void)snprintf(name, sizeof(name), "%zu", index);
        (void)snprintf(
            relative, sizeof(relative), "%s/%s", DIRECTORY, name
        );
        (void)snprintf(alias, sizeof(alias), "includes/%s", name);
        result = solar_filesystem_create_generated_symlink(
            project->root, relative, request->include_dirs.items[index]
        );
        if (result.status != SOLAR_STATUS_OK) return result;
        replacement = strdup(alias);
        if (replacement == NULL) {
            return solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate synthesis include alias",
                "free memory and try again"
            );
        }
        free(request->include_dirs.items[index]);
        request->include_dirs.items[index] = replacement;
    }
    return solar_result_ok();
}

static SolarResult build_synthesis_request(
    const SolarProject *project,
    const SolarProfile *profile,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarSynthesisRequest *request
)
{
    SolarEffectiveConfig effective;
    SolarResult result;

    solar_effective_config_init(&effective);
    solar_synthesis_request_init(request);
    result = copy_string(&request->backend, project->config.synthesis.backend);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->project_root, project->root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_string(&request->top, project->config.synthesis.top);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = add_synthesis_sources(project, generated_artifacts, request);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_config_build_effective(
        &project->config, profile, NULL, &effective
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_list(
        project, &effective.include_dirs, &request->include_dirs
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = copy_list(&effective.defines, &request->defines);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_layout(project->root);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, ".solar/tmp/synth"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_prepare_generated_directory(
        project->root, ".solar/logs/yosys"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = resolve_synthesis_paths(project, request);
    if (result.status == SOLAR_STATUS_OK) {
        result = stage_synthesis_include_aliases(project, request);
    }

cleanup:
    solar_effective_config_free(&effective);
    if (result.status != SOLAR_STATUS_OK) solar_synthesis_request_free(request);
    return result;
}

static SolarResult reset_synthesis_artifacts(const SolarProject *project)
{
    static const char *const PATHS[] = {
        ".solar/tmp/synth/synthesis.ys",
        ".solar/tmp/synth/netlist.v",
        ".solar/tmp/synth/statistics.txt"
    };
    size_t index;

    for (index = 0U; index < sizeof(PATHS) / sizeof(PATHS[0]); index++) {
        SolarResult result = solar_filesystem_reset_artifact(
            project->root, PATHS[index]
        );

        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static SolarResult set_synthesis_result(
    const SolarProfile *profile,
    const SolarSynthesisRequest *request,
    SolarSynthesisResult *synthesis_result
)
{
    SolarResult result = copy_string(
        &synthesis_result->profile_name, profile == NULL ? NULL : profile->name
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_string(
        &synthesis_result->script_path, request->script_path
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return result;
}

static SolarResult publish_synthesis_outputs(
    const SolarProject *project,
    const SolarProfile *profile,
    const SolarSynthesisRequest *request,
    SolarSynthesisResult *synthesis_result
)
{
    const char *root = project_uses_compiler(project) ? "hardware" : "synth";
    const char *base = project_uses_compiler(project)
        ? project->config.compiler.processor : request->top;
    char *netlist_name = NULL;
    char *netlist_relative = NULL;
    char *report_relative = NULL;
    char *published_netlist = NULL;
    char *published_report = NULL;
    size_t length = strlen(base) + sizeof("_netlist.v");
    SolarArtifactPublication publications[2];
    SolarResult result;

    netlist_name = malloc(length);
    if (netlist_name == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate synthesis publication path",
        "free memory and try again"
    );
    (void)snprintf(netlist_name, length, "%s_netlist.v", base);
    result = solar_filesystem_join(root, netlist_name, &netlist_relative);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_join(
            root, "statistics.txt", &report_relative
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_resolve_path(
            project, netlist_relative, &published_netlist
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_resolve_path(
            project, report_relative, &published_report
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        publications[0] = (SolarArtifactPublication){
            request->netlist_path, netlist_relative, "netlist", "synthesis",
            NULL, request->backend, profile == NULL ? NULL : profile->name
        };
        publications[1] = (SolarArtifactPublication){
            request->report_path, report_relative, "report", "synthesis",
            NULL, request->backend, profile == NULL ? NULL : profile->name
        };
        result = solar_artifact_publish_files(
            project->root, publications, 2U
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        synthesis_result->netlist_path = published_netlist;
        synthesis_result->report_path = published_report;
        published_netlist = NULL;
        published_report = NULL;
    }
    free(published_report);
    free(published_netlist);
    free(report_relative);
    free(netlist_relative);
    free(netlist_name);
    return result;
}

static char *read_yosys_version(const char *log_path)
{
    int descriptor;
    FILE *file;
    struct stat information;
    char *line = NULL;
    size_t capacity = 0U;
    char *version = NULL;

    descriptor = open(log_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return NULL;
    if (fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        information.st_size > (off_t)16 * 1024 * 1024) {
        (void)close(descriptor);
        return NULL;
    }
    file = fdopen(descriptor, "r");
    if (file == NULL) {
        (void)close(descriptor);
        return NULL;
    }
    while (getline(&line, &capacity, file) >= 0) {
        char *start = line;
        char *end;

        while (*start == ' ' || *start == '\t') start++;
        if (strncmp(start, "Yosys ", 6U) != 0 ||
            !isdigit((unsigned char)start[6]) || strlen(line) > 4096U) {
            continue;
        }
        end = start + strcspn(start, "\r\n");
        *end = '\0';
        version = strdup(start);
        break;
    }
    free(line);
    (void)fclose(file);
    return version;
}

static SolarResult set_statistics_metadata(
    const SolarSynthesisRequest *request,
    SolarSynthesisResult *synthesis_result
)
{
    SolarGenericSynthesisStatistics *statistics =
        &synthesis_result->statistics;
    SolarResult result;

    result = copy_string(&statistics->tool, "Yosys");
    if (result.status == SOLAR_STATUS_OK) {
        statistics->tool_version = read_yosys_version(request->stdout_log);
        result = copy_string(&statistics->top, request->top);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_string(
            &statistics->report_path, synthesis_result->report_path
        );
    }
    return result;
}

static void collect_synthesis_statistics(
    const SolarSynthesisRequest *request,
    SolarSynthesisResult *synthesis_result
)
{
    SolarResult metadata_result;

    synthesis_result->statistics_result = solar_synthesis_statistics_analyze(
        synthesis_result->report_path, &synthesis_result->statistics
    );
    metadata_result = set_statistics_metadata(request, synthesis_result);
    if (metadata_result.status != SOLAR_STATUS_OK) {
        synthesis_result->statistics_result = metadata_result;
    }
}

SolarResult solar_synthesis_run_with_artifacts(
    const SolarProject *project,
    const char *profile_name,
    const SolarGeneratedArtifacts *generated_artifacts,
    SolarSynthesisResult *synthesis_result
)
{
    const SolarProfile *profile = NULL;
    SolarCompilerResult compiler_result;
    const SolarGeneratedArtifacts *active_artifacts = generated_artifacts;
    SolarSynthesisRequest request;
    SolarResult result;

    if (synthesis_result != NULL) {
        solar_synthesis_result_init(synthesis_result);
    }
    solar_compiler_result_init(&compiler_result);
    if (project == NULL || synthesis_result == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot synthesize without a project and result storage",
            "load a project before running synthesis"
        );
    }
    result = solar_project_validate(project);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_config_select_profile(
        &project->config, profile_name, &profile
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (project_uses_compiler(project) && active_artifacts == NULL) {
        result = solar_compiler_compile(project, &compiler_result);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        active_artifacts = &compiler_result.artifacts;
    }
    result = build_synthesis_request(
        project, profile, active_artifacts, &request
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = set_synthesis_result(profile, &request, synthesis_result);
    if (result.status == SOLAR_STATUS_OK) {
        result = reset_synthesis_artifacts(project);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_backend_synthesize_request(
            &request, &synthesis_result->duration_ns
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = publish_synthesis_outputs(
            project, profile, &request, synthesis_result
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        collect_synthesis_statistics(&request, synthesis_result);
    }
    solar_synthesis_request_free(&request);

cleanup:
    solar_compiler_result_free(&compiler_result);
    if (result.status != SOLAR_STATUS_OK &&
        synthesis_result->statistics_result.status == SOLAR_STATUS_OK) {
        synthesis_result->statistics_result = result;
    }
    synthesis_result->result = result;
    return result;
}

SolarResult solar_synthesis_run(
    const SolarProject *project,
    const char *profile_name,
    SolarSynthesisResult *synthesis_result
)
{
    return solar_synthesis_run_with_artifacts(
        project, profile_name, NULL, synthesis_result
    );
}
