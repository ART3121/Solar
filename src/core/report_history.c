#include "solar/report_history.h"

#include "report_history_internal.h"

#include "solar/backend.h"
#include "solar/filesystem.h"
#include "solar/solar.h"
#include "solar/system.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define HISTORY_SCHEMA 1U
#define HISTORY_FILE_LIMIT ((off_t)16 * 1024 * 1024)
#define HISTORY_LINE_LIMIT 16384U
#define HISTORY_CELL_LIMIT 4096U
#define HISTORY_VALUE_LIMIT 4096U

typedef struct {
    char *key;
    char *value;
} KeyValue;

typedef struct {
    KeyValue *items;
    size_t count;
} KeyValueList;

static bool regular_file_exists(const char *path);

static SolarResult history_error(SolarStatus status, const char *message)
{
    return solar_result_error(status, message,
        "inspect .solar/reports and project filesystem permissions");
}

static SolarResult copy_optional(char **destination, const char *source)
{
    if (source == NULL) return solar_result_ok();
    *destination = strdup(source);
    if (*destination == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR, "could not allocate build history data",
        "free memory and try again");
    return solar_result_ok();
}

void solar_simulation_timings_init(SolarSimulationTimings *timings)
{
    if (timings != NULL) (void)memset(timings, 0, sizeof(*timings));
}

void solar_simulation_timings_free(SolarSimulationTimings *timings)
{
    if (timings == NULL) return;
    free(timings->simulated_duration_unit);
    solar_simulation_timings_init(timings);
}

void solar_build_metadata_init(SolarBuildMetadata *metadata)
{
    if (metadata != NULL) (void)memset(metadata, 0, sizeof(*metadata));
}

void solar_build_metadata_free(SolarBuildMetadata *metadata)
{
    if (metadata == NULL) return;
    char **items[] = {
        &metadata->build_id, &metadata->timestamp, &metadata->project_id,
        &metadata->status, &metadata->solar_version, &metadata->top,
        &metadata->profile, &metadata->synthesis_backend,
        &metadata->synthesis_tool_name, &metadata->synthesis_tool_version,
        &metadata->synthesis_source_fingerprint,
        &metadata->synthesis_defines_fingerprint,
        &metadata->synthesis_options_fingerprint,
        &metadata->synthesis_script_fingerprint, &metadata->simulation_backend,
        &metadata->simulation_tool_name, &metadata->simulation_tool_version,
        &metadata->simulation_testbench,
        &metadata->simulation_testbench_fingerprint,
        &metadata->simulation_source_fingerprint,
        &metadata->simulation_options_fingerprint,
        &metadata->simulation_input_fingerprint, &metadata->environment_os,
        &metadata->environment_arch, &metadata->environment_host_fingerprint
    };
    size_t index;

    for (index = 0U; index < sizeof(items) / sizeof(items[0]); index++) {
        free(*items[index]);
    }
    solar_build_metadata_init(metadata);
}

void solar_stored_build_report_init(SolarStoredBuildReport *report)
{
    if (report == NULL) return;
    (void)memset(report, 0, sizeof(*report));
    solar_build_metadata_init(&report->metadata);
    solar_synthesis_statistics_init(&report->synthesis_statistics);
    solar_simulation_timings_init(&report->timings);
}

void solar_stored_build_report_free(SolarStoredBuildReport *report)
{
    if (report == NULL) return;
    solar_build_metadata_free(&report->metadata);
    solar_synthesis_statistics_free(&report->synthesis_statistics);
    solar_simulation_timings_free(&report->timings);
    free(report->report_path);
    solar_stored_build_report_init(report);
}

void solar_build_report_list_init(SolarBuildReportList *list)
{
    if (list != NULL) (void)memset(list, 0, sizeof(*list));
}

void solar_build_report_list_free(SolarBuildReportList *list)
{
    size_t index;

    if (list == NULL) return;
    for (index = 0U; index < list->count; index++) {
        free(list->items[index].build_id);
        free(list->items[index].timestamp);
        free(list->items[index].status);
        free(list->items[index].top);
    }
    free(list->items);
    solar_build_report_list_init(list);
}

void solar_build_report_comparison_init(SolarBuildReportComparison *comparison)
{
    if (comparison == NULL) return;
    (void)memset(comparison, 0, sizeof(*comparison));
    solar_build_metadata_init(&comparison->baseline_metadata);
    solar_build_metadata_init(&comparison->current_metadata);
}

void solar_build_report_comparison_free(SolarBuildReportComparison *comparison)
{
    size_t index;

    if (comparison == NULL) return;
    solar_build_metadata_free(&comparison->baseline_metadata);
    solar_build_metadata_free(&comparison->current_metadata);
    solar_gss_comparison_free(&comparison->synthesis);
    solar_simulation_timing_comparison_free(&comparison->simulation);
    for (index = 0U; index < comparison->warning_count; index++) {
        free(comparison->warnings[index]);
    }
    free(comparison->warnings);
    solar_build_report_comparison_init(comparison);
}

static bool valid_build_id(const char *build_id)
{
    const char *cursor;
    size_t digits = 0U;

    if (build_id == NULL || strncmp(build_id, "build-", 6U) != 0) return false;
    for (cursor = build_id + 6U; *cursor != '\0'; cursor++) {
        if (!isdigit((unsigned char)*cursor)) return false;
        digits++;
    }
    return digits >= 6U && digits <= 18U;
}

static SolarResult join3(
    const char *root,
    const char *middle,
    const char *leaf,
    char **path_out
)
{
    char *directory = NULL;
    SolarResult result = solar_filesystem_join(root, middle, &directory);

    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_join(directory, leaf, path_out);
    }
    free(directory);
    return result;
}

static SolarResult read_regular_text(const char *path, char **text_out)
{
    struct stat information;
    int descriptor = -1;
    FILE *file = NULL;
    char *text = NULL;
    size_t size;
    SolarResult result = solar_result_ok();

    if (path == NULL || text_out == NULL) return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "cannot read stored report data without a path and output storage",
        "provide valid report storage");
    *text_out = NULL;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return history_error(
        errno == ENOENT ? SOLAR_STATUS_NOT_FOUND : SOLAR_STATUS_IO_ERROR,
        errno == ENOENT ? "stored build report file was not found" :
            "cannot open a stored build report file");
    if (fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
        information.st_size < 0 || information.st_size > HISTORY_FILE_LIMIT) {
        result = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored build report data is not a regular file below 16 MiB");
        goto cleanup;
    }
    size = (size_t)information.st_size;
    text = malloc(size + 1U);
    if (text == NULL) {
        result = solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate stored report text", "free memory and try again");
        goto cleanup;
    }
    file = fdopen(descriptor, "r");
    if (file == NULL) {
        result = history_error(SOLAR_STATUS_IO_ERROR,
            "cannot stream a stored build report file");
        goto cleanup;
    }
    descriptor = -1;
    if (fread(text, 1U, size, file) != size || ferror(file) != 0) {
        result = history_error(SOLAR_STATUS_IO_ERROR,
            "cannot read a stored build report file");
        goto cleanup;
    }
    text[size] = '\0';
    *text_out = text;
    text = NULL;

cleanup:
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    free(text);
    return result;
}

static bool encoding_safe(unsigned char value)
{
    return isalnum(value) || value == '-' || value == '_' || value == '.' ||
        value == '/' || value == ':' || value == '$';
}

static SolarResult write_encoded(FILE *file, const char *value)
{
    const unsigned char *bytes;
    size_t length;
    size_t index;

    if (value == NULL) value = "";
    bytes = (const unsigned char *)value;
    length = strlen(value);
    for (index = 0U; index < length; index++) {
        if (encoding_safe(bytes[index])) {
            if (fputc((int)bytes[index], file) == EOF) break;
        } else if (fprintf(file, "%%%02X", (unsigned int)bytes[index]) < 0) {
            break;
        }
    }
    return ferror(file) == 0 ? solar_result_ok() :
        history_error(SOLAR_STATUS_IO_ERROR, "cannot write build history data");
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static SolarResult decode_value(const char *encoded, char **value_out)
{
    size_t length = strlen(encoded);
    char *value;
    size_t source = 0U;
    size_t destination = 0U;

    *value_out = NULL;
    if (length > HISTORY_VALUE_LIMIT * 3U) return history_error(
        SOLAR_STATUS_CONFIG_ERROR, "stored report value is too long");
    value = malloc(length + 1U);
    if (value == NULL) return solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
        "could not decode stored report data", "free memory and try again");
    while (source < length) {
        if (encoded[source] == '%') {
            int high;
            int low;

            if (source + 2U >= length ||
                (high = hex_digit(encoded[source + 1U])) < 0 ||
                (low = hex_digit(encoded[source + 2U])) < 0) {
                free(value);
                return history_error(SOLAR_STATUS_CONFIG_ERROR,
                    "stored report contains invalid percent encoding");
            }
            value[destination++] = (char)((high << 4) | low);
            source += 3U;
        } else {
            value[destination++] = encoded[source++];
        }
    }
    value[destination] = '\0';
    if (destination > HISTORY_VALUE_LIMIT || strlen(value) != destination) {
        free(value);
        return history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored report contains an invalid or oversized value");
    }
    *value_out = value;
    return solar_result_ok();
}

