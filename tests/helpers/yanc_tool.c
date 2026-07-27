#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *base_name(const char *path)
{
    const char *separator = strrchr(path, '/');

    return separator == NULL ? path : separator + 1;
}

static const char *argument_value(int argc, char *argv[], const char *option)
{
    int index;

    for (index = 1; index + 1 < argc; index++) {
        if (strcmp(argv[index], option) == 0) return argv[index + 1];
    }
    return NULL;
}

static bool ensure_directory(const char *path)
{
    char *copy = strdup(path);
    char *cursor;
    bool success = true;

    if (copy == NULL) return false;
    for (cursor = copy + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
            success = false;
            break;
        }
        *cursor = '/';
    }
    if (success && mkdir(copy, 0700) != 0 && errno != EEXIST) success = false;
    free(copy);
    return success;
}

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static bool write_text(const char *path, const char *text)
{
    int descriptor;
    size_t length = strlen(text);
    size_t offset = 0U;

    descriptor = open(
        path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600
    );
    if (descriptor < 0) return false;
    while (offset < length) {
        ssize_t count = write(descriptor, text + offset, length - offset);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            (void)close(descriptor);
            return false;
        }
        offset += (size_t)count;
    }
    return close(descriptor) == 0;
}

static bool write_in_directory(
    const char *directory,
    const char *name,
    const char *text
)
{
    char *path;
    bool success;

    if (!ensure_directory(directory)) return false;
    path = join_path(directory, name);
    if (path == NULL) return false;
    success = write_text(path, text);
    free(path);
    return success;
}

static bool file_contains(const char *path, const char *needle)
{
    char buffer[256];
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t count;

    if (descriptor < 0) return false;
    do {
        count = read(descriptor, buffer, sizeof(buffer) - 1U);
    } while (count < 0 && errno == EINTR);
    (void)close(descriptor);
    if (count <= 0) return false;
    buffer[(size_t)count] = '\0';
    return strstr(buffer, needle) != NULL;
}

static bool record_invocation(const char *stage, int argc, char *argv[])
{
    const char *trace_path = getenv("SOLAR_FAKE_YANC_TRACE");
    int descriptor;
    FILE *trace;
    int index;
    bool success = true;

    if (trace_path == NULL || trace_path[0] == '\0') return true;
    descriptor = open(
        trace_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600
    );
    if (descriptor < 0) return false;
    trace = fdopen(descriptor, "a");
    if (trace == NULL) {
        (void)close(descriptor);
        return false;
    }
    if (fprintf(trace, "%s", stage) < 0) success = false;
    for (index = 1; success && index < argc; index++) {
        if (fprintf(trace, "\t%s", argv[index]) < 0) success = false;
    }
    if (success && fputc('\n', trace) == EOF) success = false;
    if (fclose(trace) != 0) success = false;
    return success;
}

static bool omission_requested(const char *name)
{
    const char *requested = getenv("SOLAR_FAKE_YANC_OMIT");

    return requested != NULL && strcmp(requested, name) == 0;
}

static int requested_failure(const char *stage)
{
    const char *requested = getenv("SOLAR_FAKE_YANC_FAIL_STAGE");
    const char *exit_text = getenv("SOLAR_FAKE_YANC_EXIT_CODE");
    char *end = NULL;
    long code;

    if (requested == NULL || strcmp(requested, stage) != 0) return 0;
    (void)fprintf(stderr, "fake YANC failure from %s\n", stage);
    if (exit_text == NULL) return 23;
    errno = 0;
    code = strtol(exit_text, &end, 10);
    if (errno != 0 || end == exit_text || *end != '\0' ||
        code < 1 || code > 255) return 23;
    return (int)code;
}

static char *processor_from_assembly(const char *assembly)
{
    const char *name = base_name(assembly);
    size_t length = strlen(name);
    char *processor;

    if (length > 4U && strcmp(name + length - 4U, ".asm") == 0) length -= 4U;
    processor = malloc(length + 1U);
    if (processor != NULL) {
        (void)memcpy(processor, name, length);
        processor[length] = '\0';
    }
    return processor;
}

