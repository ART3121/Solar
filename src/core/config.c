#include "solar/config.h"

#include "cmm_source_internal.h"

#include "solar/backend.h"
#include "solar/filesystem.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SOLAR_MANIFEST_SIZE_LIMIT (16U * 1024U * 1024U)
#define SOLAR_SAFE_NAME_LENGTH_LIMIT 127U

typedef enum {
    SECTION_NONE = 0,
    SECTION_SOLAR,
    SECTION_PROJECT,
    SECTION_SOURCES,
    SECTION_SIMULATION,
    SECTION_SYNTHESIS,
    SECTION_COMPILER,
    SECTION_YANC,
    SECTION_PROFILE,
    SECTION_TEST,
    SECTION_COUNT
} ConfigSection;

typedef enum {
    VALUE_OK = 0,
    VALUE_SYNTAX,
    VALUE_MEMORY
} ValueStatus;

enum {
    SEEN_SOLAR_FORMAT = UINT64_C(1) << 0,
    SEEN_PROJECT_NAME = UINT64_C(1) << 1,
    SEEN_PROJECT_TOP = UINT64_C(1) << 2,
    SEEN_PROJECT_LANGUAGE = UINT64_C(1) << 3,
    SEEN_PROJECT_DEFAULT_TEST = UINT64_C(1) << 4,
    SEEN_PROJECT_DEFAULT_PROFILE = UINT64_C(1) << 5,
    SEEN_SOURCES_RTL = UINT64_C(1) << 6,
    SEEN_SOURCES_TESTBENCH = UINT64_C(1) << 7,
    SEEN_SOURCES_INCLUDE_DIRS = UINT64_C(1) << 8,
    SEEN_SOURCES_DEFINES = UINT64_C(1) << 9,
    SEEN_SIMULATION_BACKEND = UINT64_C(1) << 10,
    SEEN_SIMULATION_TOP = UINT64_C(1) << 11,
    SEEN_SIMULATION_WAVEFORM = UINT64_C(1) << 12,
    SEEN_SYNTHESIS_BACKEND = UINT64_C(1) << 13,
    SEEN_SYNTHESIS_TOP = UINT64_C(1) << 14,
    SEEN_COMPILER_BACKEND = UINT64_C(1) << 15,
    SEEN_COMPILER_SOURCE = UINT64_C(1) << 16,
    SEEN_COMPILER_PROCESSOR = UINT64_C(1) << 17,
    SEEN_COMPILER_FREQUENCY = UINT64_C(1) << 18,
    SEEN_COMPILER_CLOCKS = UINT64_C(1) << 19,
    SEEN_COMPILER_INCLUDE_DIRS = UINT64_C(1) << 20,
    SEEN_YANC_ROOT = UINT64_C(1) << 21,
    SEEN_YANC_DIAGNOSTICS = UINT64_C(1) << 22,
    SEEN_PROFILE_NAME = UINT64_C(1) << 0,
    SEEN_PROFILE_INCLUDE_DIRS = UINT64_C(1) << 1,
    SEEN_PROFILE_DEFINES = UINT64_C(1) << 2,
    SEEN_TEST_NAME = UINT64_C(1) << 0,
    SEEN_TEST_TOP = UINT64_C(1) << 1,
    SEEN_TEST_SOURCES = UINT64_C(1) << 2,
    SEEN_TEST_INCLUDE_DIRS = UINT64_C(1) << 3,
    SEEN_TEST_DEFINES = UINT64_C(1) << 4,
    SEEN_TEST_WAVEFORM = UINT64_C(1) << 5,
    SEEN_TEST_DRIVER = UINT64_C(1) << 6,
    SEEN_TEST_COCOTB = UINT64_C(1) << 7
};

typedef struct {
    ConfigSection section;
    uint64_t seen_values;
    uint64_t current_seen_values;
    unsigned int seen_sections;
    SolarProfile *profile;
    SolarTest *test;
} ParserState;

static SolarResult formatted_error(
    SolarStatus status,
    const char *path,
    size_t line_number,
    const char *hint,
    const char *format,
    ...
)
{
    SolarResult result;
    char detail[384];
    va_list arguments;

    result.status = status;
    va_start(arguments, format);
    (void)vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);

    if (line_number > 0U) {
        solar_diagnostic_format(
            &result.diagnostic,
            SOLAR_DIAGNOSTIC_ERROR,
            hint,
            "%s:%zu: %s",
            path,
            line_number,
            detail
        );
    } else {
        solar_diagnostic_format(
            &result.diagnostic,
            SOLAR_DIAGNOSTIC_ERROR,
            hint,
            "%s: %s",
            path,
            detail
        );
    }
    return result;
}

static char *trim(char *text)
{
    char *end;

    while (isspace((unsigned char)*text) != 0) {
        text++;
    }
    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1U;
    while (end > text && isspace((unsigned char)*end) != 0) {
        *end = '\0';
        end--;
    }
    return text;
}

static void strip_comment(char *line)
{
    bool in_string = false;
    bool escaped = false;
    char *cursor;

    for (cursor = line; *cursor != '\0'; cursor++) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_string && *cursor == '\\') {
            escaped = true;
            continue;
        }
        if (*cursor == '"') {
            in_string = !in_string;
            continue;
        }
        if (!in_string && *cursor == '#') {
            *cursor = '\0';
            return;
        }
    }
}

static void skip_spaces(const char **cursor)
{
    while (isspace((unsigned char)**cursor) != 0) {
        (*cursor)++;
    }
}

static ValueStatus parse_string_token(const char **cursor, char **value_out)
{
    const char *input = *cursor;
    char *value;
    size_t output_index = 0U;
    bool closed = false;

    *value_out = NULL;
    skip_spaces(&input);
    if (*input != '"') {
        return VALUE_SYNTAX;
    }
    input++;

    value = malloc(strlen(input) + 1U);
    if (value == NULL) {
        return VALUE_MEMORY;
    }

    while (*input != '\0') {
        char character = *input++;

        if (character == '"') {
            closed = true;
            break;
        }
        if (character == '\\') {
            character = *input++;
            if (character == 'n') {
                character = '\n';
            } else if (character == 't') {
                character = '\t';
            } else if (character != '\\' && character != '"') {
                free(value);
                return VALUE_SYNTAX;
            }
        }
        if (character == '\0' || character == '\n' || character == '\r') {
            free(value);
            return VALUE_SYNTAX;
        }
        value[output_index++] = character;
    }

    if (!closed) {
        free(value);
        return VALUE_SYNTAX;
    }
    value[output_index] = '\0';
    *cursor = input;
    *value_out = value;
    return VALUE_OK;
}

static ValueStatus parse_string(const char *input, char **value_out)
{
    ValueStatus status;

    status = parse_string_token(&input, value_out);
    if (status != VALUE_OK) {
        return status;
    }
    skip_spaces(&input);
    if (*input != '\0') {
        free(*value_out);
        *value_out = NULL;
        return VALUE_SYNTAX;
    }
    return VALUE_OK;
}

static ValueStatus string_list_append(SolarStringList *list, char *value)
{
    char **items;

    if (list->count == SIZE_MAX / sizeof(*items)) {
        return VALUE_MEMORY;
    }
    items = realloc(list->items, (list->count + 1U) * sizeof(*items));
    if (items == NULL) {
        return VALUE_MEMORY;
    }

    list->items = items;
    list->items[list->count] = value;
    list->count++;
    return VALUE_OK;
}

