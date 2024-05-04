#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 256 * 1024 // 256 KB
#define MAX_FILES 100 
#define MAX_FILENAME_LENGTH 256
#define MAX_BLOCKS_PER_FILE 1024
#define MAX_BLOCKS MAX_BLOCKS_PER_FILE * MAX_FILES

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    size_t file_size;
    size_t block_positions[MAX_BLOCKS_PER_FILE];
    size_t num_blocks; 
} FileEntry;

typedef struct {
    FileEntry files[MAX_FILES];
    size_t num_files;
    size_t free_blocks[MAX_BLOCKS];
    size_t num_free_blocks;
} FAT;

typedef struct {
    unsigned char data[BLOCK_SIZE];
} Block; 


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


    return 0;
}

