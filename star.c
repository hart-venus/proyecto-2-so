#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>

struct Flags {
    bool create;
    bool extract;
    bool list;
    bool delete;
    bool update;
    bool verbose;
    bool veryVerbose;
    bool file;
    bool append;
    bool pack;
    char *outputFile;
    char **inputFiles;
    int numInputFiles;
};

int main(int argc, char *argv[]) {
    struct Flags flags = {false, false, false, false, false, false, false, false, false, false, NULL, NULL, 0};
    int opt;

    static struct option long_options[] = {
        {"create",      no_argument,       0, 'c'},
        {"extract",     no_argument,       0, 'x'},
        {"list",        no_argument,       0, 't'},
        {"delete",      no_argument,       0, 'd'},
        {"update",      no_argument,       0, 'u'},
        {"verbose",     no_argument,       0, 'v'},
        {"file",        no_argument,       0, 'f'},
        {"append",      no_argument,       0, 'r'},
        {"pack",        no_argument,       0, 'p'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "cxtduvwfrp", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                flags.create = true;
                break;
            case 'x':
                flags.extract = true;
                break;
            case 't':
                flags.list = true;
                break;
            case 'd':
                flags.delete = true;
                break;
            case 'u':
                flags.update = true;
                break;
            case 'v':
                if (flags.verbose) {
                    flags.veryVerbose = true;
                }
                flags.verbose = true;
                break;
            case 'f':
                flags.file = true;
                break;
            case 'r':
                flags.append = true;
                break;
            case 'p':
                flags.pack = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-cxtduvvfrp] <outputFile> <inputFile1> ... <inputFileN>\n", argv[0]);
                return 1;
        }
    }

    if (optind < argc) {
        flags.outputFile = argv[optind++];
    }

    flags.numInputFiles = argc - optind;
    if (flags.numInputFiles > 0) {
        flags.inputFiles = &argv[optind];
    }

    // Usar las banderas parseadas
    if (flags.create) {
        printf("--create flag is set\n");
    }
    if (flags.extract) {
        printf("--extract flag is set\n");
    }
    if (flags.list) {
        printf("--list flag is set\n");
    }
    if (flags.delete) {
        printf("--delete flag is set\n");
    }
    if (flags.update) {
        printf("--update flag is set\n");
    }
    if (flags.verbose) {
        printf("--verbose flag is set\n");
    }
    if (flags.veryVerbose) {
        printf("--veryverbose flag is set\n");
    }
    if (flags.file) {
        printf("--file flag is set\n");
    }
    if (flags.append) {
        printf("--append flag is set\n");
    }
    if (flags.pack) {
        printf("--pack flag is set\n");
    }

    if (flags.outputFile != NULL) {
        printf("Output file: %s\n", flags.outputFile);
    }

    if (flags.numInputFiles > 0) {
        printf("Input files:\n");
        for (int i = 0; i < flags.numInputFiles; i++) {
            printf("%s\n", flags.inputFiles[i]);
        }
    }

    return 0;
}