static ValueStatus parse_string_list(const char *input, SolarStringList *list)
{
    ValueStatus status = VALUE_SYNTAX;
    char *value = NULL;

    skip_spaces(&input);
    if (*input++ != '[') {
        return VALUE_SYNTAX;
    }
    skip_spaces(&input);
    if (*input == ']') {
        input++;
        skip_spaces(&input);
        return *input == '\0' ? VALUE_OK : VALUE_SYNTAX;
    }

    while (*input != '\0') {
        status = parse_string_token(&input, &value);
        if (status != VALUE_OK) {
            goto cleanup;
        }
        status = string_list_append(list, value);
        if (status != VALUE_OK) {
            goto cleanup;
        }
        value = NULL;

        skip_spaces(&input);
        if (*input == ']') {
            input++;
            skip_spaces(&input);
            status = *input == '\0' ? VALUE_OK : VALUE_SYNTAX;
            goto cleanup;
        }
        if (*input++ != ',') {
            status = VALUE_SYNTAX;
            goto cleanup;
        }
        skip_spaces(&input);
        if (*input == ']') {
            status = VALUE_SYNTAX;
            goto cleanup;
        }
    }

cleanup:
    free(value);
    if (status != VALUE_OK) {
        solar_string_list_free(list);
    }
    return status;
}

static SolarResult value_error(
    ValueStatus status,
    const char *path,
    size_t line_number,
    const char *section,
    const char *key
)
{
    if (status == VALUE_MEMORY) {
        return formatted_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            path,
            line_number,
            "free memory and try again",
            "could not allocate [%s].%s",
            section,
            key
        );
    }
    return formatted_error(
        SOLAR_STATUS_CONFIG_ERROR,
        path,
        line_number,
        "use a quoted string or a one-line array of quoted strings",
        "invalid value for [%s].%s",
        section,
        key
    );
}

static SolarResult store_string(
    ParserState *state,
    uint64_t flag,
    char **destination,
    const char *value,
    const char *path,
    size_t line_number,
    const char *section,
    const char *key
)
{
    ValueStatus status;
    uint64_t *seen = state->section == SECTION_PROFILE ||
        state->section == SECTION_TEST
        ? &state->current_seen_values
        : &state->seen_values;

    if ((*seen & flag) != 0U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR,
            path,
            line_number,
            "remove the duplicate assignment",
            "[%s].%s is assigned more than once",
            section,
            key
        );
    }
    status = parse_string(value, destination);
    if (status != VALUE_OK) {
        return value_error(status, path, line_number, section, key);
    }
    *seen |= flag;
    return solar_result_ok();
}

static SolarResult store_list(
    ParserState *state,
    uint64_t flag,
    SolarStringList *destination,
    const char *value,
    const char *path,
    size_t line_number,
    const char *section,
    const char *key
)
{
    ValueStatus status;
    uint64_t *seen = state->section == SECTION_PROFILE ||
        state->section == SECTION_TEST
        ? &state->current_seen_values
        : &state->seen_values;

    if ((*seen & flag) != 0U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR,
            path,
            line_number,
            "remove the duplicate assignment",
            "[%s].%s is assigned more than once",
            section,
            key
        );
    }
    status = parse_string_list(value, destination);
    if (status != VALUE_OK) {
        return value_error(status, path, line_number, section, key);
    }
    *seen |= flag;
    return solar_result_ok();
}

static SolarResult unknown_key(
    const char *path,
    size_t line_number,
    const char *section,
    const char *key
)
{
    return formatted_error(
        SOLAR_STATUS_CONFIG_ERROR,
        path,
        line_number,
        "remove the key or consult docs/project-format.md",
        "unknown key [%s].%s",
        section,
        key
    );
}

static SolarResult store_uint64(
    ParserState *state,
    uint64_t flag,
    uint64_t *destination,
    const char *value,
    const char *path,
    size_t line_number,
    const char *section,
    const char *key
)
{
    char *end = NULL;
    unsigned long long parsed;

    if ((state->seen_values & flag) != 0U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "remove the duplicate assignment",
            "[%s].%s is assigned more than once", section, key
        );
    }
    if (value[0] == '\0' || value[0] == '-' || value[0] == '+') {
        return value_error(VALUE_SYNTAX, path, line_number, section, key);
    }
    errno = 0;
    parsed = strtoull(value, &end, 10);
    while (end != NULL && isspace((unsigned char)*end) != 0) end++;
    if (errno == ERANGE || end == value || end == NULL || *end != '\0') {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "use a positive decimal integer without quotes",
            "invalid integer for [%s].%s", section, key
        );
    }
    *destination = (uint64_t)parsed;
    state->seen_values |= flag;
    return solar_result_ok();
}

static SolarResult parse_solar_value(
    ParserState *state,
    SolarConfig *config,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    uint64_t parsed = 0U;
    SolarResult result;

    if (strcmp(key, "format") != 0) {
        return unknown_key(path, line_number, "solar", key);
    }
    result = store_uint64(
        state, SEEN_SOLAR_FORMAT, &parsed, value, path, line_number,
        "solar", key
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    if (parsed > (uint64_t)UINT32_MAX) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "set [solar].format = 2",
            "project format is out of range"
        );
    }
    if (parsed == 0U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "set [solar].format = 2, or omit [solar].format for a legacy project",
            "unsupported Solar project format: 0"
        );
    }
    config->manifest_format = (unsigned int)parsed;
    return solar_result_ok();
}

static SolarResult parse_project_value(
    ParserState *state,
    SolarConfig *config,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (strcmp(key, "name") == 0) {
        return store_string(state, SEEN_PROJECT_NAME, &config->project.name,
                            value, path, line_number, "project", key);
    }
    if (strcmp(key, "top") == 0) {
        return store_string(state, SEEN_PROJECT_TOP, &config->project.top,
                            value, path, line_number, "project", key);
    }
    if (strcmp(key, "language") == 0) {
        return store_string(state, SEEN_PROJECT_LANGUAGE, &config->project.language,
                            value, path, line_number, "project", key);
    }
    if (strcmp(key, "default_test") == 0) {
        return store_string(
            state, SEEN_PROJECT_DEFAULT_TEST, &config->project.default_test,
            value, path, line_number, "project", key
        );
    }
    if (strcmp(key, "default_profile") == 0) {
        return store_string(
            state, SEEN_PROJECT_DEFAULT_PROFILE, &config->project.default_profile,
            value, path, line_number, "project", key
        );
    }
    return unknown_key(path, line_number, "project", key);
}

static SolarResult parse_sources_value(
    ParserState *state,
    SolarConfig *config,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (strcmp(key, "rtl") == 0) {
        return store_list(state, SEEN_SOURCES_RTL, &config->sources.rtl,
                          value, path, line_number, "sources", key);
    }
    if (strcmp(key, "testbench") == 0) {
        return store_list(state, SEEN_SOURCES_TESTBENCH, &config->sources.testbench,
                          value, path, line_number, "sources", key);
    }
    if (strcmp(key, "include_dirs") == 0) {
        return store_list(
            state, SEEN_SOURCES_INCLUDE_DIRS, &config->sources.include_dirs,
            value, path, line_number, "sources", key
        );
    }
    if (strcmp(key, "defines") == 0) {
        return store_list(
            state, SEEN_SOURCES_DEFINES, &config->sources.defines,
            value, path, line_number, "sources", key
        );
    }
    return unknown_key(path, line_number, "sources", key);
}

