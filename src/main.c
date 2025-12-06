#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s create <container>                - Créer un nouveau FS\n", prog);
    printf("  %s add <container> <file> <nom>      - Ajouter un fichier\n", prog);
    printf("  %s extract <container> <nom> <dest>  - Extraire un fichier\n", prog);
    printf("  %s list <container>                  - Lister les fichiers\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "create") == 0 && argc == 3) {
        return fs_create(argv[2]);
    }

    if (strcmp(cmd, "add") == 0 && argc == 5) {
        FileSystem *fs = fs_open(argv[2]);
        if (!fs) return EXIT_FAILURE;
        int ret = fs_add_file(fs, argv[4], argv[3]);
        fs_close(fs);
        return ret;
    }

    if (strcmp(cmd, "extract") == 0 && argc == 5) {
        FileSystem *fs = fs_open(argv[2]);
        if (!fs) return EXIT_FAILURE;
        int ret = fs_extract_file(fs, argv[3], argv[4]);
        fs_close(fs);
        return ret;
    }

    if (strcmp(cmd, "list") == 0 && argc == 3) {
        FileSystem *fs = fs_open(argv[2]);
        if (!fs) return EXIT_FAILURE;
        fs_list(fs);
        fs_close(fs);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Commande invalide\n\n");
    print_usage(argv[0]);
    return EXIT_FAILURE;
}
