#include "solar/synthesis_statistics.h"

#include "solar/build.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STATISTICS_FILE_LIMIT ((off_t)16 * 1024 * 1024)
#define STATISTICS_LINE_LIMIT 16384U
#define CELL_TYPE_LIMIT 4096U
#define CELL_TYPE_NAME_LIMIT 255U
#define MODULE_SECTION_LIMIT 4096U

#define ALL_SUMMARY_FIELDS ((uint32_t)( \
    SOLAR_SYNTHESIS_FIELD_MODULES | \
    SOLAR_SYNTHESIS_FIELD_WIRES | \
    SOLAR_SYNTHESIS_FIELD_WIRE_BITS | \
    SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES | \
    SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS | \
    SOLAR_SYNTHESIS_FIELD_MEMORIES | \
    SOLAR_SYNTHESIS_FIELD_MEMORY_BITS | \
    SOLAR_SYNTHESIS_FIELD_PROCESSES | \
    SOLAR_SYNTHESIS_FIELD_CELLS))

typedef struct {
    const char *label;
    SolarSynthesisStatisticsField field;
} CountLabel;

typedef struct {
    char *name;
    bool design_hierarchy;
    bool collecting_cells;
    SolarGenericSynthesisStatistics statistics;
} ParsedSection;

static const CountLabel COUNT_LABELS[] = {
    {"Number of modules:", SOLAR_SYNTHESIS_FIELD_MODULES},
    {"Number of wires:", SOLAR_SYNTHESIS_FIELD_WIRES},
    {"Number of wire bits:", SOLAR_SYNTHESIS_FIELD_WIRE_BITS},
    {"Number of public wires:", SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES},
    {"Number of public wire bits:", SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS},
    {"Number of memories:", SOLAR_SYNTHESIS_FIELD_MEMORIES},
    {"Number of memory bits:", SOLAR_SYNTHESIS_FIELD_MEMORY_BITS},
    {"Number of processes:", SOLAR_SYNTHESIS_FIELD_PROCESSES},
    {"Number of cells:", SOLAR_SYNTHESIS_FIELD_CELLS}
};

static const CountLabel COMPACT_COUNT_LABELS[] = {
    {"modules", SOLAR_SYNTHESIS_FIELD_MODULES},
    {"wires", SOLAR_SYNTHESIS_FIELD_WIRES},
    {"wire bits", SOLAR_SYNTHESIS_FIELD_WIRE_BITS},
    {"public wires", SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES},
    {"public wire bits", SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS},
    {"memories", SOLAR_SYNTHESIS_FIELD_MEMORIES},
    {"memory bits", SOLAR_SYNTHESIS_FIELD_MEMORY_BITS},
    {"processes", SOLAR_SYNTHESIS_FIELD_PROCESSES},
    {"cells", SOLAR_SYNTHESIS_FIELD_CELLS}
};

static SolarResult statistics_error(
    SolarStatus status,
    const char *message,
    const char *hint
)
{
    return solar_result_error(status, message, hint);
}

void solar_synthesis_statistics_init(
    SolarGenericSynthesisStatistics *statistics
)
{
    if (statistics != NULL) (void)memset(statistics, 0, sizeof(*statistics));
}

void solar_synthesis_statistics_free(
    SolarGenericSynthesisStatistics *statistics
)
{
    size_t index;

    if (statistics == NULL) return;
    for (index = 0U; index < statistics->cell_type_count; index++) {
        free(statistics->cell_types[index].type);
    }
    free(statistics->cell_types);
    free(statistics->tool);
    free(statistics->tool_version);
    free(statistics->top);
    free(statistics->report_path);
    solar_synthesis_statistics_init(statistics);
}

static SolarResult copy_optional_string(char **destination, const char *source)
{
    if (source == NULL) return solar_result_ok();
    *destination = strdup(source);
    if (*destination == NULL) return statistics_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not copy synthesis statistics",
        "free memory and try again"
    );
    return solar_result_ok();
}