static SolarResult parse_simulation_value(
    ParserState *state,
    SolarConfig *config,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (strcmp(key, "backend") == 0) {
        return store_string(state, SEEN_SIMULATION_BACKEND,
                            &config->simulation.backend, value, path,
                            line_number, "simulation", key);
    }
    if (strcmp(key, "top") == 0) {
        return store_string(state, SEEN_SIMULATION_TOP, &config->simulation.top,
                            value, path, line_number, "simulation", key);
    }
    if (strcmp(key, "waveform") == 0) {
        return store_string(state, SEEN_SIMULATION_WAVEFORM,
                            &config->simulation.waveform, value, path,
                            line_number, "simulation", key);
    }
    return unknown_key(path, line_number, "simulation", key);
}

static SolarResult parse_synthesis_value(
    ParserState *state,
    SolarConfig *config,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (strcmp(key, "backend") == 0) {
        return store_string(state, SEEN_SYNTHESIS_BACKEND,
                            &config->synthesis.backend, value, path,
                            line_number, "synthesis", key);
    }
    if (strcmp(key, "top") == 0) {
        return store_string(state, SEEN_SYNTHESIS_TOP, &config->synthesis.top,
                            value, path, line_number, "synthesis", key);
    }
    return unknown_key(path, line_number, "synthesis", key);
}

static SolarResult parse_compiler_value(
    ParserState *state,
    SolarConfig *config,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (strcmp(key, "backend") == 0) {
        return store_string(
            state, SEEN_COMPILER_BACKEND, &config->compiler.backend,
            value, path, line_number, "compiler", key
        );
    }
    if (strcmp(key, "source") == 0) {
        return store_string(
            state, SEEN_COMPILER_SOURCE, &config->compiler.source,
            value, path, line_number, "compiler", key
        );
    }
    if (strcmp(key, "processor") == 0) {
        return store_string(
            state, SEEN_COMPILER_PROCESSOR, &config->compiler.processor,
            value, path, line_number, "compiler", key
        );
    }
    if (strcmp(key, "frequency_mhz") == 0) {
        return store_uint64(
            state, SEEN_COMPILER_FREQUENCY, &config->compiler.frequency_mhz,
            value, path, line_number, "compiler", key
        );
    }
    if (strcmp(key, "simulation_clocks") == 0) {
        return store_uint64(
            state, SEEN_COMPILER_CLOCKS, &config->compiler.simulation_clocks,
            value, path, line_number, "compiler", key
        );
    }
    if (strcmp(key, "include_dirs") == 0) {
        return store_list(
            state, SEEN_COMPILER_INCLUDE_DIRS, &config->compiler.include_dirs,
            value, path, line_number, "compiler", key
        );
    }
    return unknown_key(path, line_number, "compiler", key);
}

static SolarResult parse_yanc_value(
    ParserState *state,
    SolarConfig *config,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (strcmp(key, "root") == 0) {
        return store_string(
            state, SEEN_YANC_ROOT, &config->yanc.root,
            value, path, line_number, "yanc", key
        );
    }
    if (strcmp(key, "diagnostics") == 0) {
        return store_string(
            state, SEEN_YANC_DIAGNOSTICS, &config->yanc.diagnostics,
            value, path, line_number, "yanc", key
        );
    }
    return unknown_key(path, line_number, "yanc", key);
}

static SolarResult parse_profile_value(
    ParserState *state,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (state->profile == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "profile parser has no current profile",
            "report this Solar bug"
        );
    }
    if (strcmp(key, "name") == 0) {
        return store_string(
            state, SEEN_PROFILE_NAME, &state->profile->name,
            value, path, line_number, "profile", key
        );
    }
    if (strcmp(key, "include_dirs") == 0) {
        return store_list(
            state, SEEN_PROFILE_INCLUDE_DIRS, &state->profile->include_dirs,
            value, path, line_number, "profile", key
        );
    }
    if (strcmp(key, "defines") == 0) {
        return store_list(
            state, SEEN_PROFILE_DEFINES, &state->profile->defines,
            value, path, line_number, "profile", key
        );
    }
    return unknown_key(path, line_number, "profile", key);
}

static SolarResult parse_test_value(
    ParserState *state,
    const char *key,
    const char *value,
    const char *path,
    size_t line_number
)
{
    if (state->test == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "test parser has no current test",
            "report this Solar bug"
        );
    }
    if (strcmp(key, "name") == 0) {
        return store_string(
            state, SEEN_TEST_NAME, &state->test->name,
            value, path, line_number, "test", key
        );
    }
    if (strcmp(key, "top") == 0) {
        return store_string(
            state, SEEN_TEST_TOP, &state->test->top,
            value, path, line_number, "test", key
        );
    }
    if (strcmp(key, "sources") == 0) {
        return store_list(
            state, SEEN_TEST_SOURCES, &state->test->sources,
            value, path, line_number, "test", key
        );
    }
    if (strcmp(key, "include_dirs") == 0) {
        return store_list(
            state, SEEN_TEST_INCLUDE_DIRS, &state->test->include_dirs,
            value, path, line_number, "test", key
        );
    }
    if (strcmp(key, "defines") == 0) {
        return store_list(
            state, SEEN_TEST_DEFINES, &state->test->defines,
            value, path, line_number, "test", key
        );
    }
    if (strcmp(key, "waveform") == 0) {
        return store_string(
            state, SEEN_TEST_WAVEFORM, &state->test->waveform,
            value, path, line_number, "test", key
        );
    }
    if (strcmp(key, "driver") == 0) {
        return store_string(
            state, SEEN_TEST_DRIVER, &state->test->driver,
            value, path, line_number, "test", key
        );
    }
    if (strcmp(key, "cocotb") == 0) {
        return store_string(
            state, SEEN_TEST_COCOTB, &state->test->cocotb,
            value, path, line_number, "test", key
        );
    }
    return unknown_key(path, line_number, "test", key);
}

static SolarResult parse_assignment(
    ParserState *state,
    SolarConfig *config,
    char *line,
    const char *path,
    size_t line_number
)
{
    char *equals = strchr(line, '=');
    char *key;
    char *value;

    if (equals == NULL || state->section == SECTION_NONE) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "place key = value assignments under a documented section",
            "expected an assignment inside a section"
        );
    }
    *equals = '\0';
    key = trim(line);
    value = trim(equals + 1);
    if (key[0] == '\0' || value[0] == '\0') {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "use key = value with a non-empty value",
            "incomplete assignment"
        );
    }

    switch (state->section) {
    case SECTION_SOLAR:
        return parse_solar_value(state, config, key, value, path, line_number);
    case SECTION_PROJECT:
        return parse_project_value(state, config, key, value, path, line_number);
    case SECTION_SOURCES:
        return parse_sources_value(state, config, key, value, path, line_number);
    case SECTION_SIMULATION:
        return parse_simulation_value(state, config, key, value, path, line_number);
    case SECTION_SYNTHESIS:
        return parse_synthesis_value(state, config, key, value, path, line_number);
    case SECTION_COMPILER:
        return parse_compiler_value(state, config, key, value, path, line_number);
    case SECTION_YANC:
        return parse_yanc_value(state, config, key, value, path, line_number);
    case SECTION_PROFILE:
        return parse_profile_value(state, key, value, path, line_number);
    case SECTION_TEST:
        return parse_test_value(state, key, value, path, line_number);
    case SECTION_NONE:
    case SECTION_COUNT:
        break;
    }
    return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "manifest parser reached an invalid section",
        "report this Solar bug"
    );
}

