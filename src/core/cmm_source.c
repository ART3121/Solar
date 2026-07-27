#include "cmm_source_internal.h"

#include "solar/config.h"
#include "solar/filesystem.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CMM_SOURCE_LIMIT ((off_t)16 * 1024 * 1024)
#define CMM_SOURCE_COUNT_LIMIT 256U
#define CMM_DIRECTORY_DEPTH_LIMIT 64U
#define CMM_PROCESSOR_NAME_LIMIT 127U

static SolarResult cmm_error(
    SolarStatus status,
    const char *hint,
    const char *format,
    ...
)
{
    SolarResult result;
    va_list arguments;

    result.status = status;
    result.diagnostic.severity = SOLAR_DIAGNOSTIC_ERROR;
    va_start(arguments, format);
    (void)vsnprintf(
        result.diagnostic.message,
        sizeof(result.diagnostic.message),
        format,
        arguments
    );
    va_end(arguments);
    (void)snprintf(
        result.diagnostic.hint,
        sizeof(result.diagnostic.hint),
        "%s",
        hint == NULL ? "" : hint
    );
    return result;
}

static bool has_cmm_suffix(const char *path)
{
    size_t length = strlen(path);

    return length >= 4U && strcmp(path + length - 4U, ".cmm") == 0;
}

bool solar_processor_name_is_safe(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || name[0] == '\0' ||
        strlen(name) > CMM_PROCESSOR_NAME_LIMIT) {
        return false;
    }
    if (!((*cursor >= 'A' && *cursor <= 'Z') ||
          (*cursor >= 'a' && *cursor <= 'z') || *cursor == '_')) {
        return false;
    }
    cursor++;
    while (*cursor != '\0') {
        if (!((*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) {
            return false;
        }
        cursor++;
    }
    return true;
}

static int compare_strings(const void *left, const void *right)
{
    const char *const *left_string = left;
    const char *const *right_string = right;

    return strcmp(*left_string, *right_string);
}

static SolarResult append_candidate(
    SolarStringList *candidates,
    const char *relative_path
)
{
    size_t index;

    for (index = 0U; index < candidates->count; index++) {
        if (strcmp(candidates->items[index], relative_path) == 0) {
            return solar_result_ok();
        }
    }
    if (candidates->count >= CMM_SOURCE_COUNT_LIMIT) {
        return cmm_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "keep one CMM processor source below software/",
            "CMM source discovery exceeded the limit of %u files",
            CMM_SOURCE_COUNT_LIMIT
        );
    }
    return solar_string_list_append_copy(candidates, relative_path);
}

static SolarResult discover_directory(
    const char *absolute_directory,
    const char *relative_directory,
    size_t depth,
    SolarStringList *candidates
)
{
    DIR *directory = NULL;
    struct dirent *entry;
    SolarResult result = solar_result_ok();

    if (depth > CMM_DIRECTORY_DEPTH_LIMIT) {
        return cmm_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "reduce directory nesting below software/",
            "CMM source discovery exceeded the directory depth limit"
        );
    }
    directory = opendir(absolute_directory);
    if (directory == NULL) {
        return cmm_error(
            SOLAR_STATUS_IO_ERROR,
            "check permissions below software/ and try again",
            "cannot inspect CMM source directory %s: %s",
            absolute_directory,
            strerror(errno)
        );
    }
    while ((entry = readdir(directory)) != NULL) {
        char *absolute = NULL;
        char *relative = NULL;
        struct stat information;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        result = solar_filesystem_join(absolute_directory, entry->d_name, &absolute);
        if (result.status != SOLAR_STATUS_OK) goto entry_cleanup;
        result = solar_filesystem_join(relative_directory, entry->d_name, &relative);
        if (result.status != SOLAR_STATUS_OK) goto entry_cleanup;
        if (!solar_filesystem_is_safe_relative(relative)) {
            result = cmm_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "rename the source path to remove unsafe components",
                "unsafe path encountered during CMM source discovery: %s",
                relative
            );
            goto entry_cleanup;
        }
        if (lstat(absolute, &information) != 0) {
            result = cmm_error(
                SOLAR_STATUS_IO_ERROR,
                "check the project filesystem and try again",
                "cannot inspect CMM source candidate %s: %s",
                relative,
                strerror(errno)
            );
            goto entry_cleanup;
        }
        if (S_ISLNK(information.st_mode)) {
            result = solar_result_ok();
        } else if (S_ISDIR(information.st_mode)) {
            result = discover_directory(
                absolute, relative, depth + 1U, candidates
            );
        } else if (S_ISREG(information.st_mode) && has_cmm_suffix(relative)) {
            result = append_candidate(candidates, relative);
        }