static SolarResult append_cell(
    SolarGenericSynthesisStatistics *statistics,
    const char *type,
    uint64_t count
)
{
    SolarSynthesisCellUsage *items;
    char *type_copy;
    size_t index;

    for (index = 0U; index < statistics->cell_type_count; index++) {
        if (strcmp(statistics->cell_types[index].type, type) == 0) {
            if (UINT64_MAX - statistics->cell_types[index].count < count) {
                return statistics_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "Yosys cell count exceeds the supported 64-bit range",
                    "inspect the original Yosys statistics artifact"
                );
            }
            statistics->cell_types[index].count += count;
            return solar_result_ok();
        }
    }
    if (statistics->cell_type_count >= CELL_TYPE_LIMIT) return statistics_error(
        SOLAR_STATUS_CONFIG_ERROR,
        "Yosys reported too many distinct cell types",
        "the Generic Synthesis Statistics limit is 4096 cell types"
    );
    type_copy = strdup(type);
    if (type_copy == NULL) return statistics_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate a Yosys cell type",
        "free memory and try again"
    );
    items = realloc(
        statistics->cell_types,
        (statistics->cell_type_count + 1U) * sizeof(*items)
    );
    if (items == NULL) {
        free(type_copy);
        return statistics_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not grow the Yosys cell usage table",
            "free memory and try again"
        );
    }
    statistics->cell_types = items;
    items[statistics->cell_type_count].type = type_copy;
    items[statistics->cell_type_count].count = count;
    statistics->cell_type_count++;
    return solar_result_ok();
}

SolarResult solar_synthesis_statistics_copy(
    SolarGenericSynthesisStatistics *destination,
    const SolarGenericSynthesisStatistics *source
)
{
    SolarGenericSynthesisStatistics copy;
    SolarResult result = solar_result_ok();
    size_t index;

    if (destination == NULL || source == NULL || destination == source) {
        return destination == source ? solar_result_ok() : statistics_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "synthesis statistics copy requires source and destination",
            "provide two initialized statistics structures"
        );
    }
    solar_synthesis_statistics_init(&copy);
    copy = *source;
    copy.cell_types = NULL;
    copy.cell_type_count = 0U;
    copy.tool = NULL;
    copy.tool_version = NULL;
    copy.top = NULL;
    copy.report_path = NULL;
    result = copy_optional_string(&copy.tool, source->tool);
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_optional_string(&copy.tool_version, source->tool_version);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_optional_string(&copy.top, source->top);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_optional_string(&copy.report_path, source->report_path);
    }
    for (index = 0U;
         result.status == SOLAR_STATUS_OK && index < source->cell_type_count;
         index++) {
        result = append_cell(
            &copy, source->cell_types[index].type,
            source->cell_types[index].count
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        solar_synthesis_statistics_free(destination);
        *destination = copy;
    } else {
        solar_synthesis_statistics_free(&copy);
    }
    return result;
}

bool solar_synthesis_statistics_has_field(
    const SolarGenericSynthesisStatistics *statistics,
    SolarSynthesisStatisticsField field
)
{
    return statistics != NULL &&
        (statistics->reported_fields & (uint32_t)field) != 0U;
}

size_t solar_synthesis_statistics_cell_count(
    const SolarGenericSynthesisStatistics *statistics
)
{
    return statistics == NULL ? 0U : statistics->cell_type_count;
}

const SolarSynthesisCellUsage *solar_synthesis_statistics_cell_at(
    const SolarGenericSynthesisStatistics *statistics,
    size_t index
)
{
    if (statistics == NULL || index >= statistics->cell_type_count) return NULL;
    return &statistics->cell_types[index];
}

const char *solar_synthesis_statistics_report_path(
    const SolarGenericSynthesisStatistics *statistics
)
{
    return statistics == NULL ? NULL : statistics->report_path;
}

static uint64_t *field_value(
    SolarGenericSynthesisStatistics *statistics,
    SolarSynthesisStatisticsField field
)
{
    switch (field) {
    case SOLAR_SYNTHESIS_FIELD_MODULES: return &statistics->modules;
    case SOLAR_SYNTHESIS_FIELD_WIRES: return &statistics->wires;
    case SOLAR_SYNTHESIS_FIELD_WIRE_BITS: return &statistics->wire_bits;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES: return &statistics->public_wires;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS:
        return &statistics->public_wire_bits;
    case SOLAR_SYNTHESIS_FIELD_MEMORIES: return &statistics->memories;
    case SOLAR_SYNTHESIS_FIELD_MEMORY_BITS: return &statistics->memory_bits;
    case SOLAR_SYNTHESIS_FIELD_PROCESSES: return &statistics->processes;
    case SOLAR_SYNTHESIS_FIELD_CELLS: return &statistics->cells;
    }
    return NULL;
}

