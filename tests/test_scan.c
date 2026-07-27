#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"
#include "solar/project.h"
#include "solar/scan.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *join_path(const char *root, const char *relative)
{
    size_t length = strlen(root) + strlen(relative) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", root, relative);
    return path;
}

static int write_relative(const char *root, const char *relative, const char *text)
{
    char *path = join_path(root, relative);
    FILE *file;
    int failed;

    if (path == NULL) return -1;
    file = fopen(path, "w");
    free(path);
    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static char *read_relative(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    FILE *file;
    long length;
    char *text = NULL;

    if (path == NULL) return NULL;
    file = fopen(path, "r");
    free(path);
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) (void)fclose(file);
        return NULL;
    }
    text = malloc((size_t)length + 1U);
    if (text != NULL && fread(text, 1U, (size_t)length, file) == (size_t)length) {
        text[(size_t)length] = '\0';
    } else {
        free(text);
        text = NULL;
    }
    (void)fclose(file);
    return text;
}

static void remove_relative(const char *root, const char *relative)
{
    char *path = join_path(root, relative);

    if (path != NULL) (void)unlink(path);
    free(path);
}

static int fail(const char *name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", name, message);
    return 1;
}

static void cleanup_v2(const char *root)
{
    const char *files[] = {
        "rtl/design.sv", "rtl/stale.v", "vendor/keep.v",
        "tb/basic.sv", "tb/second.sv", "tb/bad.sv", "solar.toml"
    };
    const char *directories[] = {"rtl", "tb", "vendor"};
    bool removed = false;
    size_t index;

    (void)solar_filesystem_clean_project(root, &removed);
    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        char *path = join_path(root, files[index]);
        if (path != NULL) (void)unlink(path);
        free(path);
    }
    for (index = 0U; index < sizeof(directories) / sizeof(directories[0]); index++) {
        char *path = join_path(root, directories[index]);
        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    (void)rmdir(root);
}

static int make_v2(const char *root)
{
    static const char manifest[] =
        "# outside comment must survive\n"
        "[solar]\nformat = 2\n\n"
        "[project]\nname = \"design\"\nlanguage = \"verilog\"\n"
        "default_test = \"basic\"\n\n"
        "[sources]\nrtl = [\"rtl/stale.v\", \"vendor/keep.v\"]\n\n"
        "[simulation]\nbackend = \"iverilog\"\n\n"
        "[synthesis]\nbackend = \"yosys\"\ntop = \"stale_design\"\n\n"
        "[[test]]\nname = \"basic\"\ntop = \"old_top\"\n"
        "sources = [\"tb/basic.sv\"]\n"
        "include_dirs = [\"tb\"]\ndefines = [\"PRESERVED\"]\n"
        "waveform = \"old.vcd\"\n";
    char *rtl = join_path(root, "rtl");
    char *tb = join_path(root, "tb");
    char *vendor = join_path(root, "vendor");
    int failed = rtl == NULL || tb == NULL || vendor == NULL ||
        mkdir(rtl, 0700) != 0 || mkdir(tb, 0700) != 0 ||
        mkdir(vendor, 0700) != 0;

    free(vendor);
    free(tb);
    free(rtl);
    if (failed) return -1;
    return write_relative(root, "solar.toml", manifest) != 0 ||
        write_relative(root, "rtl/design.sv", "module design; endmodule\n") != 0 ||
        write_relative(root, "vendor/keep.v", "module kept; endmodule\n") != 0 ||
        write_relative(root, "tb/basic.sv",
            "module basic_tb; initial $dumpfile(\"basic.fst\"); endmodule\n") != 0
        ? -1 : 0;
}

