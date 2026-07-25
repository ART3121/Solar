#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *program_name(const char *path)
{
    const char *separator = strrchr(path, '/');

    return separator == NULL ? path : separator + 1;
}

static int write_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(content, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int record_arguments(const char *tool, int argc, char **argv)
{
    const char *record_path = getenv("SOLAR_BACKEND_RECORD");
    FILE *record;
    int index;

    if (record_path == NULL) return -1;
    record = fopen(record_path, "a");
    if (record == NULL) return -1;
    if (fputs(tool, record) == EOF) {
        (void)fclose(record);
        return -1;
    }
    for (index = 1; index < argc; index++) {
        if (fprintf(record, "\t%s", argv[index]) < 0) {
            (void)fclose(record);
            return -1;
        }
    }
    {
        int failed = fputc('\n', record) == EOF;
        if (fclose(record) != 0) failed = 1;
        return failed ? -1 : 0;
    }
}

static int run_iverilog(int argc, char **argv)
{
    int index;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-tnull") == 0) return 0;
    }
    for (index = 1; index + 1 < argc; index++) {
        if (strcmp(argv[index], "-o") == 0) {
            return write_file(argv[index + 1], "fake vvp image\n");
        }
    }
    return 65;
}

int main(int argc, char **argv)
{
    const char *tool = program_name(argv[0]);

    if (record_arguments(tool, argc, argv) != 0) return 74;
    if (getenv("SOLAR_BACKEND_CHATTER") != NULL) {
        (void)fprintf(stdout, "%s verbose stdout\n", tool);
        (void)fprintf(stderr, "%s verbose stderr\n", tool);
    }
    if (strcmp(tool, "iverilog") == 0) return run_iverilog(argc, argv);
    if (strcmp(tool, "vvp") == 0) {
        const char *failing_test = getenv("SOLAR_BACKEND_FAIL_TEST");
        char *working_directory = getcwd(NULL, 0);
        bool named_test = working_directory != NULL &&
            strstr(working_directory, "/.solar/tmp/tests/basic") != NULL;
        const char *waveform = named_test
            ? "basic.vcd"
            : "basic.vcd";
        const char *test_name = working_directory == NULL
            ? "" : program_name(working_directory);
        int status = failing_test != NULL &&
            strcmp(failing_test, test_name) == 0
            ? 67
            : (getenv("SOLAR_BACKEND_NO_WAVEFORM") != NULL
                ? 0
                : (write_file(waveform, "fake vcd\n") == 0 ? 0 : 74));

        if (status == 67) {
            (void)fprintf(stderr, "intentional simulation failure\n");
        }

        free(working_directory);
        return status;
    }
    if (strcmp(tool, "yosys") == 0) {
        char *working_directory = getcwd(NULL, 0);
        bool structured = working_directory != NULL &&
            strstr(working_directory, "/.solar/tmp/synth") != NULL;
        const char *netlist = structured
            ? "netlist.v"
            : ".solar/tmp/synth/netlist.v";
        const char *report = structured
            ? "statistics.txt"
            : ".solar/tmp/synth/statistics.txt";
        const char *statistics = getenv("SOLAR_BACKEND_BAD_STATISTICS") == NULL
            ? "=== counter ===\n"
              " Number of wires: 3\n"
              " Number of wire bits: 10\n"
              " Number of public wires: 2\n"
              " Number of public wire bits: 9\n"
              " Number of memories: 0\n"
              " Number of memory bits: 0\n"
              " Number of processes: 0\n"
              " Number of cells: 2\n"
              "   $add 1\n"
              "   $dff 1\n"
            : "Yosys returned no recognizable stat section\n";

        free(working_directory);
        (void)printf("Yosys 0.test (fake backend)\n");
        if (write_file(netlist, "module counter; endmodule\n") != 0 ||
            write_file(report, statistics) != 0) {
            return 74;
        }
        return 0;
    }
    return 64;
}