static const uint64_t *const_field_value(
    const SolarGenericSynthesisStatistics *statistics,
    SolarSynthesisStatisticsField field
)
{
    switch (field) {
    case SOLAR_SYNTHESIS_FIELD_MODULES: return &statistics->modules;
    case SOLAR_SYNTHESIS_FIELD_WIRES: return &statistics->wires;
    case SOLAR_SYNTHESIS_FIELD_WIRE_BITS: return &statistics->wire_bits;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRES: return &statistics->public_wires;
    case SOLAR_SYNTHESIS_FIELD_PUBLIC_WIRE_BITS:
        return &statistics->public_wire_bits;
    case SOLAR_SYNTHESIS_FIELD_MEMORIES: return &statistics->memories;
    case SOLAR_SYNTHESIS_FIELD_MEMORY_BITS: return &statistics->memory_bits;
    case SOLAR_SYNTHESIS_FIELD_PROCESSES: return &statistics->processes;
    case SOLAR_SYNTHESIS_FIELD_CELLS: return &statistics->cells;
    }
    return NULL;
}

static int parse_uint64(const char *text, uint64_t *value_out)
{
    char *end;
    uintmax_t value;

    while (isspace((unsigned char)*text) != 0) text++;
    if (!isdigit((unsigned char)*text)) return -1;
    errno = 0;
    value = strtoumax(text, &end, 10);
    if (errno == ERANGE || value > UINT64_MAX) return -1;
    while (isspace((unsigned char)*end) != 0) end++;
    if (*end != '\0') return -1;
    *value_out = (uint64_t)value;
    return 0;
}

static char *trim_line(char *line)
{
    char *start = line;
    char *end;

    while (isspace((unsigned char)*start) != 0) start++;
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1]) != 0) end--;
    *end = '\0';
    return start;
}

static char *parse_section_name(const char *line)
{
    const char *start;
    const char *end;
    size_t length;
    char *name;

    if (strncmp(line, "===", 3U) != 0) return NULL;
    start = line + 3U;
    while (isspace((unsigned char)*start) != 0) start++;
    end = strstr(start, "===");
    if (end == NULL) return NULL;
    while (end > start && isspace((unsigned char)end[-1]) != 0) end--;
    length = (size_t)(end - start);
    if (length == 0U || length > CELL_TYPE_NAME_LIMIT) return NULL;
    name = malloc(length + 1U);
    if (name == NULL) return NULL;
    (void)memcpy(name, start, length);
    name[length] = '\0';
    return name;
}

static SolarResult append_section(
    ParsedSection **sections,
    size_t *section_count,
    char *name,
    ParsedSection **section_out
)
{
    ParsedSection *items;

    if (*section_count >= MODULE_SECTION_LIMIT) {
        free(name);
        return statistics_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "Yosys statistics contain too many module sections",
            "the Generic Synthesis Statistics limit is 4096 sections"
        );
    }
    items = realloc(*sections, (*section_count + 1U) * sizeof(*items));
    if (items == NULL) {
        free(name);
        return statistics_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate Yosys module statistics",
            "free memory and try again"
        );
    }
    *sections = items;
    *section_out = &items[*section_count];
    (void)memset(*section_out, 0, sizeof(**section_out));
    (*section_out)->name = name;
    (*section_out)->design_hierarchy = name != NULL &&
        strcmp(name, "design hierarchy") == 0;
    solar_synthesis_statistics_init(&(*section_out)->statistics);
    (*section_count)++;
    return solar_result_ok();
}