static int test_v2_scan(void)
{
    const char *name = "scan v2 synchronization";
    char root_template[] = "/tmp/solar-scan-test-XXXXXX";
    char *root = mkdtemp(root_template);
    char *first = NULL;
    char *second = NULL;
    char *before_failure = NULL;
    char *after_failure = NULL;
    SolarScanResult scan;
    SolarResult result;
    int failed = 0;

    if (root == NULL || make_v2(root) != 0) {
        if (root != NULL) cleanup_v2(root);
        return fail(name, "could not create fixture");
    }
    result = solar_scan_project(root, &scan);
    first = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_OK || first == NULL || !scan.changed ||
        scan.rtl_added != 1U || scan.rtl_removed != 1U ||
        strstr(first, "# outside comment must survive") == NULL ||
        strstr(first, "rtl/design.sv") == NULL ||
        strstr(first, "rtl/stale.v") != NULL ||
        strstr(first, "vendor/keep.v") == NULL ||
        strstr(first, "top = \"design\"") == NULL ||
        strstr(first, "top = \"stale_design\"") != NULL ||
        strstr(first, "name = \"basic\"") == NULL ||
        strstr(first, "top = \"basic_tb\"") == NULL ||
        strstr(first, "PRESERVED") == NULL ||
        strstr(first, "basic.fst") == NULL) {
        failed += fail(name, "managed fields were not synchronized correctly");
        goto cleanup;
    }
    result = solar_scan_project(root, &scan);
    second = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_OK || scan.changed || second == NULL ||
        strcmp(first, second) != 0) {
        failed += fail(name, "second scan was not idempotent");
        goto cleanup;
    }
    if (write_relative(root, "tb/second.sv",
            "module second; initial $dumpfile(\"second.vcd\"); endmodule\n") != 0) {
        failed += fail(name, "could not add a second testbench");
        goto cleanup;
    }
    result = solar_scan_project(root, &scan);
    if (result.status != SOLAR_STATUS_OK || scan.tests_added != 1U ||
        scan.tests_total != 2U) {
        failed += fail(name, "new SystemVerilog testbench was not added");
        goto cleanup;
    }
    before_failure = read_relative(root, "solar.toml");
    if (write_relative(root, "tb/bad.sv",
            "module first; endmodule\nmodule second_bad; endmodule\n") != 0) {
        failed += fail(name, "could not create ambiguous testbench");
        goto cleanup;
    }
    result = solar_scan_project(root, &scan);
    after_failure = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_CONFIG_ERROR || before_failure == NULL ||
        after_failure == NULL || strcmp(before_failure, after_failure) != 0) {
        failed += fail(name, "ambiguous scan modified the manifest");
    }

cleanup:
    free(after_failure);
    free(before_failure);
    free(second);
    free(first);
    cleanup_v2(root);
    return failed;
}

static int test_v1_migration(void)
{
    const char *name = "scan format-1 migration";
    char root_template[] = "/tmp/solar-scan-v1-XXXXXX";
    char *root = mkdtemp(root_template);
    char *rtl = NULL;
    char *tb = NULL;
    char *manifest = NULL;
    bool removed = false;
    SolarScanResult scan;
    SolarResult result;
    int failed = 0;

    if (root == NULL) return fail(name, "mkdtemp failed");
    rtl = join_path(root, "rtl");
    tb = join_path(root, "tb");
    if (rtl == NULL || tb == NULL || mkdir(rtl, 0700) != 0 ||
        mkdir(tb, 0700) != 0 ||
        write_relative(root, "solar.toml",
            "[project]\nname = \"legacy\"\ntop = \"counter\"\n"
            "language = \"verilog\"\n\n[sources]\n"
            "rtl = [\"rtl/counter.v\"]\n"
            "testbench = [\"tb/counter_tb.v\"]\n\n"
            "[simulation]\nbackend = \"iverilog\"\ntop = \"counter_tb\"\n"
            "waveform = \".solar/build/sim/counter.vcd\"\n\n"
            "[synthesis]\nbackend = \"yosys\"\ntop = \"counter\"\n") != 0 ||
        write_relative(root, "rtl/counter.v", "module counter; endmodule\n") != 0 ||
        write_relative(root, "tb/counter_tb.v",
            "module counter_tb; initial $dumpfile(\"counter.vcd\"); endmodule\n") != 0) {
        failed = fail(name, "could not create fixture");
        goto cleanup;
    }
    result = solar_scan_project(root, &scan);
    manifest = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_OK || !scan.migrated_v1 ||
        manifest == NULL || strstr(manifest, "format = 2") == NULL ||
        strstr(manifest, "testbench =") != NULL ||
        strstr(manifest, "name = \"default\"") == NULL) {
        failed = fail(name, "format 1 was not migrated to a valid format 2 manifest");
    }

