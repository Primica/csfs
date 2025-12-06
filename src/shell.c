#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BUFFER_SIZE 2048
#define MAX_ARGS 32

typedef struct {
    char *args[MAX_ARGS];
    int argc;
} Command;

static void print_prompt(const Shell *shell) {
    printf("fssh:%s> ", shell->current_path);
    fflush(stdout);
}

static void parse_command(const char *line, Command *cmd) {
    cmd->argc = 0;
    char *dup = malloc(strlen(line) + 1);
    strcpy(dup, line);

    char *saveptr = NULL;
    char *token = strtok_r(dup, " \t\n", &saveptr);

    while (token && cmd->argc < MAX_ARGS - 1) {
        cmd->args[cmd->argc] = malloc(strlen(token) + 1);
        strcpy(cmd->args[cmd->argc], token);
        cmd->argc++;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    cmd->args[cmd->argc] = NULL;
    free(dup);
}

static void free_command(Command *cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->args[i]);
    }
    cmd->argc = 0;
}

static char *resolve_path(const Shell *shell, const char *arg_path) {
    char *result = malloc(MAX_PATH);

    if (strcmp(arg_path, ".") == 0) {
        // Current directory
        strncpy(result, shell->current_path, MAX_PATH - 1);
    } else if (strcmp(arg_path, "..") == 0) {
        // Parent directory
        if (strcmp(shell->current_path, "/") == 0) {
            strncpy(result, "/", MAX_PATH - 1);
        } else {
            char temp[MAX_PATH];
            strncpy(temp, shell->current_path, MAX_PATH - 1);
            char *last_slash = strrchr(temp, '/');
            if (last_slash == temp) {
                // Parent is root
                strncpy(result, "/", MAX_PATH - 1);
            } else {
                *last_slash = '\0';
                strncpy(result, temp, MAX_PATH - 1);
            }
        }
    } else if (arg_path[0] == '/') {
        // Absolute path
        strncpy(result, arg_path, MAX_PATH - 1);
    } else if (strcmp(shell->current_path, "/") == 0) {
        // Relative path from root
        snprintf(result, MAX_PATH, "/%s", arg_path);
    } else {
        // Relative path from non-root
        snprintf(result, MAX_PATH, "%s/%s", shell->current_path, arg_path);
    }

    result[MAX_PATH - 1] = '\0';
    return result;
}

static int cmd_help(Shell *shell, Command *cmd) {
    (void)shell;
    (void)cmd;

    printf("\nCommandes disponibles:\n");
    printf("  help              - Afficher cette aide\n");
    printf("  pwd               - Afficher le répertoire courant\n");
    printf("  ls [chemin]       - Lister un répertoire (défaut: courant)\n");
    printf("  cd <chemin>       - Changer de répertoire\n");
    printf("  mkdir <chemin>    - Créer un répertoire\n");
    printf("  add <chemin> <fichier> - Ajouter un fichier\n");
    printf("  cat <chemin>      - Afficher le contenu d'un fichier\n");
    printf("  extract <chemin> <dest> - Extraire un fichier\n");
    printf("  rm <chemin>       - Supprimer un fichier/répertoire\n");
    printf("  exit              - Quitter le shell\n");
    printf("\n");
    return 0;
}

static int cmd_pwd(Shell *shell, Command *cmd) {
    (void)cmd;
    printf("%s\n", shell->current_path);
    return 0;
}

static int cmd_ls(Shell *shell, Command *cmd) {
    const char *path = (cmd->argc > 1) ? cmd->args[1] : ".";
    char *resolved = resolve_path(shell, path);
    fs_list(shell->fs, resolved);
    free(resolved);
    return 0;
}