static ConfigSection section_from_name(const char *name)
{
    if (strcmp(name, "solar") == 0) {
        return SECTION_SOLAR;
    }
    if (strcmp(name, "project") == 0) {
        return SECTION_PROJECT;
    }
    if (strcmp(name, "sources") == 0) {
        return SECTION_SOURCES;
    }
    if (strcmp(name, "simulation") == 0) {
        return SECTION_SIMULATION;
    }
    if (strcmp(name, "synthesis") == 0) {
        return SECTION_SYNTHESIS;
    }
    if (strcmp(name, "compiler") == 0) {
        return SECTION_COMPILER;
    }
    if (strcmp(name, "yanc") == 0) {
        return SECTION_YANC;
    }
    return SECTION_NONE;
}

static const char *section_name(ConfigSection section)
{
    switch (section) {
    case SECTION_SOLAR: return "solar";
    case SECTION_PROJECT: return "project";
    case SECTION_SOURCES: return "sources";
    case SECTION_SIMULATION: return "simulation";
    case SECTION_SYNTHESIS: return "synthesis";
    case SECTION_COMPILER: return "compiler";
    case SECTION_YANC: return "yanc";
    case SECTION_PROFILE: return "profile";
    case SECTION_TEST: return "test";
    case SECTION_NONE:
    case SECTION_COUNT:
        return "manifest";
    }
    return "manifest";
}

static SolarResult append_profile(
    SolarConfig *config,
    SolarProfile **profile_out
)
{
    SolarProfile *profiles;

    *profile_out = NULL;
    if (config->profile_count == SIZE_MAX / sizeof(*profiles)) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "too many profiles to represent",
            "reduce the number of [[profile]] tables"
        );
    }
    profiles = realloc(
        config->profiles,
        (config->profile_count + 1U) * sizeof(*profiles)
    );
    if (profiles == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a profile",
            "free memory and try again"
        );
    }
    config->profiles = profiles;
    *profile_out = &config->profiles[config->profile_count++];
    (void)memset(*profile_out, 0, sizeof(**profile_out));
    return solar_result_ok();
}

static SolarResult append_test(
    SolarConfig *config,
    SolarTest **test_out
)
{
    SolarTest *tests;

    *test_out = NULL;
    if (config->test_count == SIZE_MAX / sizeof(*tests)) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "too many tests to represent",
            "reduce the number of [[test]] tables"
        );
    }
    tests = realloc(config->tests, (config->test_count + 1U) * sizeof(*tests));
    if (tests == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a test",
            "free memory and try again"
        );
    }
    config->tests = tests;
    *test_out = &config->tests[config->test_count++];
    (void)memset(*test_out, 0, sizeof(**test_out));
    (*test_out)->kind = SOLAR_TEST_VERILOG;
    return solar_result_ok();
}

static SolarResult parse_repeated_section(
    ParserState *state,
    SolarConfig *config,
    char *name,
    const char *path,
    size_t line_number
)
{
    SolarResult result;

    if (strcmp(name, "profile") == 0) {
        result = append_profile(config, &state->profile);
        if (result.status == SOLAR_STATUS_OK) {
            state->test = NULL;
            state->section = SECTION_PROFILE;
            state->current_seen_values = 0U;
        }
        return result;
    }
    if (strcmp(name, "test") == 0) {
        result = append_test(config, &state->test);
        if (result.status == SOLAR_STATUS_OK) {
            state->profile = NULL;
            state->section = SECTION_TEST;
            state->current_seen_values = 0U;
        }
        return result;
    }
    return formatted_error(
        SOLAR_STATUS_CONFIG_ERROR, path, line_number,
        "use [[profile]] or [[test]] for repeated tables",
        "unknown repeated section [[%s]]", name
    );
}

static SolarResult parse_section(
    ParserState *state,
    SolarConfig *config,
    char *line,
    const char *path,
    size_t line_number
)
{
    size_t length = strlen(line);
    ConfigSection section;
    unsigned int flag;
    bool repeated = length >= 4U && line[0] == '[' && line[1] == '[';

    if (length < 3U || line[length - 1U] != ']' ||
        (repeated && (length < 5U || line[length - 2U] != ']'))) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "use a documented [section], [[profile]], or [[test]] header",
            "invalid section header"
        );
    }
    if (repeated) {
        line[length - 2U] = '\0';
        return parse_repeated_section(
            state, config, trim(line + 2), path, line_number
        );
    }
    line[length - 1U] = '\0';
    section = section_from_name(trim(line + 1));
    if (section == SECTION_NONE) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "use a section documented in docs/project-format.md",
            "unknown section [%s]",
            trim(line + 1)
        );
    }

    flag = 1U << (unsigned int)section;
    if ((state->seen_sections & flag) != 0U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, path, line_number,
            "merge assignments into one section",
            "section [%s] is declared more than once",
            trim(line + 1)
        );
    }
    state->seen_sections |= flag;
    state->section = section;
    state->profile = NULL;
    state->test = NULL;
    return solar_result_ok();
}

void solar_config_init(SolarConfig *config)
{
    if (config != NULL) {
        (void)memset(config, 0, sizeof(*config));
    }
}

static void profile_free(SolarProfile *profile)
{
    free(profile->name);
    solar_string_list_free(&profile->include_dirs);
    solar_string_list_free(&profile->defines);
    (void)memset(profile, 0, sizeof(*profile));
}

static void test_free(SolarTest *test)
{
    free(test->name);
    free(test->top);
    solar_string_list_free(&test->sources);
    solar_string_list_free(&test->include_dirs);
    solar_string_list_free(&test->defines);
    free(test->waveform);
    free(test->driver);
    free(test->cocotb);
    (void)memset(test, 0, sizeof(*test));
}

void solar_config_free(SolarConfig *config)
{
    if (config == NULL) {
        return;
    }

    free(config->project.name);
    free(config->project.top);
    free(config->project.language);
    free(config->project.default_test);
    free(config->project.default_profile);
    solar_string_list_free(&config->sources.rtl);
    solar_string_list_free(&config->sources.testbench);
    solar_string_list_free(&config->sources.include_dirs);
    solar_string_list_free(&config->sources.defines);
    free(config->simulation.backend);
    free(config->simulation.top);
    free(config->simulation.waveform);
    free(config->synthesis.backend);
    free(config->synthesis.top);
    free(config->compiler.backend);
    free(config->compiler.source);
    free(config->compiler.processor);
    solar_string_list_free(&config->compiler.include_dirs);
    free(config->yanc.root);
    free(config->yanc.diagnostics);
    for (size_t index = 0U; index < config->profile_count; index++) {
        profile_free(&config->profiles[index]);
    }
    free(config->profiles);
    for (size_t index = 0U; index < config->test_count; index++) {
        test_free(&config->tests[index]);
    }
    free(config->tests);
    solar_config_init(config);
}