entry_cleanup:
        free(relative);
        free(absolute);
        if (result.status != SOLAR_STATUS_OK) break;
    }
    if (closedir(directory) != 0 && result.status == SOLAR_STATUS_OK) {
        result = cmm_error(
            SOLAR_STATUS_IO_ERROR,
            "check the project filesystem and try again",
            "cannot close CMM source directory %s: %s",
            absolute_directory,
            strerror(errno)
        );
    }
    return result;
}

static SolarResult add_configured_source(
    const char *project_root,
    const char *configured_source,
    SolarStringList *candidates
)
{
    int directory = -1;
    int child = -1;
    char *path = NULL;
    char *component;
    SolarResult result = solar_result_ok();
    bool regular = false;

    if (configured_source == NULL || !has_cmm_suffix(configured_source) ||
        !solar_filesystem_is_safe_relative(configured_source)) {
        return solar_result_ok();
    }
    directory = open(
        project_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
    );
    path = strdup(configured_source);
    if (directory < 0 || path == NULL) {
        result = cmm_error(
            directory < 0 ? SOLAR_STATUS_IO_ERROR : SOLAR_STATUS_INTERNAL_ERROR,
            directory < 0
                ? "check the project directory and its permissions"
                : "free memory and try again",
            "cannot inspect the configured CMM source"
        );
        goto cleanup;
    }
    component = path;
    for (;;) {
        char *separator = strchr(component, '/');
        struct stat information;
        int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;

        if (separator != NULL) {
            *separator = '\0';
            flags |= O_DIRECTORY;
        }
        child = openat(directory, component, flags);
        if (child < 0) {
            if (errno == ENOENT || errno == ENOTDIR || errno == ELOOP) {
                result = solar_result_ok();
            } else {
                result = cmm_error(
                    SOLAR_STATUS_IO_ERROR,
                    "check the configured CMM source path and permissions",
                    "cannot inspect configured CMM source %s: %s",
                    configured_source,
                    strerror(errno)
                );
            }
            goto cleanup;
        }
        if (separator == NULL) {
            if (fstat(child, &information) != 0) {
                result = cmm_error(
                    SOLAR_STATUS_IO_ERROR,
                    "check the configured CMM source path and permissions",
                    "cannot inspect configured CMM source %s: %s",
                    configured_source,
                    strerror(errno)
                );
                goto cleanup;
            }
            regular = S_ISREG(information.st_mode);
            break;
        }
        (void)close(directory);
        directory = child;
        child = -1;
        component = separator + 1;
    }
    if (regular) result = append_candidate(candidates, configured_source);

cleanup:
    if (child >= 0) (void)close(child);
    if (directory >= 0) (void)close(directory);
    free(path);
    return result;
}

static SolarResult ambiguous_sources(const SolarStringList *candidates)
{
    SolarResult result = cmm_error(
        SOLAR_STATUS_CONFIG_ERROR,
        "keep exactly one .cmm processor source in the project",
        "solar scan found multiple CMM processor sources"
    );
    size_t index;
    size_t used = strlen(result.diagnostic.hint);

    if (used + 2U < sizeof(result.diagnostic.hint)) {
        result.diagnostic.hint[used++] = ':';
        result.diagnostic.hint[used++] = ' ';
        result.diagnostic.hint[used] = '\0';
    }
    for (index = 0U; index < candidates->count; index++) {
        int count = snprintf(
            result.diagnostic.hint + used,
            sizeof(result.diagnostic.hint) - used,
            "%s%s",
            index == 0U ? "" : ", ",
            candidates->items[index]
        );

        if (count < 0 || (size_t)count >= sizeof(result.diagnostic.hint) - used) {
            break;
        }
        used += (size_t)count;
    }
    return result;
}

