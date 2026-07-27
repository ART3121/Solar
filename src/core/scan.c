#include "solar/scan.h"

#include "cmm_source_internal.h"

#include "solar/config.h"
#include "solar/discovery.h"
#include "solar/filesystem.h"
#include "solar/project.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

#define SCAN_MANIFEST_LIMIT ((off_t)16 * 1024 * 1024)

static SolarResult scan_error(
    SolarStatus status,
    const char *message,
    const char *hint
)
{
    return solar_result_error(status, message, hint);
}

void solar_scan_result_init(SolarScanResult *result)
{
    if (result != NULL) (void)memset(result, 0, sizeof(*result));
}

void solar_scan_result_free(SolarScanResult *result)
{
    if (result == NULL) return;
    free(result->compiler_source);
    free(result->processor);
    solar_scan_result_init(result);
}

static bool path_below(const char *path, const char *directory)
{
    size_t length = strlen(directory);

    return strncmp(path, directory, length) == 0 && path[length] == '/';
}

static bool list_contains(const SolarStringList *list, const char *value)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (strcmp(list->items[index], value) == 0) return true;
    }
    return false;
}

static const SolarTest *find_test(const SolarConfig *config, const char *name)
{
    size_t index;

    for (index = 0U; index < config->test_count; index++) {
        if (config->tests[index].name != NULL &&
            strcmp(config->tests[index].name, name) == 0) {
            return &config->tests[index];
        }
    }
    return NULL;
}

static bool managed_test(const SolarTest *test)
{
    size_t index;

    if (test->kind != SOLAR_TEST_VERILOG || test->cocotb != NULL ||
        (test->driver != NULL && strcmp(test->driver, "verilog") != 0)) {
        return false;
    }
    for (index = 0U; index < test->sources.count; index++) {
        if (path_below(test->sources.items[index], "tb")) return true;
    }
    return false;
}

static const SolarTest *managed_test_for_source(
    const SolarConfig *config,
    const char *source
)
{
    const SolarTest *match = NULL;
    size_t index;

    for (index = 0U; index < config->test_count; index++) {
        const SolarTest *test = &config->tests[index];

        if (!managed_test(test) || !list_contains(&test->sources, source)) {
            continue;
        }
        if (match != NULL) return NULL;
        match = test;
    }
    return match;
}

static SolarResult preserve_identifiable_test_names(
    const SolarConfig *original,
    SolarConfig *inventory
)
{
    size_t index;
    size_t other;

    for (index = 0U; index < inventory->test_count; index++) {
        SolarTest *test = &inventory->tests[index];
        const SolarTest *old;
        char *name;

        if (test->sources.count == 0U) continue;
        old = managed_test_for_source(original, test->sources.items[0]);
        if (old == NULL || strcmp(old->name, test->name) == 0) continue;
        name = strdup(old->name);
        if (name == NULL) {
            return scan_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not preserve a discovered test name",
                "free memory and try again"
            );
        }
        free(test->name);
        test->name = name;
    }
    for (index = 0U; index < inventory->test_count; index++) {
        for (other = index + 1U; other < inventory->test_count; other++) {
            if (strcmp(inventory->tests[index].name,
                       inventory->tests[other].name) == 0) {
                return scan_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "scan would create duplicate test names",
                    "rename the conflicting testbench file or existing test"
                );
            }
        }
    }
    return solar_result_ok();
}

static SolarResult buffer_reserve(TextBuffer *buffer, size_t extra)
{
    size_t needed;
    size_t capacity;
    char *grown;

    if (buffer->length == SIZE_MAX ||
        extra > SIZE_MAX - buffer->length - 1U) {
        return scan_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "solar.toml is too large to synchronize",
            "reduce the manifest size and try again"
        );
    }
    needed = buffer->length + extra + 1U;
    if (needed <= buffer->capacity) return solar_result_ok();
    capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) capacity = needed;
        else capacity *= 2U;
    }
    grown = realloc(buffer->data, capacity);
    if (grown == NULL) {
        return scan_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate the synchronized manifest",
            "free memory and try again"
        );
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return solar_result_ok();
}