static int run_cpppp(int argc, char *argv[])
{
    const char *output = argument_value(argc, argv, "-o");

    if (output == NULL || omission_requested("preprocessed")) return 0;
    if (!write_text(output, "// fake YANC preprocessed C++\n")) return 74;
    return 0;
}

static int write_generated_assembly(
    int argc,
    char *argv[],
    const char *frontend
)
{
    const char *project = argument_value(argc, argv, "-p");
    const char *processor = argument_value(argc, argv, "-n");
    const char *temporary = argument_value(argc, argv, "-t");
    char *software = NULL;
    char *name = NULL;
    bool success = false;

    if (project == NULL || processor == NULL || temporary == NULL) return 64;
    software = join_path(project, "Software");
    name = malloc(strlen(processor) + 5U);
    if (software == NULL || name == NULL) goto cleanup;
    (void)snprintf(name, strlen(processor) + 5U, "%s.asm", processor);
    if (!omission_requested("assembly") &&
        !write_in_directory(software, name, "; fake YANC assembly\n")) {
        goto cleanup;
    }
    if (!write_in_directory(temporary, "cmm_log.txt", frontend)) goto cleanup;
    success = true;

cleanup:
    free(name);
    free(software);
    return success ? 0 : 74;
}

static int run_appcomp(int argc, char *argv[])
{
    const char *temporary = argument_value(argc, argv, "-t");

    if (temporary == NULL) return 64;
    if (!write_in_directory(temporary, "app_log.txt", "n_ins 1\n")) {
        return 74;
    }
    return 0;
}

static int run_asmcomp(int argc, char *argv[])
{
    const char *project = argument_value(argc, argv, "-p");
    const char *assembly = argument_value(argc, argv, "-i");
    const char *temporary = argument_value(argc, argv, "-t");
    char *processor = NULL;
    char *hardware = NULL;
    char *name = NULL;
    char *compatibility_log = NULL;
    char *program_counter_map = NULL;
    char *rtl_text = NULL;
    char *testbench_text = NULL;
    bool direct_assembly;
    bool success = false;
    size_t length;

    if (project == NULL || assembly == NULL || temporary == NULL) return 64;
    processor = processor_from_assembly(assembly);
    hardware = join_path(project, "Hardware");
    compatibility_log = join_path(temporary, "cmm_log.txt");
    if (processor == NULL || hardware == NULL || compatibility_log == NULL) {
        goto cleanup;
    }
    direct_assembly = file_contains(compatibility_log, "num_ins 1");
    length = strlen(project) + strlen(temporary) + strlen(processor) + 256U;
    rtl_text = malloc(length);
    testbench_text = malloc(length);
    name = malloc(strlen(processor) + 32U);
    if (rtl_text == NULL || testbench_text == NULL || name == NULL) goto cleanup;

    (void)snprintf(
        rtl_text, length,
        "// staged project: %s\n"
        "module %s;\n"
        "  initial $readmemh(\"%s/Hardware/%s_inst.mif\", memory);\n"
        "  reg [7:0] memory [0:0];\n"
        "endmodule\n",
        project, processor, project, processor
    );
    (void)snprintf(
        testbench_text, length,
        "// staged project: %s\nmodule %s_tb; %s dut(); endmodule\n",
        project, processor, processor
    );

    if (!ensure_directory(hardware) || !ensure_directory(temporary)) goto cleanup;
    (void)snprintf(name, strlen(processor) + 32U, "%s.v", processor);
    if (!omission_requested("rtl") &&
        !write_in_directory(hardware, name, rtl_text)) goto cleanup;
    (void)snprintf(name, strlen(processor) + 32U, "%s_data.mif", processor);
    if (!omission_requested("data") &&
        !write_in_directory(hardware, name, "00\n")) goto cleanup;
    (void)snprintf(name, strlen(processor) + 32U, "%s_inst.mif", processor);
    if (!omission_requested("inst") &&
        !write_in_directory(hardware, name, "00\n")) goto cleanup;
    (void)snprintf(name, strlen(processor) + 32U, "%s_tb.v", processor);
    if (!omission_requested("testbench") &&
        !write_in_directory(temporary, name, testbench_text)) goto cleanup;
    (void)snprintf(name, strlen(processor) + 32U, "pc_%s_mem.txt", processor);
    program_counter_map = join_path(temporary, name);
    if (program_counter_map == NULL) goto cleanup;
    if (direct_assembly) {
        if (!file_contains(program_counter_map, "00000000000000000000")) {
            goto cleanup;
        }
    } else if (!omission_requested("pc") &&
               !write_in_directory(temporary, name, "0\n")) {
        goto cleanup;
    }
    if (!omission_requested("translation") &&
        !write_in_directory(
            temporary, "trad_opcode.txt", "0 NOP\n1 LOD value\n"
        )) {
        goto cleanup;
    }
    success = true;

cleanup:
    free(testbench_text);
    free(rtl_text);
    free(program_counter_map);
    free(compatibility_log);
    free(name);
    free(hardware);
    free(processor);
    return success ? 0 : 74;
}

