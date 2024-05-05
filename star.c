#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 256 * 1024 // 256 KB
#define MAX_FILES 100 
#define MAX_FILENAME_LENGTH 256
#define MAX_BLOCKS_PER_FILE 64 
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

size_t find_free_block(FAT *fat) {
    for (size_t i = 0; i < fat->num_free_blocks; i++) {
        // NOTA: num_free_blocks solo implica el rango de bloques libres, no necesariamente que todos los bloques estén libres
        if (fat->free_blocks[i] != 0) {
            size_t free_block = fat->free_blocks[i]; // obtener el bloque libre
            fat->free_blocks[i] = 0; // marcar el bloque como ocupado
            return free_block;
        }
    }
    return (size_t)-1; // si no se ha encontrado ningun bloque libre
}

void expand_archive(FILE *archive, FAT *fat) {
    fseek(archive, 0, SEEK_END); // mover el puntero al final del archivo
    size_t current_size = ftell(archive); // obtener la posición actual del puntero (tamaño del archivo)
    size_t expanded_size = current_size + BLOCK_SIZE; // tamaño del archivo expandido (+256KB)
    ftruncate(fileno(archive), expanded_size); // expandir el archivo
    fat->free_blocks[fat->num_free_blocks++] = current_size; 
    // meter el bloque libre a la lista secuencial de bloques libres 
}

void write_block(FILE *archive, Block *block, size_t position) {
    fseek(archive, position, SEEK_SET); // conseguir la posicion marcada por el indice del bloque libre
    fwrite(block, sizeof(Block), 1, archive); // escribir los 256KB del bloque en el archivo
}

void update_fat(FAT *fat, const char *filename, size_t file_size, size_t block_position) {
    for (size_t i = 0; i < fat->num_files; i++) { // por cada archivo en el FAT
        if (strcmp(fat->files[i].filename, filename) == 0) { // si el archivo ya esta en el FAT
            fat->files[i].block_positions[fat->files[i].num_blocks++] = block_position;  // añadir la nueva posicion del bloque al archivo
            fat->files[i].file_size += sizeof(Block); // incrementar el tamaño del archivo
            return; // salir
        }
    }

    // si no hay una entrada para el archivo en el FAT
    FileEntry new_entry;
    strncpy(new_entry.filename, filename, MAX_FILENAME_LENGTH); // copiar el nombre del archivo a la nueva entrada
    new_entry.file_size = file_size + sizeof(Block); // tamaño del archivo
    new_entry.block_positions[0] = block_position; // posición del bloque
    new_entry.num_blocks = 1; // número de bloques
    fat->files[fat->num_files++] = new_entry; // añadir la nueva entrada al FAT
}

void write_fat(FILE *archive, FAT *fat) {
    fseek(archive, 0, SEEK_SET); // mover el puntero al inicio del archivo
    fwrite(fat, sizeof(FAT), 1, archive); // escribir la FAT en el archivo en la posición 0
    // NOTA: implica que los indices de los bloques libres y bloques ocupados son despues de la FAT
}