static SolarResult buffer_append(TextBuffer *buffer, const char *text, size_t length)
{
    SolarResult result = buffer_reserve(buffer, length);

    if (result.status != SOLAR_STATUS_OK) return result;
    (void)memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return solar_result_ok();
}

static SolarResult buffer_text(TextBuffer *buffer, const char *text)
{
    return buffer_append(buffer, text, strlen(text));
}

static SolarResult buffer_ensure_newline(TextBuffer *buffer)
{
    if (buffer->length == 0U || buffer->data[buffer->length - 1U] == '\n') {
        return solar_result_ok();
    }
    return buffer_text(buffer, "\n");
}

static SolarResult buffer_quoted(TextBuffer *buffer, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    SolarResult result = buffer_text(buffer, "\"");

    while (result.status == SOLAR_STATUS_OK && *cursor != '\0') {
        if (*cursor == '\\' || *cursor == '"') {
            char escaped[2] = {'\\', (char)*cursor};
            result = buffer_append(buffer, escaped, sizeof(escaped));
        } else {
            result = buffer_append(buffer, (const char *)cursor, 1U);
        }
        cursor++;
    }
    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, "\"");
    return result;
}

static SolarResult append_string_list(
    TextBuffer *buffer,
    const char *key,
    const SolarStringList *list
)
{
    size_t index;
    SolarResult result = buffer_text(buffer, key);

    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, " = [");
    if (list->count > 0U && result.status == SOLAR_STATUS_OK) {
        result = buffer_text(buffer, "\n");
    }
    for (index = 0U; index < list->count && result.status == SOLAR_STATUS_OK;
         index++) {
        result = buffer_text(buffer, "    ");
        if (result.status == SOLAR_STATUS_OK) {
            result = buffer_quoted(buffer, list->items[index]);
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = buffer_text(
                buffer, index + 1U == list->count ? "\n" : ",\n"
            );
        }
    }
    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, "]\n");
    return result;
}

static SolarResult append_key_string(
    TextBuffer *buffer,
    const char *key,
    const char *value
)
{
    SolarResult result = buffer_text(buffer, key);

    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, " = ");
    if (result.status == SOLAR_STATUS_OK) result = buffer_quoted(buffer, value);
    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, "\n");
    return result;
}

static SolarResult append_test_table(
    TextBuffer *buffer,
    const SolarTest *test,
    const SolarTest *preserved
)
{
    SolarResult result = buffer_text(buffer, "\n\n[[test]]\n");

    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(buffer, "name", test->name);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(buffer, "top", test->top);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_string_list(buffer, "sources", &test->sources);
    }
    if (preserved != NULL && preserved->include_dirs.count > 0U &&
        result.status == SOLAR_STATUS_OK) {
        result = append_string_list(
            buffer, "include_dirs", &preserved->include_dirs
        );
    }
    if (preserved != NULL && preserved->defines.count > 0U &&
        result.status == SOLAR_STATUS_OK) {
        result = append_string_list(buffer, "defines", &preserved->defines);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(buffer, "waveform", test->waveform);
    }
    return result;
}

static SolarResult read_file(const char *path, char **text_out, size_t *length_out)
{
    int descriptor = -1;
    struct stat information;
    char *text;
    size_t length;
    size_t offset = 0U;

    *text_out = NULL;
    *length_out = 0U;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        information.st_size > SCAN_MANIFEST_LIMIT) {
        if (descriptor >= 0) (void)close(descriptor);
        return scan_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot safely read solar.toml for synchronization",
            "use a regular manifest no larger than 16 MiB"
        );
    }
    length = (size_t)information.st_size;
    text = malloc(length + 1U);
    if (text == NULL) {
        (void)close(descriptor);
        return scan_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate solar.toml synchronization text",
            "free memory and try again"
        );
    }
    while (offset < length) {
        ssize_t count = read(descriptor, text + offset, length - offset);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            free(text);
            (void)close(descriptor);
            return scan_error(
                SOLAR_STATUS_IO_ERROR,
                "failed while reading solar.toml",
                "check the project filesystem and try again"
            );
        }
        offset += (size_t)count;
    }
    if (close(descriptor) != 0) {
        free(text);
        return scan_error(
            SOLAR_STATUS_IO_ERROR,
            "failed to close solar.toml after reading",
            "check the project filesystem and try again"
        );
    }
    text[length] = '\0';
    *text_out = text;
    *length_out = length;
    return solar_result_ok();
}

