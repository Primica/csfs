#include "fs.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s <container> [shell]                        - Ouvrir en mode shell (défaut)\n", prog);
    printf("  %s <container> create                         - Créer un nouveau FS\n", prog);
    printf("  %s <container> mkdir <chemin>                 - Créer un répertoire\n", prog);
    printf("  %s <container> add <fichier> [chemin_fs]      - Ajouter un fichier (chemin par défaut: /<basename>)\n", prog);
    printf("  %s <container> extract <chemin_fs> <dest>     - Extraire un fichier\n", prog);
    printf("  %s <container> list [chemin]                  - Lister les fichiers (par défaut /)\n", prog);
}

static void basename_from_path(const char *path, char *out, size_t out_size) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        strncpy(out, path, out_size - 1);
    } else {
        strncpy(out, slash + 1, out_size - 1);
    }
    out[out_size - 1] = '\0';
}

static void build_dest_path(const char *maybe_fs_path, const char *src, char *out, size_t out_size) {
    char base[MAX_FILENAME];
    basename_from_path(src, base, sizeof(base));

    if (!maybe_fs_path) {
        snprintf(out, out_size, "/%s", base);
        return;
    }

    strncpy(out, maybe_fs_path, out_size - 1);
    out[out_size - 1] = '\0';

    size_t len = strlen(out);
    if (len == 0 || out[0] != '/') {
        // Force absolute path from root
        char tmp[MAX_PATH];
        snprintf(tmp, sizeof(tmp), "/%s", out);
        strncpy(out, tmp, out_size - 1);
        out[out_size - 1] = '\0';
        len = strlen(out);
    }

    if (out[len - 1] == '/') {
        strncat(out, base, out_size - strlen(out) - 1);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *container = argv[1];

    // If only container specified or "shell" is specified, open interactive shell
    if (argc == 2 || (argc == 3 && strcmp(argv[2], "shell") == 0)) {
        FileSystem *fs = fs_open(container);
        if (!fs) return EXIT_FAILURE;

        Shell *shell = shell_create(fs);
        if (!shell) {
            fs_close(fs);
            return EXIT_FAILURE;
        }

        shell_run(shell);

        shell_destroy(shell);
        fs_close(fs);
        return EXIT_SUCCESS;
    }

    // Command-line mode
    const char *cmd = argv[2];

    if (strcmp(cmd, "create") == 0) {
        return fs_create(container);
    }

    if (strcmp(cmd, "mkdir") == 0 && argc == 4) {
        FileSystem *fs = fs_open(container);
        if (!fs) return EXIT_FAILURE;
        int ret = fs_mkdir(fs, argv[3]);
        fs_close(fs);
        return ret;
    }

    if (strcmp(cmd, "add") == 0 && (argc == 4 || argc == 5)) {
        const char *src = argv[3];
        const char *maybe_dest = (argc == 5) ? argv[4] : NULL;

        char dest_path[MAX_PATH];
        build_dest_path(maybe_dest, src, dest_path, sizeof(dest_path));

        FileSystem *fs = fs_open(container);
        if (!fs) return EXIT_FAILURE;
        int ret = fs_add_file(fs, dest_path, src);
        fs_close(fs);
        return ret;
    }

    if (strcmp(cmd, "extract") == 0 && argc == 5) {
        FileSystem *fs = fs_open(container);
        if (!fs) return EXIT_FAILURE;
        int ret = fs_extract_file(fs, argv[3], argv[4]);
        fs_close(fs);
        return ret;
    }

    if (strcmp(cmd, "list") == 0 && argc >= 3) {
        FileSystem *fs = fs_open(container);
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
