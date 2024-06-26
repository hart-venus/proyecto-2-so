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

void list_archive_contents(const char *archive_name, bool verbose) {
    FILE *archive = fopen(archive_name, "rb");
    if (archive == NULL) {
        fprintf(stderr, "Error al abrir el archivo empacado.\n");
        return;
    }

    FAT fat;
    fread(&fat, sizeof(FAT), 1, archive);

    printf("Contenido del archivo empacado:\n");
    printf("-------------------------------\n");

    for (size_t i = 0; i < fat.num_files; i++) {
        FileEntry entry = fat.files[i];
        printf("%s\t%zu bytes\n", entry.filename, entry.file_size);

        if (verbose) {
            printf("  Bloques: ");
            for (size_t j = 0; j < entry.num_blocks; j++) {
                printf("%zu ", entry.block_positions[j]);
            }
            printf("\n");
        }
    }

    fclose(archive);
}


void write_block(FILE *archive, Block *block, size_t position) {
    fseek(archive, position, SEEK_SET); // conseguir la posicion marcada por el indice del bloque libre
    fwrite(block, sizeof(Block), 1, archive); // escribir los 256KB del bloque en el archivo
}

void update_fat(FAT *fat, const char *filename, size_t file_size, size_t block_position, size_t bytes_read) {
    for (size_t i = 0; i < fat->num_files; i++) { // por cada archivo en el FAT
        if (strcmp(fat->files[i].filename, filename) == 0) { // si el archivo ya esta en el FAT
            fat->files[i].block_positions[fat->files[i].num_blocks++] = block_position;  // añadir la nueva posicion del bloque al archivo
            fat->files[i].file_size += bytes_read; // incrementar el tamaño del archivo
            return; // salir
        }
    }

    // si no hay una entrada para el archivo en el FAT
    FileEntry new_entry;
    strncpy(new_entry.filename, filename, MAX_FILENAME_LENGTH); // copiar el nombre del archivo a la nueva entrada
    new_entry.file_size = file_size + bytes_read; // tamaño del archivo
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
                update_fat(&fat, flags.inputFiles[i], file_size, block_position, bytes_read); // actualizar la FAT para que refleje el nuevo bloque

                file_size += bytes_read;
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
            update_fat(&fat, "stdin", file_size, block_position, bytes_read);

            bytes_read += sizeof(Block);
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

            size_t bytes_to_write = (file_size + sizeof(Block) > entry.file_size) ? entry.file_size - file_size : sizeof(Block);
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

void delete_files_from_archive(const char *archive_name, char **filenames, int num_files, bool verbose, bool very_verbose) {
    FILE *archive = fopen(archive_name, "rb+");
    if (archive == NULL) {
        fprintf(stderr, "Error al abrir el archivo empacado.\n");
        return;
    }

    FAT fat;
    fread(&fat, sizeof(FAT), 1, archive); // leer la FAT del archivo

    for (int i = 0; i < num_files; i++) {
        const char *filename = filenames[i]; // conseguir el nombre del archivo a borrar
        bool file_found = false;

        for (size_t j = 0; j < fat.num_files; j++) {
            if (strcmp(fat.files[j].filename, filename) == 0) { // encontre el archivo
                file_found = true;

                // Marcar los bloques como libres
                for (size_t k = 0; k < fat.files[j].num_blocks; k++) {
                    fat.free_blocks[fat.num_free_blocks++] = fat.files[j].block_positions[k];
                    if (very_verbose) {
                        printf("Bloque %zu del archivo '%s' marcado como libre.\n", fat.files[j].block_positions[k], filename);
                    }
                }

                // Eliminar la entrada del archivo del FAT
                for (size_t k = j; k < fat.num_files - 1; k++) {
                    fat.files[k] = fat.files[k + 1];
                }
                fat.num_files--;

                if (verbose) {
                    printf("Archivo '%s' eliminado del archivo empacado.\n", filename);
                }

                break;
            }
        }

        if (!file_found) {
            fprintf(stderr, "Archivo '%s' no encontrado en el archivo empacado.\n", filename);
        }
    }

    // Escribir la estructura FAT actualizada en el archivo
    fseek(archive, 0, SEEK_SET);
    fwrite(&fat, sizeof(FAT), 1, archive);

    fclose(archive);
}

void update_files_in_archive(const char *archive_name, char **filenames, int num_files, bool verbose, bool very_verbose) {
    FILE *archive = fopen(archive_name, "rb+");
    if (archive == NULL) {
        fprintf(stderr, "Error al abrir el archivo empacado.\n");
        return;
    }

    FAT fat;
    fread(&fat, sizeof(FAT), 1, archive);


    for (int i = 0; i < num_files; i++) {
        const char *filename = filenames[i];
        bool file_found = false;

        for (size_t j = 0; j < fat.num_files; j++) {
            if (strcmp(fat.files[j].filename, filename) == 0) {
                file_found = true;

                // Marcar los bloques anteriores como libres
                for (size_t k = 0; k < fat.files[j].num_blocks; k++) {
                    fat.free_blocks[fat.num_free_blocks++] = fat.files[j].block_positions[k];
                    if (very_verbose) {
                        printf("Bloque %zu del archivo '%s' marcado como libre.\n", fat.files[j].block_positions[k], filename);
                    }
                }

                // Leer el contenido actualizado del archivo
                FILE *input_file = fopen(filename, "rb");
                if (input_file == NULL) {
                    fprintf(stderr, "Error al abrir el archivo de entrada: %s\n", filename);
                    continue;
                }

                size_t file_size = 0;
                size_t block_count = 0;
                Block block;
                size_t bytes_read;
                while ((bytes_read = fread(&block, 1, sizeof(Block), input_file)) > 0) {
                    size_t block_position = find_free_block(&fat);
                    if (block_position == (size_t)-1) {
                        expand_archive(archive, &fat);
                        block_position = find_free_block(&fat);
                    }

                    write_block(archive, &block, block_position);
                    fat.files[j].block_positions[block_count++] = block_position;

                    file_size += bytes_read;

                    if (very_verbose) {
                        printf("Bloque %zu del archivo '%s' actualizado en la posición %zu\n", block_count, filename, block_position);
                    }
                }

                fat.files[j].file_size = file_size;
                fat.files[j].num_blocks = block_count;

                fclose(input_file);

                if (verbose) {
                    printf("Archivo '%s' actualizado en el archivo empacado.\n", filename);
                }

                break;
            }
        }

        if (!file_found) {
            fprintf(stderr, "Archivo '%s' no encontrado en el archivo empacado.\n", filename);
        }
    }

    // Escribir la estructura FAT actualizada en el archivo
    fseek(archive, 0, SEEK_SET);
    fwrite(&fat, sizeof(FAT), 1, archive);

    fclose(archive);
}

void defragment_archive(const char *archive_name, bool verbose, bool very_verbose) {
    FILE *archive = fopen(archive_name, "rb+");
    if (archive == NULL) {
        fprintf(stderr, "Error al abrir el archivo empacado.\n");
        return;
    }

    FAT fat;
    fread(&fat, sizeof(FAT), 1, archive);

    size_t new_block_position = sizeof(FAT);
    for (size_t i = 0; i < fat.num_files; i++) {
        FileEntry *entry = &fat.files[i];
        size_t file_size = 0;

        for (size_t j = 0; j < entry->num_blocks; j++) {
            Block block;
            fseek(archive, entry->block_positions[j], SEEK_SET);
            fread(&block, sizeof(Block), 1, archive);

            fseek(archive, new_block_position, SEEK_SET);
            fwrite(&block, sizeof(Block), 1, archive);

            entry->block_positions[j] = new_block_position;
            new_block_position += sizeof(Block);
            file_size += sizeof(Block);

            if (very_verbose) {
                printf("Bloque %zu del archivo '%s' movido a la posición %zu\n", j + 1, entry->filename, entry->block_positions[j]);
            }
        }

        // no actualizar tamanio entry->file_size = file_size;

        if (verbose) {
            printf("Archivo '%s' desfragmentado.\n", entry->filename);
        }
    }

    // Actualizar la estructura FAT con los nuevos bloques libres
    fat.num_free_blocks = 0;
    size_t remaining_space = new_block_position;
    while (remaining_space < fat.free_blocks[fat.num_free_blocks - 1]) {
        fat.free_blocks[fat.num_free_blocks++] = remaining_space;
        remaining_space += sizeof(Block);
    }

    // Escribir la estructura FAT actualizada en el archivo
    fseek(archive, 0, SEEK_SET);
    fwrite(&fat, sizeof(FAT), 1, archive);

    // Truncar el archivo para eliminar el espacio no utilizado
    ftruncate(fileno(archive), new_block_position);

    fclose(archive);
}


void append_files_to_archive(const char *archive_name, char **filenames, int num_files, bool verbose, bool very_verbose) {
    FILE *archive = fopen(archive_name, "rb+");
    if (archive == NULL) {
        fprintf(stderr, "Error al abrir el archivo empacado.\n");
        return;
    }

    FAT fat;
    fread(&fat, sizeof(FAT), 1, archive);

    if (num_files == 0) {
        // Leer desde la entrada estándar (stdin)
        char *filename = "stdin";
        size_t file_size = 0;
        size_t block_count = 0;
        size_t bytes_read = 0; 
        Block block;
        while ((bytes_read = fread(&block, 1, sizeof(Block), stdin)) > 0) {
            size_t block_position = find_free_block(&fat);
            if (block_position == (size_t)-1) {
                expand_archive(archive, &fat);
                block_position = find_free_block(&fat);
            }

            write_block(archive, &block, block_position);
            update_fat(&fat, filename, file_size, block_position, bytes_read);

            file_size += bytes_read;
            block_count++;

            if (very_verbose) {
                printf("Bloque %zu leído desde stdin y agregado en la posición %zu\n", block_count, block_position);
            }
        }

        if (verbose) {
            printf("Contenido de stdin agregado al archivo empacado como '%s'.\n", filename);
        }
    } else {
        // Agregar archivos especificados
        for (int i = 0; i < num_files; i++) {
            const char *filename = filenames[i];
            FILE *input_file = fopen(filename, "rb");
            if (input_file == NULL) {
                fprintf(stderr, "Error al abrir el archivo de entrada: %s\n", filename);
                continue;
            }

            size_t file_size = 0;
            size_t bytes_read = 0;
            size_t block_count = 0;
            Block block;
            while ((bytes_read = fread(&block, 1, sizeof(Block), input_file)) > 0) {
                size_t block_position = find_free_block(&fat);
                if (block_position == (size_t)-1) {
                    expand_archive(archive, &fat);
                    block_position = find_free_block(&fat);
                }

                write_block(archive, &block, block_position);
                update_fat(&fat, filename, file_size, block_position, bytes_read);

                file_size += bytes_read;
                block_count++;

                if (very_verbose) {
                    printf("Bloque %zu del archivo '%s' agregado en la posición %zu\n", block_count, filename, block_position);
                }
            }

            fclose(input_file);

            if (verbose) {
                printf("Archivo '%s' agregado al archivo empacado.\n", filename);
            }
        }
    }

    // Escribir la estructura FAT actualizada en el archivo
    fseek(archive, 0, SEEK_SET);
    fwrite(&fat, sizeof(FAT), 1, archive);

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
    else if (flags.delete) delete_files_from_archive(flags.outputFile, flags.inputFiles, flags.numInputFiles, flags.verbose, flags.veryVerbose);
    else if (flags.update) update_files_in_archive(flags.outputFile, flags.inputFiles, flags.numInputFiles, flags.verbose, flags.veryVerbose);
    else if (flags.append) append_files_to_archive(flags.outputFile, flags.inputFiles, flags.numInputFiles, flags.verbose, flags.veryVerbose);

    if (flags.pack) defragment_archive(flags.outputFile, flags.verbose, flags.veryVerbose);
    if (flags.list) list_archive_contents(flags.outputFile, flags.verbose);

    return 0;
}