static void key_values_free(KeyValueList *list)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        free(list->items[index].key);
        free(list->items[index].value);
    }
    free(list->items);
    (void)memset(list, 0, sizeof(*list));
}

static const char *key_value(const KeyValueList *list, const char *key)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (strcmp(list->items[index].key, key) == 0) return list->items[index].value;
    }
    return NULL;
}

static SolarResult read_key_values(const char *path, KeyValueList *list)
{
    char *text = NULL;
    char *line;
    char *save = NULL;
    SolarResult result = read_regular_text(path, &text);

    (void)memset(list, 0, sizeof(*list));
    if (result.status != SOLAR_STATUS_OK) return result;
    line = strtok_r(text, "\n", &save);
    while (line != NULL) {
        char *separator;
        KeyValue *items;
        char *decoded = NULL;
        size_t index;

        if (strlen(line) > HISTORY_LINE_LIMIT ||
            (separator = strchr(line, '=')) == NULL || separator == line) {
            result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored report sidecar contains a malformed line");
            break;
        }
        *separator = '\0';
        for (index = 0U; index < list->count; index++) {
            if (strcmp(list->items[index].key, line) == 0) {
                result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                    "stored report sidecar contains a duplicate key");
                break;
            }
        }
        if (result.status != SOLAR_STATUS_OK) break;
        result = decode_value(separator + 1U, &decoded);
        if (result.status != SOLAR_STATUS_OK) break;
        items = realloc(list->items, (list->count + 1U) * sizeof(*items));
        if (items == NULL) {
            free(decoded);
            result = solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate stored report fields", "free memory and try again");
            break;
        }
        list->items = items;
        items[list->count].key = strdup(line);
        items[list->count].value = decoded;
        if (items[list->count].key == NULL) {
            result = solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
                "could not copy a stored report key", "free memory and try again");
            break;
        }
        list->count++;
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    if (result.status != SOLAR_STATUS_OK) key_values_free(list);
    return result;
}

static bool parse_uint64_value(const char *text, uint64_t *value_out)
{
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || text[0] == '\0' || text[0] == '-') return false;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *value_out = (uint64_t)value;
    return true;
}

static SolarResult write_pair(FILE *file, const char *key, const char *value)
{
    SolarResult result;

    if (fprintf(file, "%s=", key) < 0) return history_error(
        SOLAR_STATUS_IO_ERROR, "cannot write build history key");
    result = write_encoded(file, value);
    if (result.status == SOLAR_STATUS_OK && fputc('\n', file) == EOF) {
        result = history_error(SOLAR_STATUS_IO_ERROR,
            "cannot finish a build history value");
    }
    return result;
}

static SolarResult finish_file(FILE *file)
{
    bool failed = false;

    if (fflush(file) != 0) failed = true;
    if (!failed && fsync(fileno(file)) != 0) failed = true;
    if (fclose(file) != 0) failed = true;
    if (failed) {
        return history_error(SOLAR_STATUS_IO_ERROR,
            "cannot finish a build history sidecar");
    }
    return solar_result_ok();
}

static SolarResult open_output(const char *path, FILE **file_out)
{
    int descriptor = open(path,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);

    *file_out = NULL;
    if (descriptor < 0) return history_error(SOLAR_STATUS_IO_ERROR,
        "cannot create a build history sidecar");
    *file_out = fdopen(descriptor, "w");
    if (*file_out == NULL) {
        (void)close(descriptor);
        return history_error(SOLAR_STATUS_IO_ERROR,
            "cannot stream a build history sidecar");
    }
    return solar_result_ok();
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t index;

    for (index = 0U; index < size; index++) {
        hash ^= (uint64_t)bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_text(uint64_t hash, const char *text)
{
    static const unsigned char separator = 0xffU;

    if (text != NULL) hash = hash_bytes(hash, text, strlen(text));
    return hash_bytes(hash, &separator, 1U);
}

static uint64_t hash_list(uint64_t hash, const SolarStringList *list)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        hash = hash_text(hash, list->items[index]);
    }
    return hash;
}

static char *hash_string(uint64_t hash)
{
    char *text = malloc(17U);

    if (text != NULL) (void)snprintf(text, 17U, "%016" PRIx64, hash);
    return text;
}

static uint64_t hash_source_file(
    uint64_t hash,
    const SolarProject *project,
    const char *relative_path
)
{
    char *path = NULL;
    int descriptor = -1;
    unsigned char buffer[8192];
    ssize_t count;

    hash = hash_text(hash, relative_path);
    if (relative_path == NULL || solar_project_resolve_path(
            project, relative_path, &path).status != SOLAR_STATUS_OK) {
        free(path);
        return hash;
    }
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    free(path);
    if (descriptor < 0) return hash;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        hash = hash_bytes(hash, buffer, (size_t)count);
    }
    (void)close(descriptor);
    return hash;
}

static char *fingerprint_sources(const SolarProject *project, bool simulation)
{
    const SolarConfig *config = &project->config;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0U; index < config->sources.rtl.count; index++) {
        hash = hash_source_file(hash, project, config->sources.rtl.items[index]);
    }
    if (config->compiler.source != NULL) {
        hash = hash_source_file(hash, project, config->compiler.source);
    }
    if (simulation) {
        for (index = 0U; index < config->test_count; index++) {
            hash = hash_text(hash, config->tests[index].name);
            for (size_t source = 0U;
                 source < config->tests[index].sources.count; source++) {
                hash = hash_source_file(hash, project,
                    config->tests[index].sources.items[source]);
            }
            if (config->tests[index].cocotb != NULL) {
                hash = hash_source_file(hash, project,
                    config->tests[index].cocotb);
            }
        }
    }
    return hash_string(hash);
}

static char *fingerprint_effective(
    const SolarConfig *config,
    const char *profile_name,
    bool defines_only
)
{
    const SolarProfile *profile = profile_name == NULL ? NULL :
        solar_config_find_profile(config, profile_name);
    SolarEffectiveConfig effective;
    SolarResult result;
    uint64_t hash = UINT64_C(1469598103934665603);
    char *text = NULL;

    solar_effective_config_init(&effective);
    result = solar_config_build_effective(config, profile, NULL, &effective);
    if (result.status == SOLAR_STATUS_OK) {
        if (!defines_only) hash = hash_list(hash, &effective.include_dirs);
        hash = hash_list(hash, &effective.defines);
        text = hash_string(hash);
    }
    solar_effective_config_free(&effective);
    return text;
}

static char *fingerprint_options(
    const SolarConfig *config,
    const char *profile_name,
    bool simulation
)
{
    const SolarProfile *profile = profile_name == NULL ? NULL :
        solar_config_find_profile(config, profile_name);
    SolarEffectiveConfig effective;
    SolarResult result;
    uint64_t hash = UINT64_C(1469598103934665603);
    char *text = NULL;

    solar_effective_config_init(&effective);
    result = solar_config_build_effective(config, profile, NULL, &effective);
    if (result.status == SOLAR_STATUS_OK) {
        hash = hash_text(hash, profile_name);
        hash = hash_text(hash, simulation ? config->simulation.backend :
            config->synthesis.backend);
        hash = hash_text(hash, simulation ? NULL : config->synthesis.top);
        hash = hash_list(hash, &effective.include_dirs);
        hash = hash_list(hash, &effective.defines);
        text = hash_string(hash);
    }
    solar_effective_config_free(&effective);
    return text;
}