void create_archive(struct Flags flags) {
    if (flags.verbose) printf("Creando archivo %s\n", flags.outputFile);
    FILE *archive = fopen(flags.outputFile, "wb"); // abrir archivo como binario para escritura

    if (archive == NULL) {
        fprintf(stderr, "Error al abrir el archivo %s\n", flags.outputFile);
        exit(1);
    }

    FAT fat; 
    memset(&fat, 0, sizeof(FAT)); // inicializa FAT con 0s 

    fat.free_blocks[0] = sizeof(FAT); // el primer bloque libre es despues de la FAT
    fat.num_free_blocks = 1; // solo hay un bloque libre

    fwrite(&fat, sizeof(FAT), 1, archive); // escribir la FAT en el archivo (posición 0

    if (flags.file && flags.numInputFiles > 0) {
        // si se me pasan archivos
        for (int i = 0; i < flags.numInputFiles; i++) {
            FILE *input_file = fopen(flags.inputFiles[i], "rb"); // abrir archivo como binario para lectura
            if (input_file == NULL) {
                fprintf(stderr, "Error al abrir el archivo %s\n", flags.inputFiles[i]);
                exit(1);
            }

            if (flags.verbose) printf("Agregando archivo %s\n", flags.inputFiles[i]);
            size_t file_size = 0; 
            size_t block_count = 0; 
            Block block; 
            size_t bytes_read; 

            while ((bytes_read = fread(&block, 1, sizeof(Block), input_file)) > 0) {
                // mientras que pueda leer un bloque del archivo
                size_t block_position = find_free_block(&fat); // indice del bloque libre
                if (block_position == (size_t)-1) {
                    // si no hay bloques libres 
                    if (flags.veryVerbose) {
                        printf("No hay bloques libres, expandiendo el archivo\n");
                    }
                    expand_archive(archive, &fat); // expandir el archivo
                    block_position = find_free_block(&fat); 
                    if (flags.veryVerbose) {
                        printf("Nuevo bloque libre en la posición %zu\n", block_position);
                    }
                }

                if (bytes_read < sizeof(Block)) {
                    // si no se lee un bloque completo
                    memset((char*)&block + bytes_read, 0, sizeof(Block) - bytes_read); // rellenar con 0s
                }

                write_block(archive, &block, block_position); // escribir el bloque en el archivo
                update_fat(&fat, flags.inputFiles[i], file_size, block_position); // actualizar la FAT para que refleje el nuevo bloque

                file_size += sizeof(Block);
                block_count++;

                if (flags.veryVerbose) {
                    printf("Escribiendo bloque %zu para archivo %s\n", block_position, flags.inputFiles[i]);
                }
            }

            if (flags.verbose) printf("Tamaño del archivo %s: %zu bytes\n", flags.inputFiles[i], file_size);

            fclose(input_file);
        }
    } else {
        if (flags.verbose) {
            printf("Leyendo datos desde la entrada estándar (stdin)\n");
        }

        size_t file_size = 0;
        size_t block_count = 0;
        Block block;
        size_t bytes_read;
        while ((bytes_read = fread(&block, 1, sizeof(Block), stdin)) > 0) {
            size_t block_position = find_free_block(&fat);
            if (block_position == (size_t)-1) {
                expand_archive(archive, &fat);
                block_position = find_free_block(&fat);
            }

            if (bytes_read < sizeof(Block)) {
                memset((char*)&block + bytes_read, 0, sizeof(Block) - bytes_read);
            }

            write_block(archive, &block, block_position);
            update_fat(&fat, "stdin", file_size, block_position);

            file_size += sizeof(Block);
            block_count++;

            if (flags.veryVerbose) {
                printf("Bloque %zu leído desde stdin y escrito en la posición %zu\n", block_count, block_position);
            }
        }
    }

    write_fat(archive, &fat);
    fclose(archive);
}

void extract_archive(const char *archive_name, bool verbose, bool very_verbose) {
    FILE *archive = fopen(archive_name, "rb");
    if (archive == NULL) {
        fprintf(stderr, "Error al abrir el archivo empacado.\n");
        return;
    }

    FAT fat;
    fread(&fat, sizeof(FAT), 1, archive);

    for (size_t i = 0; i < fat.num_files; i++) {
        FileEntry entry = fat.files[i];
        FILE *output_file = fopen(entry.filename, "wb");
        if (output_file == NULL) {
            fprintf(stderr, "Error al crear el archivo de salida: %s\n", entry.filename);
            continue;
        }

        if (verbose) {
            printf("Extrayendo archivo: %s\n", entry.filename);
        }

        size_t file_size = 0;
        for (size_t j = 0; j < entry.num_blocks; j++) {
            Block block;
            fseek(archive, entry.block_positions[j], SEEK_SET);
            fread(&block, sizeof(Block), 1, archive);

            size_t bytes_to_write = (file_size + sizeof(Block) <= entry.file_size) ? sizeof(Block) : entry.file_size - file_size;
            fwrite(&block, 1, bytes_to_write, output_file);

            file_size += bytes_to_write;

            if (very_verbose) {
                printf("Bloque %zu del archivo %s extraído de la posición %zu\n", j + 1, entry.filename, entry.block_positions[j]);
            }
        }

        fclose(output_file);
    }

    fclose(archive);
}



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

    if (flags.create) create_archive(flags); 
    else if (flags.extract) extract_archive(flags.outputFile, flags.verbose, flags.veryVerbose);

    return 0;
}