static const char *trim_line(const char *line, const char *end)
{
    while (line < end && (*line == ' ' || *line == '\t')) line++;
    return line;
}

static bool line_equals(const char *line, const char *end, const char *value)
{
    const char *start = trim_line(line, end);
    size_t length;

    while (end > start && (end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == ' ' || end[-1] == '\t')) end--;
    length = (size_t)(end - start);
    return strlen(value) == length && strncmp(start, value, length) == 0;
}

static bool line_is_header(const char *line, const char *end)
{
    const char *start = trim_line(line, end);

    return start < end && *start == '[';
}

static bool line_is_key(
    const char *line,
    const char *end,
    const char *key
)
{
    const char *start = trim_line(line, end);
    size_t length = strlen(key);

    if ((size_t)(end - start) < length || strncmp(start, key, length) != 0) {
        return false;
    }
    start += length;
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    return start < end && *start == '=';
}

static bool scan_removes_managed_default(
    const SolarConfig *original,
    const SolarConfig *inventory
)
{
    const SolarTest *test;

    if (original->project.default_test == NULL) return false;
    test = find_test(original, original->project.default_test);
    if (find_test(inventory, original->project.default_test) != NULL) {
        return false;
    }
    if (test != NULL) return managed_test(test);
    /* A manifest with no explicit tests delegates its default to conventional
     * discovery, as the init template does. Once no matching tb/ file exists,
     * the default is managed scan state rather than a user-owned test table. */
    return original->test_count == 0U;
}