static char *fingerprint_file(const char *path)
{
    int descriptor;
    unsigned char buffer[8192];
    ssize_t count;
    uint64_t hash = UINT64_C(1469598103934665603);

    if (path == NULL) return NULL;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return NULL;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        hash = hash_bytes(hash, buffer, (size_t)count);
    }
    (void)close(descriptor);
    return count < 0 ? NULL : hash_string(hash);
}

static void capture_simulator_version(
    const SolarProject *project,
    SolarBuildMetadata *metadata
)
{
    SolarToolReport *reports = NULL;
    size_t count = 0U;
    size_t index;

    if (metadata->simulation_tool_name == NULL ||
        solar_backend_doctor_project(project, false, &reports, &count).status !=
            SOLAR_STATUS_OK) return;
    for (index = 0U; index < count; index++) {
        if (reports[index].name != NULL && reports[index].version != NULL &&
            strcmp(reports[index].name, metadata->simulation_tool_name) == 0) {
            (void)copy_optional(&metadata->simulation_tool_version,
                reports[index].version);
            break;
        }
    }
    solar_backend_tool_reports_free(reports, count);
}

static SolarResult append_name(char **text, const char *name)
{
    size_t previous = *text == NULL ? 0U : strlen(*text);
    size_t length = strlen(name);
    char *grown;

    if (previous > SIZE_MAX - length - 2U) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR, "simulation name set is too large",
        "reduce the number of tests");
    grown = realloc(*text, previous + length + (previous == 0U ? 1U : 2U));
    if (grown == NULL) return solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate simulation context", "free memory and try again");
    if (previous != 0U) grown[previous++] = ',';
    (void)memcpy(grown + previous, name, length + 1U);
    *text = grown;
    return solar_result_ok();
}

static SolarResult metadata_from_context(
    const SolarBuildContext *context,
    const char *build_id,
    SolarBuildMetadata *metadata
)
{
    const SolarConfig *config = &context->project.config;
    SolarSystemInfo system;
    struct tm utc;
    char timestamp[32];
    uint64_t host_hash = UINT64_C(1469598103934665603);
    SolarResult result;
    size_t index;

    solar_build_metadata_init(metadata);
    if (gmtime_r(&context->finished_at, &utc) == NULL ||
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0U) {
        (void)snprintf(timestamp, sizeof(timestamp), "%s", "unavailable");
    }
    result = copy_optional(&metadata->build_id, build_id);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(
        &metadata->timestamp, timestamp);
    metadata->project_id = hash_string(hash_text(
        UINT64_C(1469598103934665603), context->project.root));
    if (metadata->project_id == NULL) result = solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR, "could not allocate project identifier",
        "free memory and try again");
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(&metadata->status,
        context->result.status == SOLAR_STATUS_OK ? "success" : "failed");
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(
        &metadata->solar_version, SOLAR_VERSION);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(&metadata->top,
        config->synthesis.top != NULL ? config->synthesis.top :
            config->compiler.processor);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(&metadata->profile,
        context->profile_name == NULL ? "default" : context->profile_name);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(
        &metadata->synthesis_backend, config->synthesis.backend);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(
        &metadata->synthesis_tool_name, config->synthesis.backend);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(
        &metadata->synthesis_tool_version,
        context->synthesis_result.statistics.tool_version);
    metadata->synthesis_source_fingerprint = fingerprint_sources(
        &context->project, false);
    metadata->synthesis_defines_fingerprint = fingerprint_effective(
        config, context->profile_name, true);
    metadata->synthesis_options_fingerprint = fingerprint_options(
        config, context->profile_name, false);
    metadata->synthesis_script_fingerprint = fingerprint_file(
        context->synthesis_result.script_path);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(
        &metadata->simulation_backend, config->simulation.backend);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(
        &metadata->simulation_tool_name,
        config->simulation.backend != NULL &&
            strcmp(config->simulation.backend, "iverilog") == 0
            ? "vvp" : config->simulation.backend);
    if (result.status == SOLAR_STATUS_OK && !context->dry_run) {
        capture_simulator_version(&context->project, metadata);
    }
    if (context->has_test_result && context->test_result.name != NULL) {
        result = append_name(&metadata->simulation_testbench,
            context->test_result.name);
        metadata->waveform_enabled_present = true;
        metadata->waveform_enabled = context->test_result.waveform_path != NULL;
    }
    if (result.status == SOLAR_STATUS_OK && context->has_test_summary) {
        metadata->waveform_enabled_present = true;
        for (index = 0U; index < context->test_summary.count; index++) {
            const SolarTestResult *simulation = &context->test_summary.results[index];

            if (simulation->name != NULL) result = append_name(
                &metadata->simulation_testbench, simulation->name);
            if (simulation->waveform_path != NULL) metadata->waveform_enabled = true;
            if (result.status != SOLAR_STATUS_OK) break;
        }
    }
    metadata->simulation_testbench_fingerprint = hash_string(hash_text(
        UINT64_C(1469598103934665603), metadata->simulation_testbench));
    metadata->simulation_source_fingerprint = fingerprint_sources(
        &context->project, true);
    metadata->simulation_options_fingerprint = fingerprint_options(
        config, context->profile_name, true);
    if (solar_system_info_collect(&system).status == SOLAR_STATUS_OK) {
        result = result.status == SOLAR_STATUS_OK ? copy_optional(
            &metadata->environment_os, system.operating_system) : result;
        if (result.status == SOLAR_STATUS_OK) result = copy_optional(
            &metadata->environment_arch, system.architecture);
        host_hash = hash_text(host_hash, system.operating_system);
        host_hash = hash_text(host_hash, system.architecture);
        host_hash = hash_text(host_hash, system.cpu_model);
        host_hash = hash_bytes(host_hash, &system.logical_cpus,
            sizeof(system.logical_cpus));
        metadata->environment_host_fingerprint = hash_string(host_hash);
        if (system.logical_cpus >= 0) {
            metadata->environment_cpu_count_present = true;
            metadata->environment_cpu_count = (uint64_t)system.logical_cpus;
        }
    }
    /* Failed validation can leave optional context unavailable. The history
     * record is still useful and must not be discarded for missing hashes. */
    if (result.status != SOLAR_STATUS_OK || metadata->project_id == NULL) {
        solar_build_metadata_free(metadata);
        return result.status == SOLAR_STATUS_OK ? solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR, "could not create a project identifier",
            "free memory and try again") : result;
    }
    return solar_result_ok();
}

static bool safe_add(uint64_t *total, uint64_t value)
{
    if (UINT64_MAX - *total < value) return false;
    *total += value;
    return true;
}

static bool collect_one_timing(
    const SolarTestResult *test,
    SolarSimulationTimings *timings
)
{
    bool compilation_attempted = test->compile_duration_ns > 0U ||
        test->result.status == SOLAR_STATUS_OK ||
        test->failure_kind == SOLAR_TEST_FAILURE_SIMULATION_COMPILE ||
        test->failure_kind == SOLAR_TEST_FAILURE_SIMULATION_RUNTIME ||
        test->failure_kind == SOLAR_TEST_FAILURE_LOGICAL;
    bool execution_attempted = test->simulation_duration_ns > 0U ||
        test->result.status == SOLAR_STATUS_OK ||
        test->failure_kind == SOLAR_TEST_FAILURE_SIMULATION_RUNTIME ||
        test->failure_kind == SOLAR_TEST_FAILURE_LOGICAL;

    timings->simulation_status_present = true;
    if (compilation_attempted) {
        timings->simulation_compile.present = true;
        if (!safe_add(&timings->simulation_compile.value,
                test->compile_duration_ns)) return false;
    }
    if (execution_attempted) {
        timings->simulation_execution.present = true;
        if (!safe_add(&timings->simulation_execution.value,
                test->simulation_duration_ns)) return false;
    }
    if (test->result.status != SOLAR_STATUS_OK) {
        timings->simulation_succeeded = false;
    }
    return true;
}