static int parse_count_line(
    const char *line,
    ParsedSection *section,
    SolarResult *result_out
)
{
    size_t index;

    for (index = 0U;
         index < sizeof(COUNT_LABELS) / sizeof(COUNT_LABELS[0]);
         index++) {
        size_t length = strlen(COUNT_LABELS[index].label);
        uint64_t value;
        uint64_t *destination;

        if (strncmp(line, COUNT_LABELS[index].label, length) != 0) continue;
        if (parse_uint64(line + length, &value) != 0) {
            *result_out = statistics_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "Yosys statistics contain an invalid numeric value",
                "inspect the recognized 'Number of ...' line in statistics.txt"
            );
            return -1;
        }
        destination = field_value(
            &section->statistics, COUNT_LABELS[index].field
        );
        *destination = value;
        section->statistics.reported_fields |=
            (uint32_t)COUNT_LABELS[index].field;
        section->statistics.available = true;
        section->collecting_cells = COUNT_LABELS[index].field ==
            SOLAR_SYNTHESIS_FIELD_CELLS;
        return 1;
    }
    if (isdigit((unsigned char)line[0])) {
        char *end;
        uintmax_t parsed;

        errno = 0;
        parsed = strtoumax(line, &end, 10);
        if (errno == ERANGE || parsed > UINT64_MAX) {
            *result_out = statistics_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "Yosys statistics contain an invalid numeric value",
                "inspect the compact count line in statistics.txt"
            );
            return -1;
        }
        while (isspace((unsigned char)*end) != 0) end++;
        for (index = 0U;
             index < sizeof(COMPACT_COUNT_LABELS) /
                sizeof(COMPACT_COUNT_LABELS[0]);
             index++) {
            uint64_t *destination;

            if (strcmp(end, COMPACT_COUNT_LABELS[index].label) != 0) continue;
            destination = field_value(
                &section->statistics, COMPACT_COUNT_LABELS[index].field
            );
            *destination = (uint64_t)parsed;
            section->statistics.reported_fields |=
                (uint32_t)COMPACT_COUNT_LABELS[index].field;
            section->statistics.available = true;
            section->collecting_cells = COMPACT_COUNT_LABELS[index].field ==
                SOLAR_SYNTHESIS_FIELD_CELLS;
            return 1;
        }
    }
    return 0;
}

static SolarResult parse_cell_line(
    const char *line,
    SolarGenericSynthesisStatistics *statistics
)
{
    const char *separator = line;
    const char *count_text;
    size_t length;
    char type[CELL_TYPE_NAME_LIMIT + 1U];
    uint64_t count;

    if (isdigit((unsigned char)line[0])) {
        char *end;
        uintmax_t parsed;
        const char *type_start;

        errno = 0;
        parsed = strtoumax(line, &end, 10);
        if (errno == ERANGE || parsed > UINT64_MAX) return statistics_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "Yosys statistics contain an invalid cell usage count",
            "inspect the cell usage row in statistics.txt"
        );
        while (isspace((unsigned char)*end) != 0) end++;
        type_start = end;
        while (*end != '\0' && isspace((unsigned char)*end) == 0) end++;
        length = (size_t)(end - type_start);
        while (isspace((unsigned char)*end) != 0) end++;
        if (length == 0U || length > CELL_TYPE_NAME_LIMIT || *end != '\0') {
            return statistics_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "Yosys statistics contain an invalid cell usage row",
                "inspect the cell type in statistics.txt"
            );
        }
        (void)memcpy(type, type_start, length);
        type[length] = '\0';
        return append_cell(statistics, type, (uint64_t)parsed);
    }
    while (*separator != '\0' &&
           isspace((unsigned char)*separator) == 0) separator++;
    if (*separator == '\0') return solar_result_ok();
    length = (size_t)(separator - line);
    count_text = separator;
    while (isspace((unsigned char)*count_text) != 0) count_text++;
    if (!isdigit((unsigned char)*count_text)) return solar_result_ok();
    if (length == 0U || length > CELL_TYPE_NAME_LIMIT ||
        parse_uint64(count_text, &count) != 0) {
        return statistics_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "Yosys statistics contain an invalid cell usage row",
            "inspect the cell type and count in statistics.txt"
        );
    }
    (void)memcpy(type, line, length);
    type[length] = '\0';
    return append_cell(statistics, type, count);
}

static int cell_usage_compare(const void *left_value, const void *right_value)
{
    const SolarSynthesisCellUsage *left = left_value;
    const SolarSynthesisCellUsage *right = right_value;

    if (left->count > right->count) return -1;
    if (left->count < right->count) return 1;
    return strcmp(left->type, right->type);
}

static SolarResult merge_field(
    SolarGenericSynthesisStatistics *destination,
    const SolarGenericSynthesisStatistics *source,
    SolarSynthesisStatisticsField field
)
{
    uint64_t *target;
    uint64_t source_value;

    if (!solar_synthesis_statistics_has_field(source, field)) {
        return solar_result_ok();
    }
    target = field_value(destination, field);
    source_value = *const_field_value(source, field);
    if (solar_synthesis_statistics_has_field(destination, field) &&
        UINT64_MAX - *target < source_value) {
        return statistics_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "aggregated Yosys statistics exceed the supported 64-bit range",
            "inspect per-module counts in statistics.txt"
        );
    }
    if (!solar_synthesis_statistics_has_field(destination, field)) *target = 0U;
    *target += source_value;
    destination->reported_fields |= (uint32_t)field;
    destination->available = true;
    return solar_result_ok();
}