SolarResult solar_cmm_discover_source(
    const char *project_root,
    const char *configured_source,
    char **source_out
)
{
    SolarStringList candidates;
    char *software = NULL;
    struct stat information;
    SolarResult result;

    if (source_out != NULL) *source_out = NULL;
    if (project_root == NULL || source_out == NULL) {
        return cmm_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "provide a loaded project and result storage",
            "cannot discover a CMM source without a project root"
        );
    }
    solar_string_list_init(&candidates);
    result = solar_filesystem_join(project_root, "software", &software);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (lstat(software, &information) == 0) {
        if (S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode)) {
            result = discover_directory(software, "software", 0U, &candidates);
            if (result.status != SOLAR_STATUS_OK) goto cleanup;
        }
    } else if (errno != ENOENT && errno != ENOTDIR) {
        result = cmm_error(
            SOLAR_STATUS_IO_ERROR,
            "check the software/ directory and try again",
            "cannot inspect the CMM source directory: %s",
            strerror(errno)
        );
        goto cleanup;
    }
    result = add_configured_source(project_root, configured_source, &candidates);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (candidates.count == 0U) {
        result = cmm_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "place one .cmm processor source below software/ and run solar scan again",
            "solar scan found no CMM processor source"
        );
        goto cleanup;
    }
    qsort(
        candidates.items,
        candidates.count,
        sizeof(*candidates.items),
        compare_strings
    );
    if (candidates.count > 1U) {
        result = ambiguous_sources(&candidates);
        goto cleanup;
    }
    *source_out = strdup(candidates.items[0]);
    if (*source_out == NULL) {
        result = cmm_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "free memory and try again",
            "could not copy the discovered CMM source path"
        );
        goto cleanup;
    }
    result = solar_result_ok();

cleanup:
    free(software);
    solar_string_list_free(&candidates);
    return result;
}

static SolarResult read_regular_source(
    const char *path,
    char **text_out,
    size_t *length_out
)
{
    int descriptor = -1;
    struct stat information;
    char *text = NULL;
    size_t offset = 0U;
    SolarResult result = solar_result_ok();

    *text_out = NULL;
    *length_out = 0U;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        information.st_size > CMM_SOURCE_LIMIT) {
        result = cmm_error(
            SOLAR_STATUS_IO_ERROR,
            "use a readable regular CMM file no larger than 16 MiB",
            "cannot safely read CMM source %s",
            path
        );
        goto cleanup;
    }
    text = malloc((size_t)information.st_size + 1U);
    if (text == NULL) {
        result = cmm_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "free memory and try again",
            "could not allocate CMM source text"
        );
        goto cleanup;
    }
    while (offset < (size_t)information.st_size) {
        ssize_t count = read(
            descriptor, text + offset, (size_t)information.st_size - offset
        );

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            result = cmm_error(
                SOLAR_STATUS_IO_ERROR,
                "check the CMM source and filesystem",
                "failed while reading CMM source %s",
                path
            );
            goto cleanup;
        }
        offset += (size_t)count;
    }
    text[offset] = '\0';
    *text_out = text;
    *length_out = offset;
    text = NULL;

cleanup:
    free(text);
    if (descriptor >= 0 && close(descriptor) != 0 &&
        result.status == SOLAR_STATUS_OK) {
        result = cmm_error(
            SOLAR_STATUS_IO_ERROR,
            "check the CMM source filesystem",
            "cannot close CMM source %s",
            path
        );
        free(*text_out);
        *text_out = NULL;
        *length_out = 0U;
    }
    return result;
}