static SolarResult timings_from_context(
    const SolarBuildContext *context,
    SolarSimulationTimings *timings,
    bool *available
)
{
    size_t index;

    solar_simulation_timings_init(timings);
    *available = context->has_test_result || context->has_test_summary;
    if (!*available) return solar_result_ok();
    timings->simulation_succeeded = true;
    if (context->has_test_result &&
        !collect_one_timing(&context->test_result, timings)) goto overflow;
    for (index = 0U; index < context->test_summary.count; index++) {
        if (!collect_one_timing(&context->test_summary.results[index], timings)) {
            goto overflow;
        }
    }
    for (index = 0U; index < context->step_count; index++) {
        if (strcmp(context->steps[index].name, "sim") == 0 ||
            strcmp(context->steps[index].name, "test") == 0) {
            timings->simulation_total.present = true;
            timings->simulation_total.value = context->steps[index].duration_ns;
            break;
        }
    }
    *available = timings->simulation_compile.present ||
        timings->simulation_execution.present || timings->simulation_total.present ||
        timings->simulated_duration.present;
    return solar_result_ok();

overflow:
    solar_simulation_timings_free(timings);
    return history_error(SOLAR_STATUS_CONFIG_ERROR,
        "simulation timing total exceeds the supported 64-bit range");
}

static SolarResult write_metadata_file(
    const char *path,
    const SolarBuildMetadata *metadata
)
{
    FILE *file = NULL;
    SolarResult result = open_output(path, &file);
    struct { const char *key; const char *value; } fields[] = {
        {"schema", "1"}, {"build_id", metadata->build_id},
        {"timestamp", metadata->timestamp}, {"project_id", metadata->project_id},
        {"status", metadata->status}, {"solar_version", metadata->solar_version},
        {"top", metadata->top}, {"profile", metadata->profile},
        {"synthesis.backend", metadata->synthesis_backend},
        {"synthesis.tool_name", metadata->synthesis_tool_name},
        {"synthesis.tool_version", metadata->synthesis_tool_version},
        {"synthesis.source_fingerprint", metadata->synthesis_source_fingerprint},
        {"synthesis.defines_fingerprint", metadata->synthesis_defines_fingerprint},
        {"synthesis.options_fingerprint", metadata->synthesis_options_fingerprint},
        {"synthesis.script_fingerprint", metadata->synthesis_script_fingerprint},
        {"simulation.backend", metadata->simulation_backend},
        {"simulation.tool_name", metadata->simulation_tool_name},
        {"simulation.tool_version", metadata->simulation_tool_version},
        {"simulation.testbench", metadata->simulation_testbench},
        {"simulation.testbench_fingerprint", metadata->simulation_testbench_fingerprint},
        {"simulation.source_fingerprint", metadata->simulation_source_fingerprint},
        {"simulation.options_fingerprint", metadata->simulation_options_fingerprint},
        {"simulation.input_fingerprint", metadata->simulation_input_fingerprint},
        {"environment.os", metadata->environment_os},
        {"environment.arch", metadata->environment_arch},
        {"environment.host_fingerprint", metadata->environment_host_fingerprint}
    };
    char number[32];
    size_t index;

    if (result.status != SOLAR_STATUS_OK || file == NULL) {
        if (file != NULL) (void)fclose(file);
        return result.status != SOLAR_STATUS_OK ? result : history_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "build history metadata output was not initialized");
    }
    for (index = 0U; index < sizeof(fields) / sizeof(fields[0]); index++) {
        if (fields[index].value == NULL) continue;
        result = write_pair(file, fields[index].key, fields[index].value);
        if (result.status != SOLAR_STATUS_OK) break;
    }
    if (result.status == SOLAR_STATUS_OK && metadata->environment_cpu_count_present) {
        (void)snprintf(number, sizeof(number), "%" PRIu64,
            metadata->environment_cpu_count);
        result = write_pair(file, "environment.cpu_count", number);
    }
    if (result.status == SOLAR_STATUS_OK && metadata->waveform_enabled_present) {
        result = write_pair(file, "simulation.waveform_enabled",
            metadata->waveform_enabled ? "1" : "0");
    }
    if (result.status == SOLAR_STATUS_OK) return finish_file(file);
    (void)fclose(file);
    return result;
}

static uint64_t statistics_value(
    const SolarGenericSynthesisStatistics *statistics,
    SolarSynthesisStatisticsField field
)
{
    switch (field) {
    case SOLAR_SYNTHESIS_FIELD_MODULES: return statistics->modules;
    case SOLAR_SYNTHESIS_FIELD_WIRES: return statistics->wires;
    case SOLAR_SYNTHESIS_FIELD_WIRE_BITS: return statistics->wire_bits;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES: return statistics->public_wires;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS: return statistics->public_wire_bits;
    case SOLAR_SYNTHESIS_FIELD_MEMORIES: return statistics->memories;
    case SOLAR_SYNTHESIS_FIELD_MEMORY_BITS: return statistics->memory_bits;
    case SOLAR_SYNTHESIS_FIELD_PROCESSES: return statistics->processes;
    case SOLAR_SYNTHESIS_FIELD_CELLS: return statistics->cells;
    }
    return 0U;
}

static SolarResult write_metric(
    FILE *file,
    const char *name,
    bool present,
    uint64_t value
)
{
    char key[128];
    char number[32];
    SolarResult result;

    (void)snprintf(key, sizeof(key), "metric.%s.present", name);
    result = write_pair(file, key, present ? "1" : "0");
    if (result.status == SOLAR_STATUS_OK && present) {
        (void)snprintf(key, sizeof(key), "metric.%s.value", name);
        (void)snprintf(number, sizeof(number), "%" PRIu64, value);
        result = write_pair(file, key, number);
    }
    return result;
}

static SolarResult write_statistics_file(
    const char *path,
    const SolarGenericSynthesisStatistics *statistics
)
{
    static const char *const names[] = {"modules", "wires", "wire_bits",
        "public_wires", "public_wire_bits", "memories", "memory_bits",
        "processes", "cells"};
    static const SolarSynthesisStatisticsField fields[] = {
        SOLAR_SYNTHESIS_FIELD_MODULES, SOLAR_SYNTHESIS_FIELD_WIRES,
        SOLAR_SYNTHESIS_FIELD_WIRE_BITS, SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES,
        SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS, SOLAR_SYNTHESIS_FIELD_MEMORIES,
        SOLAR_SYNTHESIS_FIELD_MEMORY_BITS, SOLAR_SYNTHESIS_FIELD_PROCESSES,
        SOLAR_SYNTHESIS_FIELD_CELLS};
    FILE *file = NULL;
    SolarResult result = open_output(path, &file);
    char key[128];
    char number[32];
    size_t index;

    if (result.status != SOLAR_STATUS_OK || file == NULL) {
        if (file != NULL) (void)fclose(file);
        return result.status != SOLAR_STATUS_OK ? result : history_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "build history statistics output was not initialized");
    }
    result = write_pair(file, "schema", "1");
    if (result.status == SOLAR_STATUS_OK) result = write_pair(file, "kind",
        "generic-synthesis-statistics");
    for (index = 0U; result.status == SOLAR_STATUS_OK && index < 9U; index++) {
        result = write_metric(file, names[index],
            solar_synthesis_statistics_has_field(statistics, fields[index]),
            statistics_value(statistics, fields[index]));
    }
    if (result.status == SOLAR_STATUS_OK) {
        (void)snprintf(number, sizeof(number), "%zu", statistics->cell_type_count);
        result = write_pair(file, "cell.count", number);
    }
    for (index = 0U; result.status == SOLAR_STATUS_OK &&
         index < statistics->cell_type_count; index++) {
        (void)snprintf(key, sizeof(key), "cell.%zu.type", index);
        result = write_pair(file, key, statistics->cell_types[index].type);
        if (result.status == SOLAR_STATUS_OK) {
            (void)snprintf(key, sizeof(key), "cell.%zu.value", index);
            (void)snprintf(number, sizeof(number), "%" PRIu64,
                statistics->cell_types[index].count);
            result = write_pair(file, key, number);
        }
    }
    if (result.status == SOLAR_STATUS_OK && statistics->report_path != NULL) {
        result = write_pair(file, "source_report", statistics->report_path);
    }
    if (result.status == SOLAR_STATUS_OK) return finish_file(file);
    (void)fclose(file);
    return result;
}

static SolarResult write_timer(
    FILE *file,
    const char *name,
    SolarStoredMetric metric
)
{
    char key[160];
    char number[32];
    SolarResult result;

    (void)snprintf(key, sizeof(key), "timer.%s.present", name);
    result = write_pair(file, key, metric.present ? "1" : "0");
    if (result.status == SOLAR_STATUS_OK && metric.present) {
        (void)snprintf(key, sizeof(key), "timer.%s.nanoseconds", name);
        (void)snprintf(number, sizeof(number), "%" PRIu64, metric.value);
        result = write_pair(file, key, number);
    }
    return result;
}