static SolarResult merge_statistics(
    SolarGenericSynthesisStatistics *destination,
    const SolarGenericSynthesisStatistics *source
)
{
    size_t field_index;
    size_t cell_index;
    SolarResult result = solar_result_ok();

    for (field_index = 0U;
         field_index < sizeof(COUNT_LABELS) / sizeof(COUNT_LABELS[0]);
         field_index++) {
        if (COUNT_LABELS[field_index].field == SOLAR_SYNTHESIS_FIELD_MODULES) {
            continue;
        }
        result = merge_field(
            destination, source, COUNT_LABELS[field_index].field
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    for (cell_index = 0U; cell_index < source->cell_type_count; cell_index++) {
        result = append_cell(
            destination, source->cell_types[cell_index].type,
            source->cell_types[cell_index].count
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return result;
}

static size_t unique_module_count(
    const ParsedSection *sections,
    size_t section_count
)
{
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < section_count; index++) {
        size_t earlier;

        if (sections[index].name == NULL || sections[index].design_hierarchy) {
            continue;
        }
        for (earlier = 0U; earlier < index; earlier++) {
            if (!sections[earlier].design_hierarchy &&
                sections[earlier].name != NULL &&
                strcmp(sections[earlier].name, sections[index].name) == 0) {
                break;
            }
        }
        if (earlier == index) count++;
    }
    return count;
}

static void update_completeness(SolarGenericSynthesisStatistics *statistics)
{
    bool fields_complete =
        (statistics->reported_fields & ALL_SUMMARY_FIELDS) ==
        ALL_SUMMARY_FIELDS;
    bool cells_complete =
        !solar_synthesis_statistics_has_field(
            statistics, SOLAR_SYNTHESIS_FIELD_CELLS
        ) || statistics->cells == 0U || statistics->cell_type_count > 0U;

    statistics->available = statistics->reported_fields != 0U;
    if (!statistics->available) {
        statistics->completeness = SOLAR_SYNTHESIS_STATISTICS_NONE;
    } else if (fields_complete && cells_complete) {
        statistics->completeness = SOLAR_SYNTHESIS_STATISTICS_COMPLETE;
    } else {
        statistics->completeness = SOLAR_SYNTHESIS_STATISTICS_PARTIAL;
    }
}

static SolarResult select_statistics(
    const ParsedSection *sections,
    size_t section_count,
    SolarGenericSynthesisStatistics *statistics
)
{
    const ParsedSection *hierarchy = NULL;
    size_t modules = unique_module_count(sections, section_count);
    size_t index;
    SolarResult result = solar_result_ok();

    for (index = 0U; index < section_count; index++) {
        if (sections[index].design_hierarchy &&
            sections[index].statistics.available) {
            hierarchy = &sections[index];
        }
    }
    if (hierarchy != NULL) {
        result = solar_synthesis_statistics_copy(
            statistics, &hierarchy->statistics
        );
    } else {
        for (index = 0U;
             result.status == SOLAR_STATUS_OK && index < section_count;
             index++) {
            if (!sections[index].design_hierarchy) {
                result = merge_statistics(
                    statistics, &sections[index].statistics
                );
            }
        }
    }
    if (result.status != SOLAR_STATUS_OK) return result;
    if (!solar_synthesis_statistics_has_field(
            statistics, SOLAR_SYNTHESIS_FIELD_MODULES
        ) && modules > 0U) {
        statistics->modules = (uint64_t)modules;
        statistics->reported_fields |= SOLAR_SYNTHESIS_FIELD_MODULES;
    }
    update_completeness(statistics);
    if (statistics->cell_type_count > 1U) {
        qsort(
            statistics->cell_types, statistics->cell_type_count,
            sizeof(*statistics->cell_types), cell_usage_compare
        );
    }
    if (!statistics->available) return statistics_error(
        SOLAR_STATUS_CONFIG_ERROR,
        "Yosys statistics contain no recognized design summary",
        "preserve statistics.txt and check whether this Yosys version emits stat output"
    );
    return solar_result_ok();
}

static void free_sections(ParsedSection *sections, size_t count)
{
    size_t index;

    for (index = 0U; index < count; index++) {
        free(sections[index].name);
        solar_synthesis_statistics_free(&sections[index].statistics);
    }
    free(sections);
}

SolarResult solar_synthesis_statistics_analyze(
    const char *report_path,
    SolarGenericSynthesisStatistics *statistics
)
{
    int descriptor = -1;
    FILE *file = NULL;
    struct stat information;
    char *line = NULL;
    size_t line_capacity = 0U;
    ParsedSection *sections = NULL;
    size_t section_count = 0U;
    ParsedSection *current = NULL;
    SolarResult result = solar_result_ok();

    if (statistics != NULL) solar_synthesis_statistics_free(statistics);
    if (report_path == NULL || statistics == NULL) return statistics_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "synthesis statistics require a report path and output storage",
        "provide the dedicated Yosys statistics artifact"
    );
    descriptor = open(report_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return statistics_error(
        errno == ENOENT ? SOLAR_STATUS_NOT_FOUND : SOLAR_STATUS_IO_ERROR,
        errno == ENOENT ? "Yosys statistics artifact does not exist" :
            "cannot open the Yosys statistics artifact",
        "check the synthesis report path and project permissions"
    );
    if (fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        information.st_size > STATISTICS_FILE_LIMIT) {
        result = statistics_error(
            SOLAR_STATUS_IO_ERROR,
            "Yosys statistics must be a regular file no larger than 16 MiB",
            "inspect or regenerate the synthesis report"
        );
        goto cleanup;
    }
    file = fdopen(descriptor, "r");
    if (file == NULL) {
        result = statistics_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot read the Yosys statistics artifact",
            "check available file descriptors and permissions"
        );
        goto cleanup;
    }
    descriptor = -1;
    while (getline(&line, &line_capacity, file) >= 0) {
        char *trimmed;
        char *section_name;
        int recognized;

        if (strlen(line) > STATISTICS_LINE_LIMIT) {
            result = statistics_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "Yosys statistics contain an excessively long line",
                "the Generic Synthesis Statistics line limit is 16 KiB"
            );
            goto cleanup;
        }
        trimmed = trim_line(line);
        section_name = parse_section_name(trimmed);
        if (section_name != NULL) {
            result = append_section(
                &sections, &section_count, section_name, &current
            );
            if (result.status != SOLAR_STATUS_OK) goto cleanup;
            continue;
        }
        if (current == NULL) {
            result = append_section(
                &sections, &section_count, NULL, &current
            );
            if (result.status != SOLAR_STATUS_OK) goto cleanup;
        }
        recognized = parse_count_line(trimmed, current, &result);
        if (recognized < 0) goto cleanup;
        if (recognized == 0 && current->collecting_cells && trimmed[0] != '\0') {
            result = parse_cell_line(trimmed, &current->statistics);
            if (result.status != SOLAR_STATUS_OK) goto cleanup;
        }
    }
    if (ferror(file) != 0) {
        result = statistics_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot finish reading the Yosys statistics artifact",
            "check the project filesystem and regenerate synthesis"
        );
        goto cleanup;
    }
    result = select_statistics(sections, section_count, statistics);

cleanup:
    free(line);
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    free_sections(sections, section_count);
    if (result.status != SOLAR_STATUS_OK) {
        solar_synthesis_statistics_free(statistics);
    }
    return result;
}