static SolarResult copy_string(char **destination, const char *source)
{
    if (source == NULL) return solar_result_ok();
    *destination = strdup(source);
    if (*destination == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not normalize a manifest string",
            "free memory and try again"
        );
    }
    return solar_result_ok();
}

static SolarResult copy_list(
    SolarStringList *destination,
    const SolarStringList *source
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

static bool has_v2_only_values(const SolarConfig *config)
{
    return config->project.default_test != NULL ||
        config->project.default_profile != NULL ||
        config->sources.include_dirs.count > 0U ||
        config->sources.defines.count > 0U ||
        config->compiler.backend != NULL || config->compiler.source != NULL ||
        config->compiler.processor != NULL || config->yanc.root != NULL ||
        config->yanc.diagnostics != NULL || config->profile_count > 0U ||
        config->test_count > 0U;
}

static SolarResult normalize_v1(
    SolarConfig *config,
    const char *manifest_path
)
{
    SolarTest *test = NULL;
    SolarResult result;

    if (has_v2_only_values(config)) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "add [solar] with format = 2 before using v2 fields",
            "manifest has format-2 values but no [solar].format"
        );
    }
    config->manifest_format = 1U;
    result = append_test(config, &test);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_string(&test->name, "default");
    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_string(&test->top, config->simulation.top);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_string(&test->waveform, config->simulation.waveform);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = copy_list(&test->sources, &config->sources.testbench);
    if (result.status != SOLAR_STATUS_OK) return result;
    return copy_string(&config->project.default_test, "default");
}

static bool is_yanc_language(const char *language)
{
    return language != NULL &&
        (strcmp(language, "cmm") == 0 || strcmp(language, "cpp") == 0 ||
         strcmp(language, "asm") == 0);
}

static SolarResult derived_string(
    char **output,
    const char *base,
    const char *suffix
)
{
    size_t base_length = strlen(base);
    size_t suffix_length = strlen(suffix);
    char *value;

    if (base_length > SIZE_MAX - suffix_length - 1U) {
        return solar_result_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "generated manifest value is too long",
            "use a shorter processor or test name"
        );
    }
    value = malloc(base_length + suffix_length + 1U);

    if (value == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not derive a generated test value",
            "free memory and try again"
        );
    }
    (void)memcpy(value, base, base_length);
    (void)memcpy(value + base_length, suffix, suffix_length + 1U);
    *output = value;
    return solar_result_ok();
}

static SolarResult add_generated_test(SolarConfig *config)
{
    SolarTest *test = NULL;
    SolarResult result = append_test(config, &test);

    if (result.status != SOLAR_STATUS_OK) return result;
    test->kind = SOLAR_TEST_GENERATED;
    result = copy_string(&test->name, "generated");
    if (result.status != SOLAR_STATUS_OK) return result;
    result = derived_string(&test->top, config->compiler.processor, "_tb");
    if (result.status != SOLAR_STATUS_OK) return result;
    return derived_string(
        &test->waveform, config->compiler.processor, "_tb.vcd"
    );
}

static SolarResult postprocess_config(
    SolarConfig *config,
    const char *manifest_path
)
{
    SolarResult result;

    if (config->manifest_format == 0U) {
        return normalize_v1(config, manifest_path);
    }
    if (config->manifest_format != 2U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "set [solar].format = 2, or omit [solar] for a legacy format-1 project",
            "unsupported Solar project format: %u", config->manifest_format
        );
    }
    if (config->project.top != NULL || config->sources.testbench.count > 0U ||
        config->simulation.top != NULL || config->simulation.waveform != NULL) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "move testbench, top, and waveform values into [[test]] tables",
            "format 2 cannot use format-1 testbench fields"
        );
    }
    if (is_yanc_language(config->project.language) &&
        config->yanc.diagnostics == NULL) {
        result = copy_string(&config->yanc.diagnostics, "pt");
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    if (is_yanc_language(config->project.language) &&
        config->compiler.processor != NULL && config->test_count == 0U) {
        result = add_generated_test(config);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    if (is_yanc_language(config->project.language) &&
        config->synthesis.top == NULL && config->compiler.processor != NULL) {
        result = copy_string(
            &config->synthesis.top, config->compiler.processor
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    if (config->project.language != NULL &&
        strcmp(config->project.language, "verilog") == 0) {
        size_t index;

        for (index = 0U; index < config->test_count; index++) {
            if (config->tests[index].waveform == NULL &&
                config->tests[index].name != NULL) {
                result = derived_string(
                    &config->tests[index].waveform,
                    config->tests[index].name,
                    ".vcd"
                );
                if (result.status != SOLAR_STATUS_OK) return result;
            }
        }
    }
    return solar_result_ok();
}

static SolarResult append_statement_text(
    char **statement,
    const char *content
)
{
    size_t old_length = *statement == NULL ? 0U : strlen(*statement);
    size_t content_length = strlen(content);
    size_t separator_length = old_length == 0U ? 0U : 1U;
    char *expanded;

    if (old_length > SIZE_MAX - separator_length ||
        old_length + separator_length > SIZE_MAX - content_length - 1U) {
        return solar_result_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "manifest statement is too long",
            "split or reduce the statement"
        );
    }
    expanded = realloc(
        *statement, old_length + separator_length + content_length + 1U
    );

    if (expanded == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a manifest statement",
            "free memory and try again"
        );
    }
    *statement = expanded;
    if (separator_length != 0U) (*statement)[old_length++] = ' ';
    (void)memcpy(*statement + old_length, content, content_length + 1U);
    return solar_result_ok();
}

static int statement_bracket_depth(const char *statement, bool *open_string)
{
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    const char *cursor;

    if (open_string != NULL) *open_string = false;
    if (statement == NULL || open_string == NULL) return -1;
    for (cursor = statement; *cursor != '\0'; cursor++) {
        if (escaped) {
            escaped = false;
        } else if (in_string && *cursor == '\\') {
            escaped = true;
        } else if (*cursor == '"') {
            in_string = !in_string;
        } else if (!in_string && *cursor == '[') {
            depth++;
        } else if (!in_string && *cursor == ']') {
            depth--;
        }
    }
    *open_string = in_string;
    return depth;
}

static SolarResult parse_complete_statement(
    ParserState *state,
    SolarConfig *config,
    char *statement,
    const char *manifest_path,
    size_t line_number
)
{
    if (statement[0] == '[') {
        return parse_section(
            state, config, statement, manifest_path, line_number
        );
    }
    return parse_assignment(
        state, config, statement, manifest_path, line_number
    );
}

SolarResult solar_config_parse_file(const char *manifest_path, SolarConfig *config)
{
    FILE *manifest;
    struct stat information;
    char *line = NULL;
    char *statement = NULL;
    size_t capacity = 0U;
    size_t line_number = 0U;
    size_t statement_line = 0U;
    ParserState state = {0};
    SolarResult result = solar_result_ok();

    if (config != NULL) {
        solar_config_init(config);
    }
    if (manifest_path == NULL || config == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot parse a manifest without a path and output configuration",
            "provide a solar.toml path and initialized output storage"
        );
    }
    manifest = fopen(manifest_path, "r");
    if (manifest == NULL) {
        return formatted_error(
            errno == ENOENT ? SOLAR_STATUS_NOT_FOUND : SOLAR_STATUS_IO_ERROR,
            manifest_path, 0U, "ensure solar.toml exists and is readable",
            "cannot open manifest: %s", strerror(errno)
        );
    }
    if (fstat(fileno(manifest), &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        SolarResult inspection_result = formatted_error(
            SOLAR_STATUS_IO_ERROR, manifest_path, 0U,
            "use a readable regular file for solar.toml",
            "manifest is not a safe regular file"
        );

        (void)fclose(manifest);
        return inspection_result;
    }
    if (information.st_size > (off_t)SOLAR_MANIFEST_SIZE_LIMIT) {
        SolarResult size_result = formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "reduce solar.toml below 16 MiB",
            "manifest exceeds the 16 MiB size limit"
        );

        (void)fclose(manifest);
        return size_result;
    }

    while (getline(&line, &capacity, manifest) >= 0) {
        char *content;
        bool open_string = false;
        int depth;

        line_number++;
        strip_comment(line);
        content = trim(line);
        if (content[0] == '\0') {
            continue;
        }
        if (statement == NULL) statement_line = line_number;
        result = append_statement_text(&statement, content);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        depth = statement_bracket_depth(statement, &open_string);
        if (open_string) {
            result = formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, statement_line,
                "close quoted strings on the same line",
                "multiline strings are not supported"
            );
            goto cleanup;
        }
        if (depth > 0) continue;
        result = parse_complete_statement(
            &state, config, statement, manifest_path, statement_line
        );
        free(statement);
        statement = NULL;
        if (result.status != SOLAR_STATUS_OK) {
            goto cleanup;
        }
    }

    if (ferror(manifest) != 0) {
        result = formatted_error(
            SOLAR_STATUS_IO_ERROR, manifest_path, line_number,
            "check the file and storage device, then try again",
            "failed while reading manifest: %s", strerror(errno)
        );
    } else if (statement != NULL) {
        const char *key_start = statement;
        const char *equals = strchr(statement, '=');
        char key[64] = "value";

        while (isspace((unsigned char)*key_start) != 0) key_start++;
        if (equals != NULL && equals > key_start) {
            size_t key_length = (size_t)(equals - key_start);
            while (key_length > 0U &&
                   isspace((unsigned char)key_start[key_length - 1U]) != 0) {
                key_length--;
            }
            if (key_length >= sizeof(key)) key_length = sizeof(key) - 1U;
            (void)memcpy(key, key_start, key_length);
            key[key_length] = '\0';
        }
        result = formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, statement_line,
            "close the array with ]",
            "unterminated value for [%s].%s",
            section_name(state.section), key
        );
    } else {
        result = postprocess_config(config, manifest_path);
    }

