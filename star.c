#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>

struct Flags {
    bool word;
    bool a;
    bool b;
    bool c;
};

int main(int argc, char *argv[]) {
    struct Flags flags = {false, false, false, false};
    int opt;

    static struct option long_options[] = {
        {"word", no_argument, 0, 'w'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "abc", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                flags.a = true;
                break;
            case 'b':
                flags.b = true;
                break;
            case 'c':
                flags.c = true;
                break;
            case 'w':
                flags.word = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-abc] [--word]\n", argv[0]);
                return 1;
        }
    }

    // Use the parsed flags
    if (flags.word) {
        printf("--word flag is set\n");
    }
    if (flags.a) {
        printf("-a flag is set\n");
    }
    if (flags.b) {
        printf("-b flag is set\n");
    }
    if (flags.c) {
        printf("-c flag is set\n");
    }

    return 0;
}