static int parse_persisted_field(
    const char *line,
    const char *label,
    SolarSynthesisStatisticsField field,
    SolarGenericSynthesisStatistics *statistics
)
{
    size_t length = strlen(label);
    uint64_t value;
    uint64_t *destination;
    const char *value_text;

    if (strncmp(line, label, length) != 0) return 0;
    value_text = line + length;
    while (isspace((unsigned char)*value_text) != 0) value_text++;
    if (strcmp(value_text, "not reported") == 0) return 1;
    if (parse_uint64(value_text, &value) != 0) return -1;
    destination = field_value(statistics, field);
    *destination = value;
    statistics->reported_fields |= (uint32_t)field;
    return 1;
}

static SolarResult replace_metadata(char **destination, const char *value)
{
    char *copy = NULL;

    if (strcmp(value, "not reported") != 0) {
        copy = strdup(value);
        if (copy == NULL) return statistics_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not load persisted synthesis statistics metadata",
            "free memory and try again"
        );
    }
    free(*destination);
    *destination = copy;
    return solar_result_ok();
}

static SolarResult append_metadata(char **destination, const char *value)
{
    size_t old_length = *destination == NULL ? 0U : strlen(*destination);
    size_t value_length = strlen(value);
    char *combined;

    if (old_length > SIZE_MAX - value_length - 1U) return statistics_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "persisted synthesis metadata is too large",
        "regenerate .solar/state/last-report.txt"
    );
    combined = realloc(*destination, old_length + value_length + 1U);
    if (combined == NULL) return statistics_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not load continued synthesis metadata",
        "free memory and try again"
    );
    (void)memcpy(combined + old_length, value, value_length + 1U);
    *destination = combined;
    return solar_result_ok();
}