static int bracket_delta(const char *line, const char *end)
{
    bool quoted = false;
    bool escaped = false;
    int delta = 0;

    while (line < end) {
        char character = *line++;

        if (!quoted && character == '#') break;
        if (quoted && escaped) {
            escaped = false;
        } else if (quoted && character == '\\') {
            escaped = true;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (!quoted && character == '[') {
            delta++;
        } else if (!quoted && character == ']') {
            delta--;
        }
    }
    return delta;
}

static SolarResult synchronize_v2_text(
    const char *input,
    size_t input_length,
    const SolarConfig *original,
    const SolarConfig *inventory,
    char **text_out,
    size_t *length_out
)
{
    TextBuffer output = {0};
    const char *cursor = input;
    const char *limit = input + input_length;
    bool in_project = false;
    bool in_sources = false;
    bool in_synthesis = false;
    bool sources_found = false;
    bool rtl_written = false;
    bool synthesis_top_written = false;
    bool skip_test = false;
    bool skip_rtl = false;
    bool remove_default_test = scan_removes_managed_default(
        original, inventory
    );
    int rtl_depth = 0;
    size_t test_index = 0U;
    SolarResult result = solar_result_ok();

    *text_out = NULL;
    *length_out = 0U;
    while (cursor < limit && result.status == SOLAR_STATUS_OK) {
        const char *end = memchr(cursor, '\n', (size_t)(limit - cursor));
        bool header;

        end = end == NULL ? limit : end + 1;
        header = line_is_header(cursor, end);
        if (header) {
            if (in_sources && !rtl_written) {
                result = append_string_list(
                    &output, "rtl", &inventory->sources.rtl
                );
                rtl_written = true;
            }
            if (in_synthesis && !synthesis_top_written &&
                inventory->synthesis.top != NULL) {
                result = append_key_string(
                    &output, "top", inventory->synthesis.top
                );
                synthesis_top_written = true;
            }
            in_project = line_equals(cursor, end, "[project]");
            in_sources = line_equals(cursor, end, "[sources]");
            in_synthesis = line_equals(cursor, end, "[synthesis]");
            if (in_sources) sources_found = true;
            skip_test = false;
            skip_rtl = false;
            if (line_equals(cursor, end, "[[test]]")) {
                if (test_index >= original->test_count) {
                    result = scan_error(
                        SOLAR_STATUS_INTERNAL_ERROR,
                        "manifest test tables do not match the parsed model",
                        "report this Solar parser bug"
                    );
                    break;
                }
                skip_test = managed_test(&original->tests[test_index]);
                test_index++;
            }
        }
        if (skip_test) {
            cursor = end;
            continue;
        }
        if (skip_rtl) {
            rtl_depth += bracket_delta(cursor, end);
            if (rtl_depth <= 0) skip_rtl = false;
            cursor = end;
            continue;
        }
        if (in_project && remove_default_test &&
            line_is_key(cursor, end, "default_test")) {
            cursor = end;
            continue;
        }
        if (in_sources && line_is_key(cursor, end, "rtl")) {
            result = append_string_list(
                &output, "rtl", &inventory->sources.rtl
            );
            rtl_written = true;
            rtl_depth = bracket_delta(cursor, end);
            skip_rtl = rtl_depth > 0;
            cursor = end;
            continue;
        }
        if (in_synthesis && line_is_key(cursor, end, "top")) {
            result = append_key_string(
                &output, "top", inventory->synthesis.top
            );
            synthesis_top_written = true;
            cursor = end;
            continue;
        }
        result = buffer_append(&output, cursor, (size_t)(end - cursor));
        cursor = end;
    }
    if (result.status == SOLAR_STATUS_OK && in_sources && !rtl_written) {
        result = append_string_list(&output, "rtl", &inventory->sources.rtl);
        rtl_written = true;
    }
    if (result.status == SOLAR_STATUS_OK && in_synthesis &&
        !synthesis_top_written && inventory->synthesis.top != NULL) {
        result = append_key_string(
            &output, "top", inventory->synthesis.top
        );
    }
    if (result.status == SOLAR_STATUS_OK && !sources_found) {
        result = buffer_text(&output, "\n[sources]\n");
        if (result.status == SOLAR_STATUS_OK) {
            result = append_string_list(
                &output, "rtl", &inventory->sources.rtl
            );
        }
    }
    while (output.length > 0U &&
           (output.data[output.length - 1U] == '\n' ||
            output.data[output.length - 1U] == '\r' ||
            output.data[output.length - 1U] == ' ' ||
            output.data[output.length - 1U] == '\t')) {
        output.length--;
    }
    if (output.data != NULL) output.data[output.length] = '\0';
    for (test_index = 0U;
         test_index < inventory->test_count && result.status == SOLAR_STATUS_OK;
         test_index++) {
        const SolarTest *test = &inventory->tests[test_index];
        const SolarTest *preserved = find_test(original, test->name);

        if (preserved != NULL && !managed_test(preserved)) preserved = NULL;
        result = append_test_table(&output, test, preserved);
    }
    if (result.status != SOLAR_STATUS_OK) {
        free(output.data);
        return result;
    }
    *text_out = output.data;
    *length_out = output.length;
    return solar_result_ok();
}

typedef enum {
    CMM_SECTION_OTHER = 0,
    CMM_SECTION_PROJECT,
    CMM_SECTION_COMPILER,
    CMM_SECTION_SYNTHESIS
} CmmManifestSection;

typedef struct {
    bool project_name;
    bool compiler_source;
    bool compiler_processor;
    bool synthesis_top;
} CmmWrittenFields;

static SolarResult finish_cmm_section(
    TextBuffer *output,
    CmmManifestSection section,
    const char *source,
    const char *processor,
    CmmWrittenFields *written
)
{
    SolarResult result = solar_result_ok();

    if (section == CMM_SECTION_PROJECT && !written->project_name) {
        result = buffer_ensure_newline(output);
        if (result.status == SOLAR_STATUS_OK) {
            result = append_key_string(output, "name", processor);
            written->project_name = result.status == SOLAR_STATUS_OK;
        }
    } else if (section == CMM_SECTION_COMPILER) {
        if (!written->compiler_source) {
            result = buffer_ensure_newline(output);
            if (result.status == SOLAR_STATUS_OK) {
                result = append_key_string(output, "source", source);
                written->compiler_source = result.status == SOLAR_STATUS_OK;
            }
        }
        if (result.status == SOLAR_STATUS_OK && !written->compiler_processor) {
            result = buffer_ensure_newline(output);
            if (result.status == SOLAR_STATUS_OK) {
                result = append_key_string(output, "processor", processor);
                written->compiler_processor = result.status == SOLAR_STATUS_OK;
            }
        }
    } else if (section == CMM_SECTION_SYNTHESIS && !written->synthesis_top) {
        result = buffer_ensure_newline(output);
        if (result.status == SOLAR_STATUS_OK) {
            result = append_key_string(output, "top", processor);
            written->synthesis_top = result.status == SOLAR_STATUS_OK;
        }
    }
    return result;
}

static CmmManifestSection cmm_section_for_header(
    const char *line,
    const char *end
)
{
    if (line_equals(line, end, "[project]")) return CMM_SECTION_PROJECT;
    if (line_equals(line, end, "[compiler]")) return CMM_SECTION_COMPILER;
    if (line_equals(line, end, "[synthesis]")) return CMM_SECTION_SYNTHESIS;
    return CMM_SECTION_OTHER;
}

static SolarResult synchronize_cmm_v2_text(
    const char *input,
    size_t input_length,
    const char *source,
    const char *processor,
    char **text_out,
    size_t *length_out
)
{
    TextBuffer output = {0};
    CmmManifestSection section = CMM_SECTION_OTHER;
    CmmWrittenFields written = {0};
    const char *cursor = input;
    const char *limit = input + input_length;
    SolarResult result = solar_result_ok();

    *text_out = NULL;
    *length_out = 0U;
    while (cursor < limit && result.status == SOLAR_STATUS_OK) {
        const char *end = memchr(cursor, '\n', (size_t)(limit - cursor));

        end = end == NULL ? limit : end + 1;
        if (line_is_header(cursor, end)) {
            result = finish_cmm_section(
                &output, section, source, processor, &written
            );
            if (result.status != SOLAR_STATUS_OK) break;
            section = cmm_section_for_header(cursor, end);
        }
        if (section == CMM_SECTION_PROJECT &&
            line_is_key(cursor, end, "name")) {
            result = append_key_string(&output, "name", processor);
            written.project_name = result.status == SOLAR_STATUS_OK;
        } else if (section == CMM_SECTION_COMPILER &&
                   line_is_key(cursor, end, "source")) {
            result = append_key_string(&output, "source", source);
            written.compiler_source = result.status == SOLAR_STATUS_OK;
        } else if (section == CMM_SECTION_COMPILER &&
                   line_is_key(cursor, end, "processor")) {
            result = append_key_string(&output, "processor", processor);
            written.compiler_processor = result.status == SOLAR_STATUS_OK;
        } else if (section == CMM_SECTION_SYNTHESIS &&
                   line_is_key(cursor, end, "top")) {
            result = append_key_string(&output, "top", processor);
            written.synthesis_top = result.status == SOLAR_STATUS_OK;
        } else {
            result = buffer_append(&output, cursor, (size_t)(end - cursor));
        }
        cursor = end;
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = finish_cmm_section(
            &output, section, source, processor, &written
        );
    }
    if (result.status == SOLAR_STATUS_OK &&
        (!written.project_name || !written.compiler_source ||
         !written.compiler_processor || !written.synthesis_top)) {
        result = scan_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "solar.toml is missing a section required for CMM synchronization",
            "define [project], [compiler], and [synthesis], then run solar scan again"
        );
    }
    if (result.status != SOLAR_STATUS_OK) {
        free(output.data);
        return result;
    }
    *text_out = output.data;
    *length_out = output.length;
    return solar_result_ok();
}