static int cmd_cd(Shell *shell, Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "cd: argument requis\n");
        return -1;
    }

    char *resolved = resolve_path(shell, cmd->args[1]);

    int is_dir = 0;
    int idx = -1;

    if (strcmp(resolved, "/") == 0) {
        is_dir = 1;
    } else {
        // Check if path exists and is a directory
        for (int i = 0; i < MAX_FILES; i++) {
            if (shell->fs->inodes[i].filename[0] != '\0') {
                char full_path[MAX_PATH];
                if (strcmp(shell->fs->inodes[i].parent_path, "/") == 0) {
                    snprintf(full_path, MAX_PATH, "/%s", shell->fs->inodes[i].filename);
                } else {
                    snprintf(full_path, MAX_PATH, "%s/%s", shell->fs->inodes[i].parent_path,
                             shell->fs->inodes[i].filename);
                }
                if (strcmp(full_path, resolved) == 0) {
                    idx = i;
                    is_dir = shell->fs->inodes[i].is_directory;
                    break;
                }
            }
        }
    }

    if (idx == -1 && strcmp(resolved, "/") != 0) {
        fprintf(stderr, "cd: '%s' n'existe pas\n", resolved);
        free(resolved);
        return -1;
    }

    if (!is_dir) {
        fprintf(stderr, "cd: '%s' n'est pas un répertoire\n", resolved);
        free(resolved);
        return -1;
    }

    strncpy(shell->current_path, resolved, MAX_PATH - 1);
    shell->current_path[MAX_PATH - 1] = '\0';
    free(resolved);
    return 0;
}

static int cmd_mkdir(Shell *shell, Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "mkdir: argument requis\n");
        return -1;
    }

    char *resolved = resolve_path(shell, cmd->args[1]);
    int ret = fs_mkdir(shell->fs, resolved);
    free(resolved);
    return ret;
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

static int cmd_add(Shell *shell, Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "add: usage -> add <fichier_source> [chemin_fs]\n");
        return -1;
    }

    const char *src_path = cmd->args[1];

    char dest_path[MAX_PATH];
    if (cmd->argc == 2) {
        // Pas de destination fournie : utiliser le répertoire courant + basename
        char base[MAX_FILENAME];
        basename_from_path(src_path, base, sizeof(base));
        if (strcmp(shell->current_path, "/") == 0) {
            snprintf(dest_path, sizeof(dest_path), "/%s", base);
        } else {
            snprintf(dest_path, sizeof(dest_path), "%s/%s", shell->current_path, base);
        }
    } else {
        // Destination fournie
        strncpy(dest_path, cmd->args[2], sizeof(dest_path) - 1);
        dest_path[sizeof(dest_path) - 1] = '\0';

        // Si la destination se termine par '/', on ajoute le basename du fichier source
        size_t len = strlen(dest_path);
        if (len > 0 && dest_path[len - 1] == '/') {
            char base[MAX_FILENAME];
            basename_from_path(src_path, base, sizeof(base));
            if (len == 1) {
                // destination = "/"
                snprintf(dest_path, sizeof(dest_path), "/%s", base);
            } else {
                strncat(dest_path, base, sizeof(dest_path) - strlen(dest_path) - 1);
            }
        }
    }

    char *resolved = resolve_path(shell, dest_path);
    int ret = fs_add_file(shell->fs, resolved, src_path);
    free(resolved);
    return ret;
}

static int cmd_cat(Shell *shell, Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "cat: argument requis\n");
        return -1;
    }

    char *resolved = resolve_path(shell, cmd->args[1]);

    int idx = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(shell->fs->inodes[i].parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", shell->fs->inodes[i].filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s", shell->fs->inodes[i].parent_path,
                         shell->fs->inodes[i].filename);
            }
            if (strcmp(full_path, resolved) == 0) {
                idx = i;
                break;
            }
        }
    }

    if (idx == -1) {
        fprintf(stderr, "cat: '%s' introuvable\n", resolved);
        free(resolved);
        return -1;
    }

    if (shell->fs->inodes[idx].is_directory) {
        fprintf(stderr, "cat: '%s' est un répertoire\n", resolved);
        free(resolved);
        return -1;
    }

    Inode *inode = &shell->fs->inodes[idx];
    fseek(shell->fs->container, (long)inode->offset, SEEK_SET);

    char buffer[BLOCK_SIZE];
    uint64_t remaining = inode->size;

    while (remaining > 0) {
        size_t to_read = (remaining < BLOCK_SIZE) ? (size_t)remaining : BLOCK_SIZE;
        size_t bytes_read = fread(buffer, 1, to_read, shell->fs->container);
        fwrite(buffer, 1, bytes_read, stdout);
        remaining -= bytes_read;
    }

    if (inode->size > 0 && buffer[inode->size % BLOCK_SIZE - 1] != '\n') {
        printf("\n");
    }

    free(resolved);
    return 0;
}