static SolarResult write_timings_file(
    const char *path,
    const SolarSimulationTimings *timings
)
{
    FILE *file = NULL;
    SolarResult result = open_output(path, &file);

    if (result.status != SOLAR_STATUS_OK || file == NULL) {
        if (file != NULL) (void)fclose(file);
        return result.status != SOLAR_STATUS_OK ? result : history_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "build history timing output was not initialized");
    }
    result = write_pair(file, "schema", "1");
    if (result.status == SOLAR_STATUS_OK) result = write_pair(file, "kind",
        "build-timings");
    if (result.status == SOLAR_STATUS_OK) result = write_timer(file,
        "simulation_compile", timings->simulation_compile);
    if (result.status == SOLAR_STATUS_OK) result = write_timer(file,
        "simulation_elaboration", timings->simulation_elaboration);
    if (result.status == SOLAR_STATUS_OK) result = write_timer(file,
        "simulation_execution", timings->simulation_execution);
    if (result.status == SOLAR_STATUS_OK) result = write_timer(file,
        "simulation_total", timings->simulation_total);
    if (result.status == SOLAR_STATUS_OK) result = write_pair(file,
        "simulation.status", timings->simulation_status_present
            ? (timings->simulation_succeeded ? "success" : "failed")
            : "unavailable");
    if (result.status == SOLAR_STATUS_OK) result = write_pair(file,
        "simulation.simulated_duration.present",
        timings->simulated_duration.present ? "1" : "0");
    if (result.status == SOLAR_STATUS_OK && timings->simulated_duration.present) {
        char number[32];

        (void)snprintf(number, sizeof(number), "%" PRIu64,
            timings->simulated_duration.value);
        result = write_pair(file, "simulation.simulated_duration.value", number);
        if (result.status == SOLAR_STATUS_OK) result = write_pair(file,
            "simulation.simulated_duration.unit",
            timings->simulated_duration_unit);
    }
    if (result.status == SOLAR_STATUS_OK) return finish_file(file);
    (void)fclose(file);
    return result;
}

static SolarResult copy_file(const char *source, const char *destination)
{
    char *text = NULL;
    FILE *file = NULL;
    SolarResult result = read_regular_text(source, &text);

    if (result.status != SOLAR_STATUS_OK) {
        free(text);
        return result;
    }
    if (text == NULL) return history_error(SOLAR_STATUS_INTERNAL_ERROR,
        "stored human report returned no data");
    result = open_output(destination, &file);
    if (result.status != SOLAR_STATUS_OK || file == NULL) {
        free(text);
        if (file != NULL) (void)fclose(file);
        return result.status != SOLAR_STATUS_OK ? result : history_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "stored human report output was not initialized");
    }
    if (fputs(text, file) == EOF) {
        result = history_error(SOLAR_STATUS_IO_ERROR,
            "cannot copy the human build report into history");
    }
    free(text);
    if (result.status == SOLAR_STATUS_OK) return finish_file(file);
    if (file != NULL) (void)fclose(file);
    return result;
}

static uint64_t scan_maximum_id(const char *reports_path)
{
    DIR *directory = opendir(reports_path);
    struct dirent *entry;
    uint64_t maximum = 0U;

    if (directory == NULL) return 0U;
    while ((entry = readdir(directory)) != NULL) {
        uint64_t value;

        if (valid_build_id(entry->d_name) &&
            parse_uint64_value(entry->d_name + 6U, &value) && value > maximum) {
            maximum = value;
        }
    }
    (void)closedir(directory);
    return maximum;
}

static SolarResult next_build_number(
    const char *reports_path,
    const char *sequence_path,
    uint64_t *next_out
)
{
    uint64_t maximum = scan_maximum_id(reports_path);
    char *text = NULL;
    uint64_t sequence_value = 0U;
    SolarResult result;

    if (regular_file_exists(sequence_path)) {
        char *newline;

        result = read_regular_text(sequence_path, &text);
        if (result.status != SOLAR_STATUS_OK) {
            free(text);
            return result;
        }
        if (text == NULL) return history_error(SOLAR_STATUS_INTERNAL_ERROR,
            "build history sequence marker returned no data");
        newline = strpbrk(text, "\r\n");
        if (newline != NULL) *newline = '\0';
        if (!parse_uint64_value(text, &sequence_value)) {
            free(text);
            return history_error(SOLAR_STATUS_CONFIG_ERROR,
                "build history sequence marker is malformed");
        }
        free(text);
        if (sequence_value > maximum) maximum = sequence_value;
    }
    if (maximum == UINT64_MAX) return history_error(SOLAR_STATUS_CONFIG_ERROR,
        "build history identifier space is exhausted");
    *next_out = maximum + 1U;
    return solar_result_ok();
}

static bool write_all(int descriptor, const char *text, size_t length)
{
    size_t offset = 0U;

    while (offset < length) {
        ssize_t written = write(descriptor, text + offset, length - offset);

        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        offset += (size_t)written;
    }
    return true;
}

static SolarResult atomic_text_replace(const char *path, const char *text)
{
    size_t length = strlen(path);
    char *temporary = malloc(length + 5U);
    int descriptor;
    size_t text_length = strlen(text);
    SolarResult result = solar_result_ok();

    if (temporary == NULL) return solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate atomic history path", "free memory and try again");
    (void)snprintf(temporary, length + 5U, "%s.tmp", path);
    descriptor = open(temporary,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) result = history_error(SOLAR_STATUS_IO_ERROR,
        "cannot create an atomic build history marker");
    else {
        bool complete = write_all(descriptor, text, text_length);
        int sync_result = complete ? fsync(descriptor) : -1;
        int close_result = close(descriptor);

        if (!complete || sync_result != 0 || close_result != 0 ||
            rename(temporary, path) != 0) {
            result = history_error(SOLAR_STATUS_IO_ERROR,
                "cannot publish an atomic build history marker");
        }
    }
    if (result.status != SOLAR_STATUS_OK) (void)unlink(temporary);
    free(temporary);
    return result;
}

static SolarResult sync_directory(const char *path)
{
    int descriptor = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int sync_error = 0;
    int close_error;

    if (descriptor < 0) return history_error(SOLAR_STATUS_IO_ERROR,
        "cannot open the build history directory for synchronization");
    if (fsync(descriptor) != 0 && errno != EINVAL && errno != ENOTSUP) {
        sync_error = errno;
    }
    close_error = close(descriptor);
    if (sync_error != 0 || close_error != 0) return history_error(
        SOLAR_STATUS_IO_ERROR,
        "cannot synchronize the build history directory");
    return solar_result_ok();
}

static void remove_staging(const char *directory)
{
    static const char *const files[] = {
        "report.txt", "synthesis-stats.dat", "timings.dat", "metadata.dat"};
    size_t index;

    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        char *path = NULL;

        if (solar_filesystem_join(directory, files[index], &path).status ==
            SOLAR_STATUS_OK) (void)unlink(path);
        free(path);
    }
    (void)rmdir(directory);
}