cleanup:
    (void)solar_filesystem_clean_project(root, &removed);
    remove_relative(root, "rtl/counter.v");
    remove_relative(root, "tb/counter_tb.v");
    remove_relative(root, "solar.toml");
    if (rtl != NULL) (void)rmdir(rtl);
    if (tb != NULL) (void)rmdir(tb);
    (void)rmdir(root);
    free(manifest);
    free(tb);
    free(rtl);
    return failed;
}

static int test_rtl_only_scan(void)
{
    const char *name = "scan RTL-only project";
    char root_template[] = "/tmp/solar-scan-rtl-only-XXXXXX";
    char *root = mkdtemp(root_template);
    char *manifest = NULL;
    char *second = NULL;
    SolarProject project;
    SolarScanResult scan;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    if (root == NULL) return fail(name, "mkdtemp failed");
    {
        char *rtl = join_path(root, "rtl");
        char *tb = join_path(root, "tb");
        int fixture_failed = rtl == NULL || tb == NULL ||
            mkdir(rtl, 0700) != 0 || mkdir(tb, 0700) != 0 ||
            write_relative(root, "solar.toml",
                "[solar]\nformat = 2\n\n"
                "[project]\nname = \"rtl-only\"\nlanguage = \"verilog\"\n"
                "default_test = \"basic\"\n\n"
                "[simulation]\nbackend = \"iverilog\"\n\n"
                "[synthesis]\nbackend = \"yosys\"\n") != 0 ||
            write_relative(root, "rtl/design.sv",
                "module design; endmodule\n") != 0 ||
            write_relative(root, "tb/basic.sv",
                "module basic; initial $finish; endmodule\n") != 0;

        free(tb);
        free(rtl);
        if (fixture_failed) {
            cleanup_v2(root);
            return fail(name, "could not create fixture");
        }
    }
    remove_relative(root, "tb/basic.sv");
    result = solar_scan_project(root, &scan);
    manifest = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_OK || manifest == NULL || !scan.changed ||
        scan.tests_total != 0U ||
        strstr(manifest, "rtl/design.sv") == NULL ||
        strstr(manifest, "default_test") != NULL ||
        strstr(manifest, "[[test]]") != NULL) {
        failed += fail(name, "scan did not persist the RTL-only inventory");
        goto cleanup;
    }
    result = solar_project_load(root, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    if (result.status != SOLAR_STATUS_OK || project.config.test_count != 0U) {
        failed += fail(name, "the synchronized RTL-only project is invalid");
        goto cleanup;
    }
    result = solar_scan_project(root, &scan);
    second = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_OK || scan.changed || second == NULL ||
        strcmp(manifest, second) != 0) {
        failed += fail(name, "RTL-only scan is not idempotent");
    }

cleanup:
    solar_project_free(&project);
    free(second);
    free(manifest);
    cleanup_v2(root);
    return failed;
}

static int test_ambiguous_synthesis_scan(void)
{
    const char *name = "scan ambiguous synthesis hierarchy";
    char root_template[] = "/tmp/solar-scan-ambiguous-XXXXXX";
    char *root = mkdtemp(root_template);
    char *before = NULL;
    char *after = NULL;
    char *rtl = NULL;
    SolarScanResult scan;
    SolarResult result;
    int failed = 0;

    if (root == NULL) return fail(name, "mkdtemp failed");
    rtl = join_path(root, "rtl");
    if (rtl == NULL || mkdir(rtl, 0700) != 0 ||
        write_relative(root, "solar.toml",
            "[solar]\nformat = 2\n\n"
            "[project]\nname = \"board\"\nlanguage = \"verilog\"\n\n"
            "[simulation]\nbackend = \"iverilog\"\n\n"
            "[synthesis]\nbackend = \"yosys\"\ntop = \"old_top\"\n") != 0 ||
        write_relative(root, "rtl/alpha.v",
            "module alpha; endmodule\n") != 0 ||
        write_relative(root, "rtl/beta.v",
            "module beta; endmodule\n") != 0) {
        failed += fail(name, "could not create fixture");
        goto cleanup;
    }
    before = read_relative(root, "solar.toml");
    result = solar_scan_project(root, &scan);
    after = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_CONFIG_ERROR || before == NULL ||
        after == NULL || strcmp(before, after) != 0 ||
        strstr(result.diagnostic.hint, "alpha") == NULL ||
        strstr(result.diagnostic.hint, "beta") == NULL) {
        failed += fail(name, "ambiguous roots did not preserve the manifest");
    }