static int run_fake_iverilog(int argc, char *argv[])
{
    const char *output = argument_value(argc, argv, "-o");

    if (output == NULL) return 64;
    return write_text(output, "fake simulation executable\n") ? 0 : 74;
}

static int run_fake_vvp(void)
{
    (void)printf("fake generated test passed\n");
    return write_text("processor_tb.vcd", "fake vcd\n") ? 0 : 74;
}

static int run_fake_yosys(void)
{
    if (!write_text("netlist.v", "module processor; endmodule\n") ||
        !write_text("statistics.txt", "fake synthesis report\n")) {
        return 74;
    }
    return 0;
}

static int run_fake_gen_gtkw(int argc, char *argv[])
{
    char *layout;
    size_t length;

    if (argc != 6 || strcmp(argv[1], "--assembly-only") != 0) return 64;
    length = strlen(argv[2]) + strlen(argv[4]) + strlen(argv[5]) + 128U;
    layout = malloc(length);
    if (layout == NULL) return 74;
    (void)snprintf(
        layout, length,
        "[dumpfile] \"%s\"\n[savefile] \"%s\"\n"
        "^1 %s\n+{Assembly} processor_tb.proc.valr2[15:0]\n",
        argv[2], argv[5], argv[4]
    );
    if (!write_text(argv[3], layout)) {
        free(layout);
        return 74;
    }
    free(layout);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *stage = base_name(argv[0]);
    int failure;

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        (void)printf("%s (YANC) 5.2-fake\n", stage);
        return 0;
    }
    if (!record_invocation(stage, argc, argv)) return 74;
    failure = requested_failure(stage);
    if (failure != 0) return failure;
    if (strcmp(stage, "cpppp") == 0) return run_cpppp(argc, argv);
    if (strcmp(stage, "cmmcomp") == 0) {
        return write_generated_assembly(argc, argv, "fake cmmcomp\n");
    }
    if (strcmp(stage, "cppcomp") == 0) {
        return write_generated_assembly(argc, argv, "fake cppcomp\n");
    }
    if (strcmp(stage, "appcomp") == 0) return run_appcomp(argc, argv);
    if (strcmp(stage, "asmcomp") == 0) return run_asmcomp(argc, argv);
    if (strcmp(stage, "gen_gtkw") == 0) return run_fake_gen_gtkw(argc, argv);
    if (strcmp(stage, "iverilog") == 0) return run_fake_iverilog(argc, argv);
    if (strcmp(stage, "vvp") == 0) return run_fake_vvp();
    if (strcmp(stage, "yosys") == 0) return run_fake_yosys();
    (void)fprintf(stderr, "unknown fake YANC executable: %s\n", stage);
    return 64;
}