static SolarResult append_v1_project(
    TextBuffer *output,
    const SolarConfig *original,
    const SolarConfig *inventory
)
{
    const SolarTest *test = &inventory->tests[0];
    const char *synthesis_top = inventory->synthesis.top;
    SolarResult result = buffer_text(output, "[solar]\nformat = 2\n\n[project]\n");

    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(output, "name", original->project.name);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(output, "language", "verilog");
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(output, "default_test", test->name);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = buffer_text(output, "\n[sources]\n");
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_string_list(output, "rtl", &inventory->sources.rtl);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = buffer_text(output, "\n[simulation]\n");
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(
            output, "backend", original->simulation.backend
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = buffer_text(output, "\n[synthesis]\n");
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_key_string(
            output, "backend", original->synthesis.backend
        );
    }
    if (result.status == SOLAR_STATUS_OK && synthesis_top != NULL) {
        result = append_key_string(output, "top", synthesis_top);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_test_table(output, test, NULL);
    }
    return result;
}

static SolarResult migrate_v1_text(
    const SolarConfig *original,
    const SolarConfig *inventory,
    char **text_out,
    size_t *length_out
)
{
    TextBuffer output = {0};
    SolarResult result;

    *text_out = NULL;
    *length_out = 0U;
    if (inventory->test_count != 1U || original->test_count != 1U ||
        original->tests[0].top == NULL ||
        strcmp(original->tests[0].top, inventory->tests[0].top) != 0) {
        return scan_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "format-1 project cannot be migrated unambiguously",
            "make one testbench below tb/ match [simulation].top, then run solar scan again"
        );
    }
    result = append_v1_project(&output, original, inventory);
    if (result.status != SOLAR_STATUS_OK) {
        free(output.data);
        return result;
    }
    *text_out = output.data;
    *length_out = output.length;
    return solar_result_ok();
}