SolarResult solar_report_history_store(
    const SolarBuildContext *context,
    const char *latest_report_path
)
{
    char *reports = NULL;
    char *sequence = NULL;
    char *staging = NULL;
    char *final = NULL;
    char *latest = NULL;
    char *path = NULL;
    char build_id[64];
    char staging_name[96];
    char counter[80];
    uint64_t next = 0U;
    SolarBuildMetadata metadata;
    SolarSimulationTimings timings;
    bool has_timings = false;
    SolarResult result;

    solar_build_metadata_init(&metadata);
    solar_simulation_timings_init(&timings);
    result = solar_filesystem_prepare_generated_directory(
        context->project.root, ".solar/reports");
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_join(context->project.root, ".solar/reports", &reports);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_join(reports, "sequence", &sequence);
    if (result.status == SOLAR_STATUS_OK) result = next_build_number(
        reports, sequence, &next);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    (void)snprintf(build_id, sizeof(build_id), "build-%06" PRIu64, next);
    (void)snprintf(counter, sizeof(counter), "%" PRIu64 "\n", next);
    result = atomic_text_replace(sequence, counter);
    if (result.status == SOLAR_STATUS_OK) result = sync_directory(reports);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    (void)snprintf(staging_name, sizeof(staging_name), ".pending-%s-%ld",
        build_id, (long)getpid());
    result = solar_filesystem_join(reports, staging_name, &staging);
    if (result.status == SOLAR_STATUS_OK) result = solar_filesystem_join(
        reports, build_id, &final);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (mkdir(staging, 0700) != 0) {
        result = history_error(SOLAR_STATUS_IO_ERROR,
            "cannot create the build history staging directory");
        goto cleanup;
    }
    result = metadata_from_context(context, build_id, &metadata);
    if (result.status == SOLAR_STATUS_OK) result = timings_from_context(
        context, &timings, &has_timings);
    if (result.status == SOLAR_STATUS_OK) result = solar_filesystem_join(
        staging, "report.txt", &path);
    if (result.status == SOLAR_STATUS_OK) result = copy_file(
        latest_report_path, path);
    free(path); path = NULL;
    if (result.status == SOLAR_STATUS_OK) result = solar_filesystem_join(
        staging, "metadata.dat", &path);
    if (result.status == SOLAR_STATUS_OK) result = write_metadata_file(path, &metadata);
    free(path); path = NULL;
    if (result.status == SOLAR_STATUS_OK && context->has_synthesis_result &&
        context->synthesis_result.statistics.available) {
        result = solar_filesystem_join(staging, "synthesis-stats.dat", &path);
        if (result.status == SOLAR_STATUS_OK) result = write_statistics_file(
            path, &context->synthesis_result.statistics);
        free(path); path = NULL;
    }
    if (result.status == SOLAR_STATUS_OK && has_timings) {
        result = solar_filesystem_join(staging, "timings.dat", &path);
        if (result.status == SOLAR_STATUS_OK) result = write_timings_file(path, &timings);
        free(path); path = NULL;
    }
    if (result.status == SOLAR_STATUS_OK && rename(staging, final) != 0) {
        result = history_error(SOLAR_STATUS_IO_ERROR,
            "cannot publish the completed build history record");
    }
    if (result.status == SOLAR_STATUS_OK) result = sync_directory(reports);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_join(reports, "latest", &latest);
    }
    if (result.status == SOLAR_STATUS_OK) {
        (void)snprintf(counter, sizeof(counter), "%s\n", build_id);
        result = atomic_text_replace(latest, counter);
    }
    if (result.status == SOLAR_STATUS_OK) result = sync_directory(reports);

cleanup:
    if (result.status != SOLAR_STATUS_OK && staging != NULL) remove_staging(staging);
    free(path);
    free(latest);
    free(final);
    free(staging);
    free(sequence);
    free(reports);
    solar_simulation_timings_free(&timings);
    solar_build_metadata_free(&metadata);
    return result;
}

static SolarResult parse_metric(
    const KeyValueList *values,
    const char *prefix,
    const char *value_suffix,
    SolarStoredMetric *metric
)
{
    char present_key[160];
    char value_key[160];
    const char *present;
    const char *number;

    (void)snprintf(present_key, sizeof(present_key), "%s.present", prefix);
    (void)snprintf(value_key, sizeof(value_key), "%s.%s", prefix, value_suffix);
    present = key_value(values, present_key);
    number = key_value(values, value_key);
    if (present == NULL || (strcmp(present, "0") != 0 && strcmp(present, "1") != 0)) {
        return history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored report metric has an invalid presence marker");
    }
    metric->present = strcmp(present, "1") == 0;
    if (metric->present && !parse_uint64_value(number, &metric->value)) {
        return history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored report metric has an invalid integer value");
    }
    if (!metric->present && number != NULL) return history_error(
        SOLAR_STATUS_CONFIG_ERROR,
        "stored report metric has a value while marked unavailable");
    return solar_result_ok();
}

static uint64_t *statistics_destination(
    SolarGenericSynthesisStatistics *statistics,
    size_t index
)
{
    uint64_t *items[] = {&statistics->modules, &statistics->wires,
        &statistics->wire_bits, &statistics->public_wires,
        &statistics->public_wire_bits, &statistics->memories,
        &statistics->memory_bits, &statistics->processes, &statistics->cells};
    return items[index];
}

static SolarResult load_statistics(
    const char *path,
    SolarGenericSynthesisStatistics *statistics
)
{
    static const char *const names[] = {"modules", "wires", "wire_bits",
        "public_wires", "public_wire_bits", "memories", "memory_bits",
        "processes", "cells"};
    static const SolarSynthesisStatisticsField fields[] = {
        SOLAR_SYNTHESIS_FIELD_MODULES, SOLAR_SYNTHESIS_FIELD_WIRES,
        SOLAR_SYNTHESIS_FIELD_WIRE_BITS, SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES,
        SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS, SOLAR_SYNTHESIS_FIELD_MEMORIES,
        SOLAR_SYNTHESIS_FIELD_MEMORY_BITS, SOLAR_SYNTHESIS_FIELD_PROCESSES,
        SOLAR_SYNTHESIS_FIELD_CELLS};
    KeyValueList values;
    SolarResult result = read_key_values(path, &values);
    uint64_t count = 0U;
    size_t index;

    if (result.status != SOLAR_STATUS_OK) return result;
    if (strcmp(key_value(&values, "schema") == NULL ? "" :
            key_value(&values, "schema"), "1") != 0 ||
        strcmp(key_value(&values, "kind") == NULL ? "" :
            key_value(&values, "kind"), "generic-synthesis-statistics") != 0) {
        result = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored GSS sidecar has an unsupported schema or kind");
        goto cleanup;
    }
    for (index = 0U; index < 9U; index++) {
        SolarStoredMetric metric;
        char prefix[128];

        (void)snprintf(prefix, sizeof(prefix), "metric.%s", names[index]);
        (void)memset(&metric, 0, sizeof(metric));
        result = parse_metric(&values, prefix, "value", &metric);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        if (metric.present) {
            statistics->reported_fields |= (uint32_t)fields[index];
            *statistics_destination(statistics, index) = metric.value;
        }
    }
    if (!parse_uint64_value(key_value(&values, "cell.count"), &count) ||
        count > HISTORY_CELL_LIMIT || count > SIZE_MAX / sizeof(*statistics->cell_types)) {
        result = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored GSS sidecar has an invalid cell count");
        goto cleanup;
    }
    if (count > 0U) {
        statistics->cell_types = calloc((size_t)count, sizeof(*statistics->cell_types));
        if (statistics->cell_types == NULL) {
            result = solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate stored cell usage", "free memory and try again");
            goto cleanup;
        }
    }
    for (index = 0U; index < (size_t)count; index++) {
        char key[128];
        const char *type;
        const char *number;
        size_t prior;

        (void)snprintf(key, sizeof(key), "cell.%zu.type", index);
        type = key_value(&values, key);
        (void)snprintf(key, sizeof(key), "cell.%zu.value", index);
        number = key_value(&values, key);
        if (type == NULL || type[0] == '\0' || strlen(type) > 255U ||
            !parse_uint64_value(number, &statistics->cell_types[index].count)) {
            result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored GSS sidecar contains an invalid cell entry");
            goto cleanup;
        }
        for (prior = 0U; prior < index; prior++) {
            if (strcmp(statistics->cell_types[prior].type, type) == 0) {
                result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                    "stored GSS sidecar contains a duplicate cell type");
                goto cleanup;
            }
        }
        statistics->cell_types[index].type = strdup(type);
        if (statistics->cell_types[index].type == NULL) {
            result = solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
                "could not copy stored cell type", "free memory and try again");
            goto cleanup;
        }
        statistics->cell_type_count++;
    }
    statistics->available = statistics->reported_fields != 0U || count > 0U;
    statistics->completeness = statistics->reported_fields ==
        (uint32_t)((1U << 9) - 1U) ? SOLAR_SYNTHESIS_STATISTICS_COMPLETE :
        (statistics->available ? SOLAR_SYNTHESIS_STATISTICS_PARTIAL :
            SOLAR_SYNTHESIS_STATISTICS_NONE);
    result = copy_optional(&statistics->report_path,
        key_value(&values, "source_report"));

cleanup:
    key_values_free(&values);
    if (result.status != SOLAR_STATUS_OK) solar_synthesis_statistics_free(statistics);
    return result;
}