cleanup:
    free(line);
    free(statement);
    if (fclose(manifest) != 0 && result.status == SOLAR_STATUS_OK) {
        result = formatted_error(
            SOLAR_STATUS_IO_ERROR, manifest_path, 0U,
            "check the file and storage device, then try again",
            "failed to close manifest: %s", strerror(errno)
        );
    }
    if (result.status != SOLAR_STATUS_OK) {
        solar_config_free(config);
    }
    return result;
}

static SolarResult missing_value(
    const char *path,
    const char *section,
    const char *key,
    const char *example
)
{
    char hint[SOLAR_DIAGNOSTIC_HINT_SIZE];

    (void)snprintf(hint, sizeof(hint), "add %s under [%s]", example, section);
    return formatted_error(
        SOLAR_STATUS_CONFIG_ERROR, path, 0U, hint,
        "[%s].%s is required", section, key
    );
}

static SolarResult validate_required_values(
    const SolarConfig *config,
    const char *path
)
{
    if (config->project.name == NULL || config->project.name[0] == '\0') {
        return missing_value(path, "project", "name", "name = \"counter\"");
    }
    if (config->project.language == NULL || config->project.language[0] == '\0') {
        return missing_value(path, "project", "language", "language = \"verilog\"");
    }
    if (config->simulation.backend == NULL || config->simulation.backend[0] == '\0') {
        return missing_value(path, "simulation", "backend", "backend = \"iverilog\"");
    }
    if (config->synthesis.backend == NULL || config->synthesis.backend[0] == '\0') {
        return missing_value(path, "synthesis", "backend", "backend = \"yosys\"");
    }
    if (config->manifest_format == 1U) {
        if (config->project.top == NULL || config->project.top[0] == '\0') {
            return missing_value(path, "project", "top", "top = \"counter\"");
        }
        if (config->sources.rtl.count == 0U) {
            return missing_value(path, "sources", "rtl", "rtl = [\"rtl/counter.v\"]");
        }
        if (config->sources.testbench.count == 0U) {
            return missing_value(path, "sources", "testbench",
                                 "testbench = [\"tb/counter_tb.v\"]");
        }
        if (config->simulation.top == NULL || config->simulation.top[0] == '\0') {
            return missing_value(path, "simulation", "top", "top = \"counter_tb\"");
        }
        if (config->simulation.waveform == NULL ||
            config->simulation.waveform[0] == '\0') {
            return missing_value(path, "simulation", "waveform",
                                 "waveform = \".solar/build/sim/counter.vcd\"");
        }
    }
    if (config->synthesis.top == NULL || config->synthesis.top[0] == '\0') {
        return missing_value(path, "synthesis", "top", "top = \"counter\"");
    }
    return solar_result_ok();
}

static SolarResult validate_relative_paths(
    const SolarStringList *list,
    const char *path,
    const char *section,
    const char *key
)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (!solar_filesystem_is_safe_relative(list->items[index])) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, path, 0U,
                "use a project-relative path without empty, '.' or '..' components",
                "[%s].%s[%zu] is not a safe relative path: %s",
                section, key, index, list->items[index]
            );
        }
        if (strcmp(list->items[index], ".solar") == 0 ||
            strncmp(list->items[index], ".solar/", 7U) == 0) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, path, 0U,
                "move authored sources outside .solar and update the manifest",
                "[%s].%s[%zu] uses the reserved generated-data directory: %s",
                section, key, index, list->items[index]
            );
        }
    }
    return solar_result_ok();
}

static bool is_simple_verilog_identifier(const char *identifier)
{
    const unsigned char *cursor = (const unsigned char *)identifier;

    if (!((*cursor >= 'A' && *cursor <= 'Z') ||
          (*cursor >= 'a' && *cursor <= 'z') || *cursor == '_')) {
        return false;
    }
    cursor++;
    while (*cursor != '\0') {
        if (!((*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') ||
              *cursor == '_' || *cursor == '$')) {
            return false;
        }
        cursor++;
    }
    return true;
}

static bool contains_control_character(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;

    while (*cursor != '\0') {
        if (*cursor < 32U || *cursor == 127U) return true;
        cursor++;
    }
    return false;
}

static SolarResult validate_top_identifiers(
    const SolarConfig *config,
    const char *manifest_path
)
{
    size_t index;

    if (!is_simple_verilog_identifier(config->synthesis.top)) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "use a simple Verilog identifier beginning with a letter or underscore",
            "[synthesis].top is not a supported Verilog identifier: %s",
            config->synthesis.top
        );
    }
    if (config->manifest_format == 1U &&
        (!is_simple_verilog_identifier(config->project.top) ||
         !is_simple_verilog_identifier(config->simulation.top))) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "use simple Verilog identifiers for legacy top values",
            "format-1 project contains an invalid top identifier"
        );
    }
    for (index = 0U; index < config->test_count; index++) {
        if (!is_simple_verilog_identifier(config->tests[index].top)) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use a simple Verilog identifier beginning with a letter or underscore",
                "test \"%s\" has an unsupported top identifier: %s",
                config->tests[index].name == NULL ? "?" : config->tests[index].name,
                config->tests[index].top == NULL ? "(missing)" : config->tests[index].top
            );
        }
    }
    return solar_result_ok();
}