static SolarResult build_inventory(
    const char *root,
    const SolarConfig *original,
    SolarProject *inventory
)
{
    size_t index;
    SolarResult result;

    solar_project_init(inventory);
    inventory->root = strdup(root);
    inventory->manifest_path = strdup("solar.toml");
    inventory->config.manifest_format = 2U;
    inventory->config.project.name = strdup(original->project.name);
    inventory->config.project.language = strdup("verilog");
    if (inventory->root == NULL || inventory->manifest_path == NULL ||
        inventory->config.project.name == NULL ||
        inventory->config.project.language == NULL) {
        solar_project_free(inventory);
        return scan_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate the scan inventory",
            "free memory and try again"
        );
    }
    for (index = 0U; index < original->sources.rtl.count; index++) {
        if (path_below(original->sources.rtl.items[index], "rtl")) continue;
        result = solar_string_list_append_copy(
            &inventory->config.sources.rtl,
            original->sources.rtl.items[index]
        );
        if (result.status != SOLAR_STATUS_OK) {
            solar_project_free(inventory);
            return result;
        }
    }
    result = solar_discovery_apply(inventory, &inventory->discovery);
    if (result.status != SOLAR_STATUS_OK) {
        solar_project_free(inventory);
        return result;
    }
    if (inventory->discovery.ambiguous_testbench_files > 0U) {
        solar_project_free(inventory);
        return scan_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "one or more discovered testbenches have an ambiguous top module",
            "give each tb/*.v or tb/*.sv file one module matching its filename"
        );
    }
    return solar_result_ok();
}