cleanup:
    remove_relative(root, "rtl/alpha.v");
    remove_relative(root, "rtl/beta.v");
    remove_relative(root, "solar.toml");
    if (rtl != NULL) (void)rmdir(rtl);
    (void)rmdir(root);
    free(rtl);
    free(after);
    free(before);
    return failed;
}

static const char CMM_MANIFEST[] =
    "# preserved scan comment\n"
    "[solar]\nformat = 2\n\n"
    "[project]\nname = \"stale-project\"\nlanguage = \"cmm\"\n"
    "default_test = \"generated\"\ndefault_profile = \"debug\"\n\n"
    "[compiler]\nbackend = \"yanc\"\n"
    "source = \"software/source files/new processor.cmm\"\n"
    "processor = \"stale_cpu\"\n"
    "frequency_mhz = 100\nsimulation_clocks = 1000\n\n"
    "[yanc]\ndiagnostics = \"pt\"\n\n"
    "[simulation]\nbackend = \"iverilog\"\n\n"
    "[synthesis]\nbackend = \"yosys\"\ntop = \"stale_cpu\"\n\n"
    "[[profile]]\nname = \"debug\"\ndefines = [\"KEEP_ME\"]\n";

static void cleanup_cmm_project(
    const char *root,
    const char *const *files,
    size_t file_count,
    const char *const *directories,
    size_t directory_count
)
{
    bool removed = false;
    size_t index;

    (void)solar_filesystem_clean_project(root, &removed);
    for (index = 0U; index < file_count; index++) {
        remove_relative(root, files[index]);
    }
    remove_relative(root, "solar.toml");
    for (index = 0U; index < directory_count; index++) {
        char *path = join_path(root, directories[index]);

        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    (void)rmdir(root);
}

static int test_cmm_scan(void)
{
    const char *name = "scan CMM source and processor identity";
    const char *files[] = {"software/source files/new processor.cmm"};
    const char *directories[] = {"software/source files", "software"};
    char root_template[] = "/tmp/solar-scan-cmm-XXXXXX";
    char *root = mkdtemp(root_template);
    char *software = NULL;
    char *nested = NULL;
    char *manifest = NULL;
    char *second = NULL;
    SolarProject project;
    SolarScanResult scan;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    solar_scan_result_init(&scan);
    if (root == NULL) return fail(name, "mkdtemp failed");
    software = join_path(root, "software");
    nested = join_path(root, "software/source files");
    if (software == NULL || nested == NULL || mkdir(software, 0700) != 0 ||
        mkdir(nested, 0700) != 0 ||
        write_relative(root, "solar.toml", CMM_MANIFEST) != 0 ||
        write_relative(
            root,
            files[0],
            "/* #PRNAME ignored_block */\n"
            "#define MESSAGE \"#PRNAME ignored_string\"\n"
            "#PRNAME fresh_cpu // active identity\n"
            "void main() {}\n"
        ) != 0) {
        failed = fail(name, "could not create fixture");
        goto cleanup;
    }
    result = solar_project_load(root, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.hint, "solar scan") == NULL) {
        failed += fail(name, "stale CMM identity was not diagnosed");
        goto cleanup;
    }
    solar_project_free(&project);
    result = solar_scan_project(root, &scan);
    manifest = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_OK || scan.kind != SOLAR_SCAN_CMM ||
        !scan.changed || scan.compiler_source == NULL || scan.processor == NULL ||
        strcmp(scan.compiler_source, files[0]) != 0 ||
        strcmp(scan.processor, "fresh_cpu") != 0 || manifest == NULL ||
        strstr(manifest, "name = \"fresh_cpu\"") == NULL ||
        strstr(manifest,
               "source = \"software/source files/new processor.cmm\"") == NULL ||
        strstr(manifest, "processor = \"fresh_cpu\"") == NULL ||
        strstr(manifest, "top = \"fresh_cpu\"") == NULL ||
        strstr(manifest, "defines = [\"KEEP_ME\"]") == NULL ||
        strstr(manifest, "# preserved scan comment") == NULL) {
        failed += fail(name, "CMM manifest was not synchronized correctly");
        goto cleanup;
    }
    solar_scan_result_free(&scan);
    result = solar_project_load(root, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    if (result.status != SOLAR_STATUS_OK ||
        strcmp(project.config.project.name, "fresh_cpu") != 0 ||
        strcmp(project.config.compiler.processor, "fresh_cpu") != 0 ||
        strcmp(project.config.synthesis.top, "fresh_cpu") != 0 ||
        project.config.test_count != 1U ||
        strcmp(project.config.tests[0].top, "fresh_cpu_tb") != 0) {
        failed += fail(name, "synchronized CMM project is not valid");
        goto cleanup;
    }
    solar_project_free(&project);
    result = solar_scan_project(root, &scan);
    second = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_OK || scan.changed || second == NULL ||
        strcmp(manifest, second) != 0) {
        failed += fail(name, "CMM scan is not idempotent");
    }