static bool is_safe_named_item(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || name[0] == '\0' || name[0] == '.' ||
        strlen(name) > SOLAR_SAFE_NAME_LENGTH_LIMIT ||
        strstr(name, "..") != NULL) {
        return false;
    }
    while (*cursor != '\0') {
        if (!((*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_' ||
              *cursor == '-' || *cursor == '.')) {
            return false;
        }
        cursor++;
    }
    return true;
}

static SolarResult validate_named_items(
    const SolarConfig *config,
    const char *manifest_path
)
{
    size_t index;
    size_t other;

    for (index = 0U; index < config->profile_count; index++) {
        const char *name = config->profiles[index].name;

        if (!is_safe_named_item(name)) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use at most 127 letters, numbers, '_', '-' and '.', without a leading '.' or '..'",
                "profile has an unsafe name: %s", name == NULL ? "(missing)" : name
            );
        }
        for (other = 0U; other < index; other++) {
            if (strcmp(name, config->profiles[other].name) == 0) {
                return formatted_error(
                    SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                    "give every [[profile]] a unique name",
                    "duplicate profile name: %s", name
                );
            }
        }
    }
    for (index = 0U; index < config->test_count; index++) {
        const char *name = config->tests[index].name;

        if (!is_safe_named_item(name)) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use at most 127 letters, numbers, '_', '-' and '.', without a leading '.' or '..'",
                "test has an unsafe name: %s", name == NULL ? "(missing)" : name
            );
        }
        for (other = 0U; other < index; other++) {
            if (strcmp(name, config->tests[other].name) == 0) {
                return formatted_error(
                    SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                    "give every [[test]] a unique name",
                    "duplicate test name: %s", name
                );
            }
        }
    }
    return solar_result_ok();
}

static bool is_valid_define(const char *define)
{
    const unsigned char *cursor = (const unsigned char *)define;

    if (define == NULL || define[0] == '\0' || define[0] == '-') return false;
    if (!((*cursor >= 'A' && *cursor <= 'Z') ||
          (*cursor >= 'a' && *cursor <= 'z') || *cursor == '_')) {
        return false;
    }
    cursor++;
    while (*cursor != '\0' && *cursor != '=') {
        if (!((*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) {
            return false;
        }
        cursor++;
    }
    if (*cursor == '=') {
        cursor++;
        if (*cursor == '\0') return false;
        while (*cursor != '\0') {
            if (!((*cursor >= 'A' && *cursor <= 'Z') ||
                  (*cursor >= 'a' && *cursor <= 'z') ||
                  (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) {
                return false;
            }
            cursor++;
        }
    }
    return true;
}

static SolarResult validate_defines(
    const SolarStringList *defines,
    const char *manifest_path,
    const char *owner
)
{
    size_t index;

    for (index = 0U; index < defines->count; index++) {
        if (!is_valid_define(defines->items[index])) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use NAME or NAME=value with letters, numbers and underscores",
                "%s defines[%zu] is not a valid Verilog macro: %s",
                owner, index, defines->items[index]
            );
        }
    }
    return solar_result_ok();
}

static SolarResult validate_defaults(
    const SolarConfig *config,
    const char *manifest_path
)
{
    if (config->project.default_test != NULL &&
        solar_config_find_test(config, config->project.default_test) == NULL) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "set default_test to a declared test name",
            "[project].default_test does not exist: %s",
            config->project.default_test
        );
    }
    if (config->project.default_profile != NULL &&
        solar_config_find_profile(config, config->project.default_profile) == NULL) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "set default_profile to a declared profile name",
            "[project].default_profile does not exist: %s",
            config->project.default_profile
        );
    }
    return solar_result_ok();
}

static bool text_has_suffix(const char *text, const char *suffix)
{
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);

    return text_length >= suffix_length &&
        strcmp(text + text_length - suffix_length, suffix) == 0;
}

static bool valid_python_module_path(const char *path)
{
    const unsigned char *name = (const unsigned char *)strrchr(path, '/');
    size_t length;
    size_t index;

    name = name == NULL ? (const unsigned char *)path : name + 1;
    length = strlen((const char *)name);
    if (length <= 3U || strcmp((const char *)name + length - 3U, ".py") != 0) {
        return false;
    }
    if (!(isalpha(name[0]) || name[0] == (unsigned char)'_')) return false;
    for (index = 1U; index < length - 3U; index++) {
        if (!(isalnum(name[index]) || name[index] == (unsigned char)'_')) {
            return false;
        }
    }
    return true;
}

static SolarResult validate_test_values(
    const SolarConfig *config,
    const char *manifest_path
)
{
    size_t index;

    for (index = 0U; index < config->test_count; index++) {
        const SolarTest *test = &config->tests[index];
        SolarResult result;

        if (test->top == NULL || test->top[0] == '\0') {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "add top = \"module_tb\" to the test",
                "test \"%s\" has no top", test->name
            );
        }
        bool cocotb_driver = test->driver != NULL &&
            strcmp(test->driver, "cocotb") == 0;

        if (test->driver != NULL && !cocotb_driver &&
            strcmp(test->driver, "verilog") != 0) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use driver = \"verilog\" or driver = \"cocotb\"",
                "test \"%s\" has unknown driver: %s",
                test->name, test->driver
            );
        }
        if (test->kind == SOLAR_TEST_VERILOG && !cocotb_driver &&
            test->sources.count == 0U) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "add at least one Verilog source to the test",
                "test \"%s\" has no sources", test->name
            );
        }
        result = validate_relative_paths(
            &test->sources, manifest_path, "test", "sources"
        );
        if (result.status != SOLAR_STATUS_OK) return result;
        result = validate_relative_paths(
            &test->include_dirs, manifest_path, "test", "include_dirs"
        );
        if (result.status != SOLAR_STATUS_OK) return result;
        result = validate_defines(&test->defines, manifest_path, "test");
        if (result.status != SOLAR_STATUS_OK) return result;
        if (cocotb_driver &&
            (test->cocotb == NULL || test->cocotb[0] == '\0')) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "add cocotb = \"tb/test_design.py\" to the test",
                "cocotb test \"%s\" has no Python module", test->name
            );
        }
        if (!cocotb_driver && test->cocotb != NULL) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "set driver = \"cocotb\" when declaring a cocotb module",
                "test \"%s\" declares cocotb without the cocotb driver",
                test->name
            );
        }
        if (cocotb_driver &&
            (config->simulation.backend == NULL ||
             strcmp(config->simulation.backend, "verilator") != 0)) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "set [simulation].backend = \"verilator\" for cocotb tests",
                "cocotb test \"%s\" requires the Verilator backend",
                test->name
            );
        }
        if (cocotb_driver &&
            (!solar_filesystem_is_safe_relative(test->cocotb) ||
             strstr(test->cocotb, "..") != NULL ||
             !valid_python_module_path(test->cocotb))) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use a safe project-relative .py file for cocotb",
                "test \"%s\" has an invalid cocotb module path: %s",
                test->name, test->cocotb
            );
        }
        if (test->waveform == NULL || test->waveform[0] == '\0') {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "set a waveform file name for the test",
                "test \"%s\" has no waveform", test->name
            );
        }
        if (config->simulation.backend != NULL &&
            strcmp(config->simulation.backend, "verilator") == 0 &&
            !text_has_suffix(test->waveform, ".vcd") &&
            !text_has_suffix(test->waveform, ".fst")) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use a Verilator waveform ending in .vcd or .fst",
                "test \"%s\" has an unsupported Verilator waveform: %s",
                test->name, test->waveform
            );
        }
        if (config->manifest_format == 2U &&
            (!solar_filesystem_is_safe_relative(test->waveform) ||
             strstr(test->waveform, "..") != NULL)) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "use a waveform file name relative to the test artifact directory",
                "test \"%s\" has an unsafe waveform: %s",
                test->name, test->waveform
            );
        }
    }
    return solar_result_ok();
}