static void count_changes(
    const SolarConfig *original,
    const SolarConfig *inventory,
    SolarScanResult *scan
)
{
    size_t index;

    scan->rtl_total = inventory->sources.rtl.count;
    scan->tests_total = inventory->test_count;
    for (index = 0U; index < inventory->sources.rtl.count; index++) {
        if (!list_contains(&original->sources.rtl,
                           inventory->sources.rtl.items[index])) {
            scan->rtl_added++;
        }
    }
    for (index = 0U; index < original->sources.rtl.count; index++) {
        if (path_below(original->sources.rtl.items[index], "rtl") &&
            !list_contains(&inventory->sources.rtl,
                           original->sources.rtl.items[index])) {
            scan->rtl_removed++;
        }
    }
    for (index = 0U; index < inventory->test_count; index++) {
        const SolarTest *old = find_test(
            original, inventory->tests[index].name
        );
        if (old == NULL || !managed_test(old)) scan->tests_added++;
    }
    for (index = 0U; index < original->test_count; index++) {
        if (!managed_test(&original->tests[index])) scan->tests_total++;
        if (managed_test(&original->tests[index]) &&
            find_test(inventory, original->tests[index].name) == NULL) {
            scan->tests_removed++;
        }
    }
}

static SolarResult write_candidate(
    const char *root,
    const char *path,
    const char *text,
    size_t length,
    mode_t mode
)
{
    int descriptor;
    size_t written = 0U;
    SolarResult result = solar_filesystem_reset_artifact(
        root, ".solar/tmp/scan/solar.toml.next"
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    descriptor = open(
        path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        mode & 0777
    );
    if (descriptor < 0) {
        return scan_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot create the candidate solar.toml",
            "check .solar/tmp permissions and try again"
        );
    }
    while (written < length) {
        ssize_t count = write(descriptor, text + written, length - written);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            result = scan_error(
                SOLAR_STATUS_IO_ERROR,
                "cannot write the candidate solar.toml",
                "check free disk space and try again"
            );
            break;
        }
        written += (size_t)count;
    }
    if (result.status == SOLAR_STATUS_OK && fsync(descriptor) != 0) {
        result = scan_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot synchronize the candidate solar.toml",
            "check the project filesystem and try again"
        );
    }
    if (close(descriptor) != 0 && result.status == SOLAR_STATUS_OK) {
        result = scan_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot close the candidate solar.toml",
            "check the project filesystem and try again"
        );
    }
    if (result.status != SOLAR_STATUS_OK) (void)unlink(path);
    return result;
}

static SolarResult validate_candidate(const char *root, const char *path)
{
    SolarProject project;
    SolarConfig parsed;
    SolarResult result;

    solar_project_init(&project);
    solar_config_init(&parsed);
    result = solar_config_parse_file(path, &parsed);
    if (result.status != SOLAR_STATUS_OK) return result;
    project.root = strdup(root);
    project.manifest_path = strdup(path);
    if (project.root == NULL || project.manifest_path == NULL) {
        solar_config_free(&parsed);
        solar_project_free(&project);
        return scan_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate candidate validation state",
            "free memory and try again"
        );
    }
    project.config = parsed;
    solar_config_init(&parsed);
    result = solar_discovery_apply(&project, &project.discovery);
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    solar_project_free(&project);
    return result;
}