cleanup:
    solar_scan_result_free(&scan);
    solar_project_free(&project);
    free(second);
    free(manifest);
    free(nested);
    free(software);
    cleanup_cmm_project(
        root, files, sizeof(files) / sizeof(files[0]),
        directories, sizeof(directories) / sizeof(directories[0])
    );
    return failed;
}

static int test_cmm_scan_multiple_sources(void)
{
    const char *name = "scan rejects multiple CMM sources";
    const char *files[] = {"software/alpha.cmm", "software/beta.cmm"};
    const char *directories[] = {"software"};
    char root_template[] = "/tmp/solar-scan-cmm-many-XXXXXX";
    char *root = mkdtemp(root_template);
    char *software = NULL;
    char *before = NULL;
    char *after = NULL;
    SolarScanResult scan;
    SolarResult result;
    int failed = 0;

    solar_scan_result_init(&scan);
    if (root == NULL) return fail(name, "mkdtemp failed");
    software = join_path(root, "software");
    if (software == NULL || mkdir(software, 0700) != 0 ||
        write_relative(root, "solar.toml", CMM_MANIFEST) != 0 ||
        write_relative(root, files[0], "#PRNAME alpha\nvoid main() {}\n") != 0 ||
        write_relative(root, files[1], "#PRNAME beta\nvoid main() {}\n") != 0) {
        failed = fail(name, "could not create fixture");
        goto cleanup;
    }
    before = read_relative(root, "solar.toml");
    result = solar_scan_project(root, &scan);
    after = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_CONFIG_ERROR || before == NULL ||
        after == NULL || strcmp(before, after) != 0 ||
        strstr(result.diagnostic.hint, "software/alpha.cmm") == NULL ||
        strstr(result.diagnostic.hint, "software/beta.cmm") == NULL) {
        failed += fail(name, "ambiguous CMM scan modified the manifest");
    }

cleanup:
    solar_scan_result_free(&scan);
    free(after);
    free(before);
    free(software);
    cleanup_cmm_project(
        root, files, sizeof(files) / sizeof(files[0]),
        directories, sizeof(directories) / sizeof(directories[0])
    );
    return failed;
}