static SolarResult load_timings(const char *path, SolarSimulationTimings *timings)
{
    KeyValueList values;
    SolarResult result = read_key_values(path, &values);
    const char *status;
    const char *present;

    if (result.status != SOLAR_STATUS_OK) return result;
    if (strcmp(key_value(&values, "schema") == NULL ? "" :
            key_value(&values, "schema"), "1") != 0 ||
        strcmp(key_value(&values, "kind") == NULL ? "" :
            key_value(&values, "kind"), "build-timings") != 0) {
        result = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored timing sidecar has an unsupported schema or kind");
        goto cleanup;
    }
    result = parse_metric(&values, "timer.simulation_compile", "nanoseconds",
        &timings->simulation_compile);
    if (result.status == SOLAR_STATUS_OK) result = parse_metric(&values,
        "timer.simulation_elaboration", "nanoseconds",
        &timings->simulation_elaboration);
    if (result.status == SOLAR_STATUS_OK) result = parse_metric(&values,
        "timer.simulation_execution", "nanoseconds",
        &timings->simulation_execution);
    if (result.status == SOLAR_STATUS_OK) result = parse_metric(&values,
        "timer.simulation_total", "nanoseconds", &timings->simulation_total);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    status = key_value(&values, "simulation.status");
    if (status != NULL && strcmp(status, "unavailable") != 0) {
        if (strcmp(status, "success") != 0 && strcmp(status, "failed") != 0) {
            result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored timing sidecar has an invalid simulation status");
            goto cleanup;
        }
        timings->simulation_status_present = true;
        timings->simulation_succeeded = strcmp(status, "success") == 0;
    }
    present = key_value(&values, "simulation.simulated_duration.present");
    if (present == NULL || (strcmp(present, "0") != 0 && strcmp(present, "1") != 0)) {
        result = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored HDL duration has an invalid presence marker");
        goto cleanup;
    }
    timings->simulated_duration.present = strcmp(present, "1") == 0;
    if (timings->simulated_duration.present) {
        if (!parse_uint64_value(key_value(&values,
                "simulation.simulated_duration.value"),
                &timings->simulated_duration.value)) {
            result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored HDL duration has an invalid value");
            goto cleanup;
        }
        result = copy_optional(&timings->simulated_duration_unit,
            key_value(&values, "simulation.simulated_duration.unit"));
        if (result.status == SOLAR_STATUS_OK &&
            timings->simulated_duration_unit == NULL) {
            result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored HDL duration does not declare a unit");
        }
    }

cleanup:
    key_values_free(&values);
    if (result.status != SOLAR_STATUS_OK) solar_simulation_timings_free(timings);
    return result;
}

static SolarResult assign_metadata_text(
    char **destination,
    const KeyValueList *values,
    const char *key
)
{
    return copy_optional(destination, key_value(values, key));
}

static SolarResult load_metadata(const char *path, SolarBuildMetadata *metadata)
{
    KeyValueList values;
    SolarResult result = read_key_values(path, &values);
    struct { char **destination; const char *key; } fields[] = {
        {&metadata->build_id, "build_id"}, {&metadata->timestamp, "timestamp"},
        {&metadata->project_id, "project_id"}, {&metadata->status, "status"},
        {&metadata->solar_version, "solar_version"}, {&metadata->top, "top"},
        {&metadata->profile, "profile"},
        {&metadata->synthesis_backend, "synthesis.backend"},
        {&metadata->synthesis_tool_name, "synthesis.tool_name"},
        {&metadata->synthesis_tool_version, "synthesis.tool_version"},
        {&metadata->synthesis_source_fingerprint, "synthesis.source_fingerprint"},
        {&metadata->synthesis_defines_fingerprint, "synthesis.defines_fingerprint"},
        {&metadata->synthesis_options_fingerprint, "synthesis.options_fingerprint"},
        {&metadata->synthesis_script_fingerprint, "synthesis.script_fingerprint"},
        {&metadata->simulation_backend, "simulation.backend"},
        {&metadata->simulation_tool_name, "simulation.tool_name"},
        {&metadata->simulation_tool_version, "simulation.tool_version"},
        {&metadata->simulation_testbench, "simulation.testbench"},
        {&metadata->simulation_testbench_fingerprint,
            "simulation.testbench_fingerprint"},
        {&metadata->simulation_source_fingerprint, "simulation.source_fingerprint"},
        {&metadata->simulation_options_fingerprint, "simulation.options_fingerprint"},
        {&metadata->simulation_input_fingerprint, "simulation.input_fingerprint"},
        {&metadata->environment_os, "environment.os"},
        {&metadata->environment_arch, "environment.arch"},
        {&metadata->environment_host_fingerprint, "environment.host_fingerprint"}
    };
    const char *number;
    const char *waveform;
    size_t index;

    if (result.status != SOLAR_STATUS_OK) return result;
    if (strcmp(key_value(&values, "schema") == NULL ? "" :
            key_value(&values, "schema"), "1") != 0) {
        result = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored build metadata uses an unsupported schema");
        goto cleanup;
    }
    for (index = 0U; result.status == SOLAR_STATUS_OK &&
         index < sizeof(fields) / sizeof(fields[0]); index++) {
        result = assign_metadata_text(fields[index].destination,
            &values, fields[index].key);
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (metadata->build_id == NULL || metadata->project_id == NULL ||
        metadata->status == NULL) {
        result = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "stored build metadata is missing a required field");
        goto cleanup;
    }
    number = key_value(&values, "environment.cpu_count");
    if (number != NULL) {
        if (!parse_uint64_value(number, &metadata->environment_cpu_count)) {
            result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored environment CPU count is invalid");
            goto cleanup;
        }
        metadata->environment_cpu_count_present = true;
    }
    waveform = key_value(&values, "simulation.waveform_enabled");
    if (waveform != NULL) {
        if (strcmp(waveform, "0") != 0 && strcmp(waveform, "1") != 0) {
            result = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored waveform flag is invalid");
            goto cleanup;
        }
        metadata->waveform_enabled_present = true;
        metadata->waveform_enabled = strcmp(waveform, "1") == 0;
    }

cleanup:
    key_values_free(&values);
    if (result.status != SOLAR_STATUS_OK) solar_build_metadata_free(metadata);
    return result;
}

static bool regular_file_exists(const char *path)
{
    struct stat information;
    return lstat(path, &information) == 0 && S_ISREG(information.st_mode) &&
        !S_ISLNK(information.st_mode);
}

SolarResult solar_report_history_load(
    const SolarProject *project,
    const char *build_id,
    SolarStoredBuildReport *result
)
{
    char *directory = NULL;
    char *path = NULL;
    SolarResult operation;

    if (project == NULL || project->root == NULL || result == NULL ||
        !valid_build_id(build_id)) return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT, "invalid stored build identifier",
            "use an identifier shown by solar report list");
    solar_stored_build_report_free(result);
    operation = join3(project->root, ".solar/reports", build_id, &directory);
    if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    operation = solar_filesystem_join(directory, "report.txt", &path);
    if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    if (!regular_file_exists(path)) {
        operation = history_error(SOLAR_STATUS_NOT_FOUND,
            "stored build report was not found");
        goto cleanup;
    }
    result->report_path = path;
    path = NULL;
    result->has_report = true;
    operation = solar_filesystem_join(directory, "metadata.dat", &path);
    if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    if (regular_file_exists(path)) {
        operation = load_metadata(path, &result->metadata);
        if (operation.status != SOLAR_STATUS_OK) goto cleanup;
        if (strcmp(result->metadata.build_id, build_id) != 0) {
            operation = history_error(SOLAR_STATUS_CONFIG_ERROR,
                "stored build metadata identifier does not match its directory");
            goto cleanup;
        }
    } else {
        operation = copy_optional(&result->metadata.build_id, build_id);
        if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    }
    free(path); path = NULL;
    operation = solar_filesystem_join(directory, "synthesis-stats.dat", &path);
    if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    if (regular_file_exists(path)) {
        operation = load_statistics(path, &result->synthesis_statistics);
        if (operation.status != SOLAR_STATUS_OK) goto cleanup;
        result->has_synthesis_statistics = true;
        operation = copy_optional(&result->synthesis_statistics.tool,
            result->metadata.synthesis_tool_name);
        if (operation.status == SOLAR_STATUS_OK) operation = copy_optional(
            &result->synthesis_statistics.tool_version,
            result->metadata.synthesis_tool_version);
        if (operation.status == SOLAR_STATUS_OK) operation = copy_optional(
            &result->synthesis_statistics.top, result->metadata.top);
        if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    }
    free(path); path = NULL;
    operation = solar_filesystem_join(directory, "timings.dat", &path);
    if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    if (regular_file_exists(path)) {
        operation = load_timings(path, &result->timings);
        if (operation.status != SOLAR_STATUS_OK) goto cleanup;
        result->has_timings = true;
    }
    operation = solar_result_ok();