static SolarResult validate_profile_values(
    const SolarConfig *config,
    const char *manifest_path
)
{
    size_t index;

    for (index = 0U; index < config->profile_count; index++) {
        SolarResult result = validate_relative_paths(
            &config->profiles[index].include_dirs,
            manifest_path,
            "profile",
            "include_dirs"
        );
        if (result.status != SOLAR_STATUS_OK) return result;
        result = validate_defines(
            &config->profiles[index].defines, manifest_path, "profile"
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static bool is_processor_name(const char *name)
{
    return solar_processor_name_is_safe(name);
}

static bool is_safe_absolute_root(const char *path)
{
    const char *component;

    if (path == NULL || path[0] != '/' || path[1] == '\0' ||
        contains_control_character(path)) {
        return false;
    }
    component = path + 1;
    while (*component != '\0') {
        const char *separator = strchr(component, '/');
        size_t length = separator == NULL
            ? strlen(component)
            : (size_t)(separator - component);

        if (length == 0U || (length == 1U && component[0] == '.') ||
            (length == 2U && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (separator == NULL) break;
        component = separator + 1;
    }
    return true;
}

static SolarResult validate_language_model(
    const SolarConfig *config,
    const char *manifest_path
)
{
    if (strcmp(config->project.language, "verilog") == 0) {
        if (config->sources.rtl.count == 0U) {
            return missing_value(
                manifest_path, "sources", "rtl", "rtl = [\"rtl/counter.v\"]"
            );
        }
        if (config->compiler.backend != NULL) {
            return formatted_error(
                SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
                "remove [compiler] from a Verilog project",
                "Verilog projects have no compiler backend"
            );
        }
        return solar_result_ok();
    }
    if (!is_yanc_language(config->project.language)) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "use verilog, cmm, cpp, or asm",
            "unsupported [project].language: %s", config->project.language
        );
    }
    if (config->compiler.backend == NULL ||
        strcmp(config->compiler.backend, "yanc") != 0) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "set backend = \"yanc\" under [compiler]",
            "language %s requires the YANC compiler backend",
            config->project.language
        );
    }
    if (config->compiler.source == NULL || config->compiler.source[0] == '\0') {
        return missing_value(
            manifest_path, "compiler", "source", "source = \"src/processor.cmm\""
        );
    }
    if (!is_processor_name(config->compiler.processor)) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "use at most 127 letters, numbers and underscores, beginning with a letter or underscore",
            "[compiler].processor contains an invalid processor name"
        );
    }
    if (config->compiler.frequency_mhz == 0U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "set frequency_mhz to a positive integer",
            "[compiler].frequency_mhz must be positive"
        );
    }
    if (config->compiler.simulation_clocks == 0U) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "set simulation_clocks to a positive integer",
            "[compiler].simulation_clocks must be positive"
        );
    }
    if (config->yanc.root != NULL &&
        !is_safe_absolute_root(config->yanc.root)) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "use an absolute YANC root without '.', '..', or control characters",
            "[yanc].root is not a safe absolute path: %s",
            config->yanc.root
        );
    }
    if (config->test_count != 1U ||
        config->tests[0].kind != SOLAR_TEST_GENERATED) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "remove explicit [[test]] tables from a YANC project",
            "the current YANC flow supports only the implicit generated test"
        );
    }
    if (config->yanc.diagnostics == NULL ||
        (strcmp(config->yanc.diagnostics, "pt") != 0 &&
         strcmp(config->yanc.diagnostics, "en") != 0)) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "set diagnostics = \"pt\" or diagnostics = \"en\" under [yanc]",
            "unsupported YANC diagnostics language"
        );
    }
    return solar_result_ok();
}

static SolarResult validate_all_lists(
    const SolarConfig *config,
    const char *manifest_path
)
{
    SolarResult result = validate_relative_paths(
        &config->sources.rtl, manifest_path, "sources", "rtl"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_relative_paths(
        &config->sources.testbench, manifest_path, "sources", "testbench"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_relative_paths(
        &config->sources.include_dirs, manifest_path, "sources", "include_dirs"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_relative_paths(
        &config->compiler.include_dirs, manifest_path, "compiler", "include_dirs"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    if (config->compiler.source != NULL) {
        SolarStringList source = {(char **)&config->compiler.source, 1U};
        result = validate_relative_paths(
            &source, manifest_path, "compiler", "source"
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    result = validate_defines(
        &config->sources.defines, manifest_path, "sources"
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_profile_values(config, manifest_path);
    if (result.status != SOLAR_STATUS_OK) return result;
    return validate_test_values(config, manifest_path);
}

static SolarResult validate_legacy_waveform(
    const SolarConfig *config,
    const char *manifest_path
)
{
    if (config->manifest_format != 1U) return solar_result_ok();
    if (!solar_filesystem_is_safe_relative(config->simulation.waveform) ||
        strncmp(config->simulation.waveform, ".solar/build/sim/", 17U) != 0 ||
        strchr(config->simulation.waveform + 17U, '/') != NULL) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "place the waveform below .solar/build/sim",
            "[simulation].waveform must be a safe .solar/build/sim path"
        );
    }
    return solar_result_ok();
}

SolarResult solar_config_validate(
    const SolarConfig *config,
    const char *manifest_path
)
{
    SolarResult result;

    if (config == NULL || manifest_path == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot validate a null configuration",
            "load a Solar project before validating it"
        );
    }
    result = validate_required_values(config, manifest_path);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    if (contains_control_character(config->project.name)) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "use a printable project name without control characters",
            "[project].name contains an unsupported control character"
        );
    }
    result = validate_language_model(config, manifest_path);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_named_items(config, manifest_path);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_defaults(config, manifest_path);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = validate_top_identifiers(config, manifest_path);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    if (!solar_backend_supports(
            config->simulation.backend, SOLAR_BACKEND_SIMULATION
        )) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "select a registered simulation backend",
            "unknown simulation backend: %s", config->simulation.backend
        );
    }
    if (!solar_backend_supports(
            config->synthesis.backend, SOLAR_BACKEND_SYNTHESIS
        )) {
        return formatted_error(
            SOLAR_STATUS_CONFIG_ERROR, manifest_path, 0U,
            "select a registered synthesis backend",
            "unknown synthesis backend: %s", config->synthesis.backend
        );
    }
    result = validate_all_lists(config, manifest_path);
    if (result.status != SOLAR_STATUS_OK) return result;
    return validate_legacy_waveform(config, manifest_path);
}