static int test_cmm_scan_invalid_directives(void)
{
    const char *name = "scan rejects invalid CMM processor directives";
    const char *files[] = {"software/processor.cmm"};
    const char *directories[] = {"software"};
    const char *invalid_sources[] = {
        "// #PRNAME comment_only\nvoid main() {}\n",
        "#PRNAME first\n#PRNAME second\nvoid main() {}\n",
        "#PRNAME unsafe-name\nvoid main() {}\n"
    };
    size_t case_index;
    int failed = 0;

    for (case_index = 0U;
         case_index < sizeof(invalid_sources) / sizeof(invalid_sources[0]);
         case_index++) {
        char root_template[] = "/tmp/solar-scan-cmm-invalid-XXXXXX";
        char *root = mkdtemp(root_template);
        char *software = NULL;
        char *before = NULL;
        char *after = NULL;
        SolarScanResult scan;
        SolarResult result;

        solar_scan_result_init(&scan);
        if (root == NULL) {
            failed += fail(name, "mkdtemp failed");
            continue;
        }
        software = join_path(root, "software");
        if (software == NULL || mkdir(software, 0700) != 0 ||
            write_relative(root, "solar.toml", CMM_MANIFEST) != 0 ||
            write_relative(root, files[0], invalid_sources[case_index]) != 0) {
            failed += fail(name, "could not create invalid fixture");
        } else {
            before = read_relative(root, "solar.toml");
            result = solar_scan_project(root, &scan);
            after = read_relative(root, "solar.toml");
            if (result.status != SOLAR_STATUS_CONFIG_ERROR || before == NULL ||
                after == NULL || strcmp(before, after) != 0) {
                failed += fail(name, "invalid directive modified the manifest");
            }
        }
        solar_scan_result_free(&scan);
        free(after);
        free(before);
        free(software);
        cleanup_cmm_project(
            root, files, sizeof(files) / sizeof(files[0]),
            directories, sizeof(directories) / sizeof(directories[0])
        );
    }
    return failed;
}

static int test_cmm_scan_ignores_symlink(void)
{
    const char *name = "scan ignores symbolic CMM sources";
    const char *files[] = {"software/linked.cmm", "outside.cmm"};
    const char *directories[] = {"software"};
    char root_template[] = "/tmp/solar-scan-cmm-link-XXXXXX";
    char *root = mkdtemp(root_template);
    char *software = NULL;
    char *link_path = NULL;
    char *before = NULL;
    char *after = NULL;
    SolarScanResult scan;
    SolarResult result;
    int failed = 0;

    solar_scan_result_init(&scan);
    if (root == NULL) return fail(name, "mkdtemp failed");
    software = join_path(root, "software");
    link_path = join_path(root, files[0]);
    if (software == NULL || link_path == NULL || mkdir(software, 0700) != 0 ||
        write_relative(root, "solar.toml", CMM_MANIFEST) != 0 ||
        write_relative(root, files[1], "#PRNAME outside\nvoid main() {}\n") != 0 ||
        symlink("../outside.cmm", link_path) != 0) {
        failed = fail(name, "could not create symlink fixture");
        goto cleanup;
    }
    before = read_relative(root, "solar.toml");
    result = solar_scan_project(root, &scan);
    after = read_relative(root, "solar.toml");
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "no CMM") == NULL || before == NULL ||
        after == NULL || strcmp(before, after) != 0) {
        failed += fail(name, "symlink source was not ignored safely");
    }

cleanup:
    solar_scan_result_free(&scan);
    free(after);
    free(before);
    free(link_path);
    free(software);
    cleanup_cmm_project(
        root, files, sizeof(files) / sizeof(files[0]),
        directories, sizeof(directories) / sizeof(directories[0])
    );
    return failed;
}

int main(void)
{
    int failures = 0;

    failures += test_v2_scan();
    failures += test_v1_migration();
    failures += test_rtl_only_scan();
    failures += test_ambiguous_synthesis_scan();
    failures += test_cmm_scan();
    failures += test_cmm_scan_multiple_sources();
    failures += test_cmm_scan_invalid_directives();
    failures += test_cmm_scan_ignores_symlink();
    return failures == 0 ? 0 : 1;
}
