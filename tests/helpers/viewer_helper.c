#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *record_path = getenv("SOLAR_VIEWER_RECORD");
    const char *record_cwd = getenv("SOLAR_VIEWER_RECORD_CWD");
    char *working_directory = NULL;
    FILE *record;
    int index;

    if (argc < 2 || record_path == NULL) return 64;
    record = fopen(record_path, "w");
    if (record == NULL) return 74;
    for (index = 0; index < argc; index++) {
        if (fprintf(record, "%s\n", argv[index]) < 0) {
            (void)fclose(record);
            return 74;
        }
    }
    if (record_cwd != NULL) {
        working_directory = getcwd(NULL, 0U);
        if (working_directory == NULL ||
            fprintf(record, "%s\n", working_directory) < 0) {
            free(working_directory);
            (void)fclose(record);
            return 74;
        }
    }
    free(working_directory);
    if (fclose(record) != 0) {
        return 74;
    }
    return 0;
}