SolarResult solar_scan_project(const char *start_path, SolarScanResult *scan)
{
    SolarConfig original;
    SolarProject inventory;
    char *root = NULL;
    char *manifest = NULL;
    char *candidate_path = NULL;
    char *input = NULL;
    char *candidate = NULL;
    char *cmm_source = NULL;
    char *cmm_source_path = NULL;
    char *cmm_processor = NULL;
    size_t input_length = 0U;
    size_t candidate_length = 0U;
    struct stat information;
    SolarProjectLock project_lock;
    SolarResult result;
    bool cmm_scan = false;

    if (start_path == NULL || scan == NULL) {
        return scan_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot scan without a project path and result storage",
            "provide a project directory"
        );
    }
    solar_scan_result_init(scan);
    solar_config_init(&original);
    solar_project_init(&inventory);
    solar_project_lock_init(&project_lock);
    result = solar_project_find_root(start_path, &root);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_lock_acquire(root, &project_lock);
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_join(root, "solar.toml", &manifest);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_config_parse_file(manifest, &original);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    cmm_scan = original.project.language != NULL &&
        strcmp(original.project.language, "cmm") == 0;
    if (original.project.language == NULL ||
        (strcmp(original.project.language, "verilog") != 0 && !cmm_scan)) {
        result = scan_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "solar scan supports conventional Verilog and CMM projects",
            "configure C++ and Assembly sources explicitly under [compiler]"
        );
        goto cleanup;
    }
    scan->kind = cmm_scan ? SOLAR_SCAN_CMM : SOLAR_SCAN_VERILOG;
    if (cmm_scan) {
        if (original.manifest_format != 2U) {
            result = scan_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "CMM scan requires Solar project format 2",
                "add [solar] format = 2 before synchronizing the CMM project"
            );
            goto cleanup;
        }
        result = solar_cmm_discover_source(
            root, original.compiler.source, &cmm_source
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        result = solar_filesystem_join(root, cmm_source, &cmm_source_path);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        result = solar_cmm_read_processor_name(
            cmm_source_path, &cmm_processor
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    } else {
        result = build_inventory(root, &original, &inventory);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        result = preserve_identifiable_test_names(&original, &inventory.config);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        count_changes(&original, &inventory.config, scan);
    }
    result = read_file(manifest, &input, &input_length);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (cmm_scan) {
        result = synchronize_cmm_v2_text(
            input, input_length, cmm_source, cmm_processor,
            &candidate, &candidate_length
        );
    } else if (original.manifest_format == 1U) {
        result = migrate_v1_text(
            &original, &inventory.config, &candidate, &candidate_length
        );
        scan->migrated_v1 = result.status == SOLAR_STATUS_OK;
    } else {
        result = synchronize_v2_text(
            input, input_length, &original, &inventory.config,
            &candidate, &candidate_length
        );
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    scan->changed = input_length != candidate_length ||
        memcmp(input, candidate, input_length) != 0;
    if (!scan->changed) {
        result = solar_result_ok();
        goto cleanup;
    }
    if (stat(manifest, &information) != 0) {
        result = scan_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot inspect solar.toml before replacement",
            "check manifest permissions and try again"
        );
        goto cleanup;
    }
    result = solar_filesystem_prepare_generated_directory(
        root, ".solar/tmp/scan"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_join(
        root, ".solar/tmp/scan/solar.toml.next", &candidate_path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = write_candidate(
        root, candidate_path, candidate, candidate_length, information.st_mode
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = validate_candidate(root, candidate_path);
    if (result.status != SOLAR_STATUS_OK) {
        (void)unlink(candidate_path);
        goto cleanup;
    }
    if (rename(candidate_path, manifest) != 0) {
        (void)unlink(candidate_path);
        result = scan_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot atomically replace solar.toml",
            "check project filesystem permissions and try again"
        );
        goto cleanup;
    }
    {
        int root_descriptor = open(
            root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        );
        if (root_descriptor >= 0) {
            (void)fsync(root_descriptor);
            (void)close(root_descriptor);
        }
    }
    result = solar_result_ok();

cleanup:
    if (result.status == SOLAR_STATUS_OK && cmm_scan) {
        scan->compiler_source = cmm_source;
        scan->processor = cmm_processor;
        cmm_source = NULL;
        cmm_processor = NULL;
    }
    solar_project_lock_release(&project_lock);
    free(cmm_processor);
    free(cmm_source_path);
    free(cmm_source);
    free(candidate);
    free(input);
    free(candidate_path);
    free(manifest);
    free(root);
    solar_project_free(&inventory);
    solar_config_free(&original);
    return result;
}
