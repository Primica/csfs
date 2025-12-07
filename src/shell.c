#include "shell.h"
#include "man.h"

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
    printf("  help              - Afficher cette aide rapide\n");
    printf("  man <commande>    - Afficher le manuel d'une commande\n");
    printf("  pwd               - Afficher le répertoire courant\n");
    printf("  ls [chemin]       - Lister un répertoire\n");
    printf("  tree [options]    - Affichage arborescent\n");
    printf("  cd <chemin>       - Changer de répertoire\n");
    printf("  mkdir <chemin>    - Créer un répertoire\n");
    printf("  add <fichier>     - Ajouter un fichier\n");
    printf("  cat <chemin>      - Afficher le contenu d'un fichier\n");
    printf("  cp <src> <dest>   - Copier un fichier\n");
    printf("  mv <src> <dest>   - Déplacer/renommer un fichier ou répertoire\n");
    printf("  extract <src> [dest] - Extraire un fichier\n");
    printf("  rm <chemin>       - Supprimer un fichier/répertoire\n");
    printf("  exit              - Quitter le shell\n");
    printf("\nPour plus de détails: man <commande>\n");
    printf("Liste complète: man --list\n\n");
    return 0;
}

static int cmd_man(Shell *shell, Command *cmd) {
    (void)shell;

    if (cmd->argc < 2) {
        printf("Usage: man <commande>\n");
        printf("       man --list    Liste toutes les pages disponibles\n");
        return -1;
    }

    if (strcmp(cmd->args[1], "-l") == 0 || strcmp(cmd->args[1], "--list") == 0) {
        man_list_all();
        return 0;
    }

    man_display(cmd->args[1]);
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
    if (cmd->argc < 2) {
        fprintf(stderr, "extract: usage -> extract <chemin_fs> [destination]\n");
        return -1;
    }

    char *resolved = resolve_path(shell, cmd->args[1]);
    
    // Si pas de destination fournie, utiliser le basename du fichier source
    char final_dest[MAX_PATH];
    const char *dest_path = cmd->args[2];
    
    if (cmd->argc == 2) {
        // Pas de destination : utiliser le basename dans le répertoire courant
        basename_from_path(cmd->args[1], final_dest, sizeof(final_dest));
        dest_path = final_dest;
    } else if (dest_path[strlen(dest_path) - 1] == '/') {
        // Destination se termine par / : c'est un répertoire, ajouter le basename
        char base[MAX_FILENAME];
        basename_from_path(cmd->args[1], base, sizeof(base));
        snprintf(final_dest, sizeof(final_dest), "%s%s", dest_path, base);
        dest_path = final_dest;
    }

    int ret = fs_extract_file(shell->fs, resolved, dest_path);
    free(resolved);
    return ret;
}

static int cmd_cp(Shell *shell, Command *cmd) {
    if (cmd->argc < 3) {
        fprintf(stderr, "cp: arguments requis (source et destination)\n");
        return -1;
    }

    char *src = resolve_path(shell, cmd->args[1]);
    char *dest = resolve_path(shell, cmd->args[2]);

    int ret = fs_copy_file(shell->fs, src, dest);
    free(src);
    free(dest);
    return ret;
}