static const char *aligned_metadata_value(
    const char *line,
    const char *label
)
{
    const size_t label_width = 20U;
    size_t label_length = strlen(label);
    size_t line_length = strlen(line);
    size_t index;

    if (line_length < 23U || strncmp(line, "  ", 2U) != 0 ||
        label_length > label_width ||
        strncmp(line + 2U, label, label_length) != 0) {
        return NULL;
    }
    for (index = label_length; index < label_width; index++) {
        if (line[2U + index] != ' ') return NULL;
    }
    if (line[2U + label_width] != ' ') return NULL;
    return line + 2U + label_width + 1U;
}

static const char *aligned_metadata_continuation(const char *line)
{
    size_t index;

    if (strlen(line) < 23U) return NULL;
    for (index = 0U; index < 23U; index++) {
        if (line[index] != ' ') return NULL;
    }
    return line + 23U;
}

static int parse_aligned_persisted_field(
    const char *line,
    const char *label,
    SolarSynthesisStatisticsField field,
    SolarGenericSynthesisStatistics *statistics
)
{
    size_t length = strlen(label);
    const char *value_text;
    uint64_t value;
    uint64_t *destination;

    if (strncmp(line, label, length) != 0 ||
        line[length] != ' ') {
        return 0;
    }
    value_text = line + length;
    while (isspace((unsigned char)*value_text) != 0) value_text++;
    if (strcmp(value_text, "not reported") == 0) return 1;
    if (parse_uint64(value_text, &value) != 0) return -1;
    destination = field_value(statistics, field);
    *destination = value;
    statistics->reported_fields |= (uint32_t)field;
    return 1;
}