static bool identifier_start(unsigned char character)
{
    return (character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z') || character == '_';
}

static bool identifier_character(unsigned char character)
{
    return identifier_start(character) ||
        (character >= '0' && character <= '9');
}

static SolarResult parse_processor_name(
    const char *path,
    const char *text,
    size_t length,
    char **processor_out
)
{
    const char directive[] = "#PRNAME";
    bool line_start = true;
    bool block_comment = false;
    bool line_comment = false;
    bool string = false;
    bool escaped = false;
    size_t index;

    for (index = 0U; index < length; index++) {
        unsigned char character = (unsigned char)text[index];

        if (line_comment) {
            if (character == '\n') {
                line_comment = false;
                line_start = true;
            }
            continue;
        }
        if (block_comment) {
            if (character == '*' && index + 1U < length &&
                text[index + 1U] == '/') {
                block_comment = false;
                index++;
            } else if (character == '\n') {
                line_start = true;
            }
            continue;
        }
        if (string) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') string = false;
            else if (character == '\n') line_start = true;
            continue;
        }
        if (character == '/' && index + 1U < length && text[index + 1U] == '/') {
            line_comment = true;
            index++;
            continue;
        }
        if (character == '/' && index + 1U < length && text[index + 1U] == '*') {
            block_comment = true;
            index++;
            continue;
        }
        if (character == '"') {
            string = true;
            line_start = false;
            continue;
        }
        if (character == '\n') {
            line_start = true;
            continue;
        }
        if (character == ' ' || character == '\t' || character == '\r') {
            continue;
        }
        if (line_start && index + sizeof(directive) - 1U <= length &&
            memcmp(text + index, directive, sizeof(directive) - 1U) == 0 &&
            (index + sizeof(directive) - 1U == length ||
             !identifier_character((unsigned char)text[index + sizeof(directive) - 1U]))) {
            size_t name_start;
            size_t name_end;
            char *name;

            index += sizeof(directive) - 1U;
            while (index < length &&
                   (text[index] == ' ' || text[index] == '\t')) index++;
            name_start = index;
            if (name_start >= length ||
                !identifier_start((unsigned char)text[name_start])) {
                return cmm_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "use #PRNAME followed by letters, numbers, or underscores",
                    "%s contains a malformed #PRNAME directive",
                    path
                );
            }
            index++;
            while (index < length &&
                   identifier_character((unsigned char)text[index])) index++;
            name_end = index;
            while (index < length &&
                   (text[index] == ' ' || text[index] == '\t' ||
                    text[index] == '\r')) index++;
            if (index < length && text[index] != '\n' &&
                !(text[index] == '/' && index + 1U < length &&
                  (text[index + 1U] == '/' || text[index + 1U] == '*'))) {
                return cmm_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "use one safe processor identifier on the #PRNAME line",
                    "%s contains a malformed #PRNAME directive",
                    path
                );
            }
            if (*processor_out != NULL) {
                return cmm_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "keep exactly one #PRNAME directive in the CMM source",
                    "%s contains duplicate #PRNAME directives",
                    path
                );
            }
            name = strndup(text + name_start, name_end - name_start);
            if (name == NULL) {
                return cmm_error(
                    SOLAR_STATUS_INTERNAL_ERROR,
                    "free memory and try again",
                    "could not allocate the CMM processor name"
                );
            }
            if (!solar_processor_name_is_safe(name)) {
                free(name);
                return cmm_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "use at most 127 letters, numbers, and underscores, beginning with a letter or underscore",
                    "%s contains an unsafe #PRNAME processor name",
                    path
                );
            }
            *processor_out = name;
            if (index > 0U) index--;
            line_start = false;
            continue;
        }
        line_start = false;
    }
    if (*processor_out == NULL) {
        return cmm_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "add #PRNAME processor_name to the CMM source",
            "%s does not declare a CMM processor with #PRNAME",
            path
        );
    }
    return solar_result_ok();
}

SolarResult solar_cmm_read_processor_name(
    const char *source_path,
    char **processor_out
)
{
    char *text = NULL;
    size_t length = 0U;
    SolarResult result;

    if (processor_out != NULL) *processor_out = NULL;
    if (source_path == NULL || processor_out == NULL) {
        return cmm_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "provide a CMM source and result storage",
            "cannot read a processor name from a null CMM source"
        );
    }
    result = read_regular_source(source_path, &text, &length);
    if (result.status == SOLAR_STATUS_OK) {
        result = parse_processor_name(source_path, text, length, processor_out);
    }
    if (result.status != SOLAR_STATUS_OK) {
        free(*processor_out);
        *processor_out = NULL;
    }
    free(text);
    return result;
}