static int cmd_extract(Shell *shell, Command *cmd) {
    if (cmd->argc < 3) {
        fprintf(stderr, "extract: arguments requis (chemin_fs et destination)\n");
        return -1;
    }

    char *resolved = resolve_path(shell, cmd->args[1]);
    int ret = fs_extract_file(shell->fs, resolved, cmd->args[2]);
    free(resolved);
    return ret;
}

static int cmd_rm(Shell *shell, Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "rm: argument requis\n");
        return -1;
    }

    char *resolved = resolve_path(shell, cmd->args[1]);

    int idx = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(shell->fs->inodes[i].parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", shell->fs->inodes[i].filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s", shell->fs->inodes[i].parent_path,
                         shell->fs->inodes[i].filename);
            }
            if (strcmp(full_path, resolved) == 0) {
                idx = i;
                break;
            }
        }
    }

    if (idx == -1) {
        fprintf(stderr, "rm: '%s' introuvable\n", resolved);
        free(resolved);
        return -1;
    }

    if (shell->fs->inodes[idx].is_directory) {
        // Check if directory is empty
        for (int i = 0; i < MAX_FILES; i++) {
            if (shell->fs->inodes[i].filename[0] != '\0' &&
                strcmp(shell->fs->inodes[i].parent_path, resolved) == 0) {
                fprintf(stderr, "rm: '%s' n'est pas vide\n", resolved);
                free(resolved);
                return -1;
            }
        }
    }

    shell->fs->inodes[idx].filename[0] = '\0';
    shell->fs->sb.num_files--;

    printf("Supprimé: %s\n", resolved);
    free(resolved);
    return 0;
}

int shell_execute_command(Shell *shell, const char *cmd_line) {
    if (!cmd_line || cmd_line[0] == '\0') return 0;

    Command cmd;
    parse_command(cmd_line, &cmd);

    if (cmd.argc == 0) return 0;

    const char *command = cmd.args[0];
    int ret = 0;

    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        shell->running = 0;
    } else if (strcmp(command, "help") == 0) {
        ret = cmd_help(shell, &cmd);
    } else if (strcmp(command, "pwd") == 0) {
        ret = cmd_pwd(shell, &cmd);
    } else if (strcmp(command, "ls") == 0) {
        ret = cmd_ls(shell, &cmd);
    } else if (strcmp(command, "cd") == 0) {
        ret = cmd_cd(shell, &cmd);
    } else if (strcmp(command, "mkdir") == 0) {
        ret = cmd_mkdir(shell, &cmd);
    } else if (strcmp(command, "add") == 0) {
        ret = cmd_add(shell, &cmd);
    } else if (strcmp(command, "cat") == 0) {
        ret = cmd_cat(shell, &cmd);
    } else if (strcmp(command, "extract") == 0) {
        ret = cmd_extract(shell, &cmd);
    } else if (strcmp(command, "rm") == 0) {
        ret = cmd_rm(shell, &cmd);
    } else {
        fprintf(stderr, "Commande inconnue: %s\n", command);
        ret = -1;
    }

    free_command(&cmd);
    return ret;
}

Shell *shell_create(FileSystem *fs) {
    if (!fs) return NULL;

    Shell *shell = malloc(sizeof(Shell));
    if (!shell) return NULL;

    shell->fs = fs;
    strcpy(shell->current_path, "/");
    shell->running = 1;

    return shell;
}

void shell_destroy(Shell *shell) {
    if (!shell) return;
    free(shell);
}

void shell_run(Shell *shell) {
    if (!shell) return;

    printf("\n=== CSFS Shell v1.0 ===\n");
    printf("Tapez 'help' pour la liste des commandes\n\n");

    char buffer[BUFFER_SIZE];

    while (shell->running) {
        print_prompt(shell);

        if (!fgets(buffer, sizeof(buffer), stdin)) {
            break;
        }

        // Remove trailing newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

        shell_execute_command(shell, buffer);
    }

    printf("\nAu revoir!\n");
}