static SolarResult parse_persisted_statistics(
    char *report_text,
    SolarGenericSynthesisStatistics *statistics
)
{
    static const char HEADER[] = "GENERIC SYNTHESIS STATISTICS\n";
    char *section = strstr(report_text, HEADER);
    char *line;
    char *save = NULL;
    bool cells = false;
    char **metadata_continuation = NULL;
    SolarResult result = solar_result_ok();

    if (section == NULL) return solar_result_ok();
    line = strtok_r(section + sizeof(HEADER) - 1U, "\n", &save);
    while (line != NULL) {
        int parsed = 0;
        const char *trimmed = line;
        const char *aligned;

        while (*trimmed == ' ') trimmed++;
        aligned = metadata_continuation == NULL
            ? NULL : aligned_metadata_continuation(line);
        if (aligned != NULL) {
            result = append_metadata(metadata_continuation, aligned);
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        metadata_continuation = NULL;
        aligned = aligned_metadata_value(line, "Data schema");
        if (aligned != NULL) {
            if (strcmp(aligned, "solar-generic-synthesis-statistics/1") != 0) {
                return statistics_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "the persisted synthesis statistics schema is unsupported",
                    "regenerate the report with this Solar version"
                );
            }
        } else if ((aligned = aligned_metadata_value(line, "Status")) != NULL) {
            statistics->available = strcmp(aligned, "NOT AVAILABLE") != 0;
        } else if ((aligned = aligned_metadata_value(line, "Tool")) != NULL) {
            result = replace_metadata(&statistics->tool, aligned);
            metadata_continuation = &statistics->tool;
        } else if ((aligned = aligned_metadata_value(line, "Version")) != NULL) {
            result = replace_metadata(&statistics->tool_version, aligned);
            metadata_continuation = &statistics->tool_version;
        } else if ((aligned = aligned_metadata_value(line, "Top")) != NULL) {
            result = replace_metadata(&statistics->top, aligned);
            metadata_continuation = &statistics->top;
        } else if ((aligned = aligned_metadata_value(line, "Source")) != NULL) {
            result = replace_metadata(&statistics->report_path, aligned);
            metadata_continuation = &statistics->report_path;
        } else if (strncmp(trimmed, "Schema: ", 8U) == 0) {
            if (strcmp(trimmed + 8U, "solar-generic-synthesis-statistics/1") != 0) {
                return statistics_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "the persisted synthesis statistics schema is unsupported",
                    "regenerate the report with this Solar version"
                );
            }
        } else if (strncmp(trimmed, "Status: ", 8U) == 0) {
            statistics->available = strcmp(trimmed + 8U, "not available") != 0;
        } else if (strncmp(trimmed, "Tool: ", 6U) == 0) {
            result = replace_metadata(&statistics->tool, trimmed + 6U);
        } else if (strncmp(trimmed, "Tool+: ", 7U) == 0) {
            result = append_metadata(&statistics->tool, trimmed + 7U);
        } else if (strncmp(trimmed, "Tool version: ", 14U) == 0) {
            result = replace_metadata(&statistics->tool_version, trimmed + 14U);
        } else if (strncmp(trimmed, "Tool version+: ", 15U) == 0) {
            result = append_metadata(
                &statistics->tool_version, trimmed + 15U
            );
        } else if (strncmp(trimmed, "Top: ", 5U) == 0) {
            result = replace_metadata(&statistics->top, trimmed + 5U);
        } else if (strncmp(trimmed, "Top+: ", 6U) == 0) {
            result = append_metadata(&statistics->top, trimmed + 6U);
        } else if (strncmp(trimmed, "Original report: ", 17U) == 0) {
            result = replace_metadata(&statistics->report_path, trimmed + 17U);
        } else if (strncmp(trimmed, "Original report+: ", 18U) == 0) {
            result = append_metadata(&statistics->report_path, trimmed + 18U);
        } else if (strcmp(trimmed, "Cell usage") == 0 ||
                   strcmp(trimmed, "CELL USAGE") == 0) {
            cells = true;
        } else if (strcmp(trimmed, "Design summary") == 0 ||
                   strcmp(trimmed, "DESIGN SUMMARY") == 0) {
            cells = false;
        } else if (line[0] != ' ' && line[0] != '\0' &&
                   strncmp(line, "-", 1U) != 0) {
            break;
        } else {
            size_t index;
            static const char *const labels[] = {
                "Modules: ", "Wires: ", "Wire bits: ", "Public wires: ",
                "Public wire bits: ", "Memories: ", "Memory bits: ",
                "Processes: ", "Cells: "
            };
            static const char *const aligned_labels[] = {
                "Modules", "Wires", "Wire bits", "Public wires",
                "Public wire bits", "Memories", "Memory bits",
                "Processes", "Cells"
            };

            for (index = 0U; index < sizeof(labels) / sizeof(labels[0]); index++) {
                parsed = parse_persisted_field(
                    trimmed, labels[index], COUNT_LABELS[index].field,
                    statistics
                );
                if (parsed == 0) {
                    parsed = parse_aligned_persisted_field(
                        trimmed, aligned_labels[index],
                        COUNT_LABELS[index].field, statistics
                    );
                }
                if (parsed != 0) break;
            }
            if (parsed == 0 && cells && trimmed[0] != '\0') {
                result = parse_cell_line(trimmed, statistics);
            } else if (parsed < 0) {
                result = statistics_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "persisted synthesis statistics contain an invalid number",
                    "regenerate .solar/state/last-report.txt"
                );
            }
        }
        if (result.status != SOLAR_STATUS_OK) return result;
        line = strtok_r(NULL, "\n", &save);
    }
    update_completeness(statistics);
    if (statistics->cell_type_count > 1U) qsort(
        statistics->cell_types, statistics->cell_type_count,
        sizeof(*statistics->cell_types), cell_usage_compare
    );
    return solar_result_ok();
}

SolarResult solar_synthesis_statistics_load_last_report(
    const char *start_path,
    SolarGenericSynthesisStatistics *statistics
)
{
    char *report_text = NULL;
    SolarResult result;

    if (statistics != NULL) solar_synthesis_statistics_free(statistics);
    if (start_path == NULL || statistics == NULL) return statistics_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "persisted synthesis statistics require a project path and output storage",
        "provide the current project directory"
    );
    result = solar_build_report_read(start_path, &report_text);
    if (result.status == SOLAR_STATUS_OK) {
        result = parse_persisted_statistics(report_text, statistics);
    }
    free(report_text);
    if (result.status != SOLAR_STATUS_OK) {
        solar_synthesis_statistics_free(statistics);
    }
    return result;
}
