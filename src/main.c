#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s create <container>                      - Créer un nouveau FS\n", prog);
    printf("  %s mkdir <container> <chemin>              - Créer un répertoire\n", prog);
    printf("  %s add <container> <chemin_fs> <fichier>   - Ajouter un fichier\n", prog);
    printf("  %s extract <container> <chemin_fs> <dest>  - Extraire un fichier\n", prog);
    printf("  %s list <container> [chemin]               - Lister les fichiers (par défaut /)\n", prog);
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

    if (strcmp(cmd, "mkdir") == 0 && argc == 4) {
        FileSystem *fs = fs_open(argv[2]);
        if (!fs) return EXIT_FAILURE;
        int ret = fs_mkdir(fs, argv[3]);
        fs_close(fs);
        return ret;
    }

    if (strcmp(cmd, "add") == 0 && argc == 5) {
        FileSystem *fs = fs_open(argv[2]);
        if (!fs) return EXIT_FAILURE;
        int ret = fs_add_file(fs, argv[3], argv[4]);
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

    if (strcmp(cmd, "list") == 0 && argc >= 3) {
        FileSystem *fs = fs_open(argv[2]);
        if (!fs) return EXIT_FAILURE;
        const char *list_path = (argc == 4) ? argv[3] : "/";
        fs_list(fs, list_path);
        fs_close(fs);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Commande invalide\n\n");
    print_usage(argv[0]);
    return EXIT_FAILURE;
}