cleanup:
    free(path);
    free(directory);
    if (operation.status != SOLAR_STATUS_OK) solar_stored_build_report_free(result);
    return operation;
}

SolarResult solar_report_history_load_latest(
    const SolarProject *project,
    SolarStoredBuildReport *result
)
{
    char *path = NULL;
    char *text = NULL;
    char *newline;
    SolarResult operation;

    if (project == NULL || project->root == NULL || result == NULL) return
        solar_result_error(SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot load latest history without a project and output storage",
            "load the current project first");
    operation = solar_filesystem_join(project->root,
        ".solar/reports/latest", &path);
    if (operation.status == SOLAR_STATUS_OK) operation = read_regular_text(path, &text);
    if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    if (text == NULL) {
        operation = history_error(SOLAR_STATUS_INTERNAL_ERROR,
            "latest build history marker returned no data");
        goto cleanup;
    }
    newline = strpbrk(text, "\r\n");
    if (newline != NULL) *newline = '\0';
    if (!valid_build_id(text)) {
        operation = history_error(SOLAR_STATUS_CONFIG_ERROR,
            "latest build history marker is malformed");
        goto cleanup;
    }
    operation = solar_report_history_load(project, text, result);

cleanup:
    free(text);
    free(path);
    return operation;
}

SolarResult solar_report_history_read_text(
    const SolarProject *project,
    const char *build_id,
    char **report_text_out
)
{
    SolarStoredBuildReport report;
    SolarResult result;

    if (report_text_out == NULL) return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "cannot read a stored report without output storage",
        "provide a destination for the report text");
    *report_text_out = NULL;
    solar_stored_build_report_init(&report);
    result = solar_report_history_load(project, build_id, &report);
    if (result.status == SOLAR_STATUS_OK) result = read_regular_text(
        report.report_path, report_text_out);
    solar_stored_build_report_free(&report);
    return result;
}

static int report_name_order(const void *left, const void *right)
{
    const SolarBuildReportInfo *left_item = left;
    const SolarBuildReportInfo *right_item = right;
    uint64_t left_number = 0U;
    uint64_t right_number = 0U;

    (void)parse_uint64_value(left_item->build_id + 6U, &left_number);
    (void)parse_uint64_value(right_item->build_id + 6U, &right_number);
    if (left_number < right_number) return 1;
    if (left_number > right_number) return -1;
    return 0;
}

static SolarResult append_report_info(
    SolarBuildReportList *list,
    const char *build_id,
    const SolarStoredBuildReport *report,
    bool corrupt
)
{
    SolarBuildReportInfo *items = realloc(
        list->items, (list->count + 1U) * sizeof(*items));
    SolarBuildReportInfo *item;
    SolarResult result;

    if (items == NULL) return solar_result_error(SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate build report listing", "free memory and try again");
    list->items = items;
    item = &items[list->count];
    (void)memset(item, 0, sizeof(*item));
    result = copy_optional(&item->build_id, build_id);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(&item->timestamp,
        report == NULL ? NULL : report->metadata.timestamp);
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(&item->status,
        corrupt ? "corrupt" : (report == NULL ? "unknown" :
            report->metadata.status));
    if (result.status == SOLAR_STATUS_OK) result = copy_optional(&item->top,
        report == NULL ? NULL : report->metadata.top);
    if (result.status != SOLAR_STATUS_OK) return result;
    if (report != NULL) {
        size_t present = 0U;
        const SolarSimulationTimings *timings = &report->timings;

        item->gss_available = report->has_synthesis_statistics &&
            report->synthesis_statistics.available;
        present += timings->simulation_compile.present ? 1U : 0U;
        present += timings->simulation_elaboration.present ? 1U : 0U;
        present += timings->simulation_execution.present ? 1U : 0U;
        present += timings->simulation_total.present ? 1U : 0U;
        if (!report->has_timings || present == 0U) {
            item->timings_availability = SOLAR_TIMINGS_UNAVAILABLE;
        } else if (timings->simulation_status_present &&
                   !timings->simulation_succeeded) {
            item->timings_availability = SOLAR_TIMINGS_PARTIAL;
        } else {
            item->timings_availability =
                timings->simulation_compile.present &&
                timings->simulation_execution.present &&
                timings->simulation_total.present
                    ? SOLAR_TIMINGS_AVAILABLE : SOLAR_TIMINGS_PARTIAL;
        }
    }
    list->count++;
    return solar_result_ok();
}

SolarResult solar_report_history_list(
    const SolarProject *project,
    SolarBuildReportList *result
)
{
    char *reports_path = NULL;
    DIR *directory = NULL;
    struct dirent *entry;
    SolarResult operation;

    if (project == NULL || project->root == NULL || result == NULL) return
        solar_result_error(SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot list history without a project and output storage",
            "load the current project first");
    solar_build_report_list_free(result);
    operation = solar_filesystem_join(project->root, ".solar/reports", &reports_path);
    if (operation.status != SOLAR_STATUS_OK) goto cleanup;
    directory = opendir(reports_path);
    if (directory == NULL && errno == ENOENT) {
        operation = solar_result_ok();
        goto cleanup;
    }
    if (directory == NULL) {
        operation = history_error(SOLAR_STATUS_IO_ERROR,
            "cannot open the build report history");
        goto cleanup;
    }
    while ((entry = readdir(directory)) != NULL) {
        SolarStoredBuildReport report;
        SolarResult loaded;

        if (!valid_build_id(entry->d_name)) continue;
        solar_stored_build_report_init(&report);
        loaded = solar_report_history_load(project, entry->d_name, &report);
        operation = append_report_info(result, entry->d_name,
            loaded.status == SOLAR_STATUS_OK ? &report : NULL,
            loaded.status != SOLAR_STATUS_OK);
        solar_stored_build_report_free(&report);
        if (operation.status != SOLAR_STATUS_OK) break;
    }
    if (operation.status == SOLAR_STATUS_OK && result->count > 1U) qsort(
        result->items, result->count, sizeof(*result->items), report_name_order);

cleanup:
    if (directory != NULL) (void)closedir(directory);
    free(reports_path);
    if (operation.status != SOLAR_STATUS_OK) solar_build_report_list_free(result);
    return operation;
}

SolarResult solar_report_history_find_previous_comparable(
    const SolarProject *project,
    const SolarStoredBuildReport *current,
    SolarStoredBuildReport *baseline
)
{
    SolarBuildReportList list;
    SolarResult result;
    size_t index;
    bool found_current = false;

    if (current == NULL || current->metadata.build_id == NULL || baseline == NULL) {
        return solar_result_error(SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot select a baseline without the current build",
            "load the latest stored build first");
    }
    if ((!current->has_synthesis_statistics ||
         !current->synthesis_statistics.available) && !current->has_timings) {
        return history_error(SOLAR_STATUS_NOT_FOUND,
            "the current build does not contain comparable synthesis or simulation data");
    }
    solar_build_report_list_init(&list);
    result = solar_report_history_list(project, &list);
    for (index = 0U; result.status == SOLAR_STATUS_OK && index < list.count; index++) {
        SolarStoredBuildReport candidate;
        SolarBuildReportComparison comparison;

        if (!found_current) {
            found_current = strcmp(list.items[index].build_id,
                current->metadata.build_id) == 0;
            continue;
        }
        solar_stored_build_report_init(&candidate);
        solar_build_report_comparison_init(&comparison);
        result = solar_report_history_load(project, list.items[index].build_id,
            &candidate);
        if (result.status == SOLAR_STATUS_OK) result = solar_build_reports_compare(
            &candidate, current, &comparison);
        if (result.status == SOLAR_STATUS_OK &&
            (comparison.synthesis_comparison_available ||
             comparison.simulation_comparison_available)) {
            solar_stored_build_report_free(baseline);
            *baseline = candidate;
            solar_stored_build_report_init(&candidate);
            solar_build_report_comparison_free(&comparison);
            solar_stored_build_report_free(&candidate);
            solar_build_report_list_free(&list);
            return solar_result_ok();
        }
        solar_build_report_comparison_free(&comparison);
        solar_stored_build_report_free(&candidate);
        result = solar_result_ok();
    }
    solar_build_report_list_free(&list);
    return history_error(SOLAR_STATUS_NOT_FOUND,
        "no previous comparable build report was found");
}