static int cmd_mv(Shell *shell, Command *cmd) {
    if (cmd->argc < 3) {
        fprintf(stderr, "mv: arguments requis (source et destination)\n");
        return -1;
    }

    char *src = resolve_path(shell, cmd->args[1]);
    char *dest = resolve_path(shell, cmd->args[2]);

    int ret = fs_move_file(shell->fs, src, dest);
    free(src);
    free(dest);
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

typedef struct {
    int show_metadata;
    int dirs_only;
    int max_depth;
} TreeOptions;

static void tree_recursive(Shell *shell, const char *path, int depth, TreeOptions *opts, 
                          int is_last[], int parent_depth) {
    (void)parent_depth;
    if (opts->max_depth >= 0 && depth > opts->max_depth) return;

    // Print entries in this directory
    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0' &&
            strcmp(shell->fs->inodes[i].parent_path, path) == 0) {

            if (opts->dirs_only && !shell->fs->inodes[i].is_directory) continue;

            // Print tree characters
            for (int d = 0; d < depth - 1; d++) {
                printf("%s   ", is_last[d] ? " " : "│");
            }
            if (depth > 0) {
                // Count remaining entries at this level
                int remaining = 0;
                for (int j = i + 1; j < MAX_FILES; j++) {
                    if (shell->fs->inodes[j].filename[0] != '\0' &&
                        strcmp(shell->fs->inodes[j].parent_path, path) == 0) {
                        if (!opts->dirs_only || shell->fs->inodes[j].is_directory) {
                            remaining++;
                        }
                    }
                }
                printf("%s── ", remaining == 0 ? "└" : "├");
                is_last[depth - 1] = (remaining == 0);
            }

            // Print name
            if (shell->fs->inodes[i].is_directory) {
                printf("\033[1;34m%s\033[0m/", shell->fs->inodes[i].filename);
            } else {
                printf("%s", shell->fs->inodes[i].filename);
            }

            // Print metadata if requested
            if (opts->show_metadata) {
                if (!shell->fs->inodes[i].is_directory) {
                    printf(" (%lu B)", (unsigned long)shell->fs->inodes[i].size);
                }
                char time_str[20];
                struct tm *tm_info = localtime(&shell->fs->inodes[i].modified);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
                printf(" [%s]", time_str);
            }
            printf("\n");

            // Recurse into subdirectories
            if (shell->fs->inodes[i].is_directory) {
                char subdir_path[MAX_PATH];
                if (strcmp(path, "/") == 0) {
                    snprintf(subdir_path, MAX_PATH, "/%s", shell->fs->inodes[i].filename);
                } else {
                    snprintf(subdir_path, MAX_PATH, "%s/%s", path, shell->fs->inodes[i].filename);
                }
                tree_recursive(shell, subdir_path, depth + 1, opts, is_last, depth);
            }
        }
    }
}

static int cmd_tree(Shell *shell, Command *cmd) {
    TreeOptions opts = {0, 0, -1};
    const char *path = NULL;

    // Parse options
    for (int i = 1; i < cmd->argc; i++) {
        if (strcmp(cmd->args[i], "-a") == 0) {
            opts.show_metadata = 1;
        } else if (strcmp(cmd->args[i], "-d") == 0) {
            opts.dirs_only = 1;
        } else if (strcmp(cmd->args[i], "-L") == 0) {
            if (i + 1 < cmd->argc) {
                opts.max_depth = atoi(cmd->args[++i]);
            } else {
                fprintf(stderr, "tree: -L requiert un argument numérique\n");
                return -1;
            }
        } else if (cmd->args[i][0] != '-') {
            path = cmd->args[i];
        } else {
            fprintf(stderr, "tree: option inconnue '%s'\n", cmd->args[i]);
            return -1;
        }
    }

    if (!path) path = ".";

    char *resolved = resolve_path(shell, path);

    // Check if path exists and is a directory
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

    if (idx != -1 && !shell->fs->inodes[idx].is_directory && strcmp(resolved, "/") != 0) {
        fprintf(stderr, "tree: '%s' n'est pas un répertoire\n", resolved);
        free(resolved);
        return -1;
    }

    // Print root
    printf("\033[1;34m%s\033[0m\n", resolved);

    // Tree traversal
    int is_last[256] = {0};
    tree_recursive(shell, resolved, 1, &opts, is_last, 0);

    // Count stats
    int dirs = 0, files = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0') {
            if (shell->fs->inodes[i].is_directory) dirs++;
            else files++;
        }
    }

    printf("\n");
    if (opts.dirs_only) {
        printf("%d directories\n", dirs);
    } else {
        printf("%d directories, %d files\n", dirs, files);
    }

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
    } else if (strcmp(command, "man") == 0) {
        ret = cmd_man(shell, &cmd);
    } else if (strcmp(command, "pwd") == 0) {
        ret = cmd_pwd(shell, &cmd);
    } else if (strcmp(command, "ls") == 0) {
        ret = cmd_ls(shell, &cmd);
    } else if (strcmp(command, "tree") == 0) {
        ret = cmd_tree(shell, &cmd);
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
    } else if (strcmp(command, "cp") == 0) {
        ret = cmd_cp(shell, &cmd);
    } else if (strcmp(command, "mv") == 0) {
        ret = cmd_mv(shell, &cmd);
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
