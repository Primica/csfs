#include "../include/fs.h"
#include "../include/man.h"
#include "../include/shell.h"
#include "../include/fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <glob.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFER_SIZE 2048
#define MAX_ARGS 32

typedef struct {
    char *args[MAX_ARGS];
    int argc;
} Command;

static int mkdir_p(Shell *shell, const char *path);

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
        strncpy(result, shell->current_path, MAX_PATH - 1);
    } else if (strcmp(arg_path, "..") == 0) {
        if (strcmp(shell->current_path, "/") == 0) {
            strncpy(result, "/", MAX_PATH - 1);
        } else {
            char temp[MAX_PATH];
            strncpy(temp, shell->current_path, MAX_PATH - 1);
            char *last_slash = strrchr(temp, '/');
            if (last_slash == temp) {
                strncpy(result, "/", MAX_PATH - 1);
            } else {
                *last_slash = '\0';
                strncpy(result, temp, MAX_PATH - 1);
            }
        }
    } else if (arg_path[0] == '/') {
        strncpy(result, arg_path, MAX_PATH - 1);
    } else if (strcmp(shell->current_path, "/") == 0) {
        snprintf(result, MAX_PATH, "/%s", arg_path);
    } else {
        snprintf(result, MAX_PATH, "%s/%s", shell->current_path, arg_path);
    }

    result[MAX_PATH - 1] = '\0';
    return result;
}

static void build_full_path_from_inode(const Inode *inode, char *out, size_t size) {
    if (strcmp(inode->parent_path, "/") == 0) {
        snprintf(out, size, "/%s", inode->filename);
    } else {
        snprintf(out, size, "%s/%s", inode->parent_path, inode->filename);
    }
    out[size - 1] = '\0';
}

static int inode_index_for_path(Shell *shell, const char *path, int *is_dir_out) {
    if (strcmp(path, "/") == 0) {
        if (is_dir_out) *is_dir_out = 1;
        return -1;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0') {
            char full_path[MAX_PATH];
            build_full_path_from_inode(&shell->fs->inodes[i], full_path, sizeof(full_path));
            if (strcmp(full_path, path) == 0) {
                if (is_dir_out) *is_dir_out = shell->fs->inodes[i].is_directory;
                return i;
            }
        }
    }

    return -1;
}

static int wildcard_match(const char *pattern, const char *str) {
    if (*pattern == '\0') return *str == '\0';

    if (*pattern == '*') {
        return wildcard_match(pattern + 1, str) || (*str && wildcard_match(pattern, str + 1));
    }

    if (*pattern == '?') {
        return *str && wildcard_match(pattern + 1, str + 1);
    }

    if (*pattern == *str) {
        return wildcard_match(pattern + 1, str + 1);
    }

    return 0;
}

static void strip_trailing_slash(char *path) {
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';
}

static int fs_path_is_dir(Shell *shell, const char *abs_path) {
    if (strcmp(abs_path, "/") == 0) return 1;
    int is_dir = 0;
    int idx = inode_index_for_path(shell, abs_path, &is_dir);
    return (idx >= 0) ? is_dir : 0;
}

static int has_glob(const char *s) {
    return strpbrk(s, "*?") != NULL;
}

static int delete_path(Shell *sh, const char *abs_path, int recursive, int force) {
    if (strcmp(abs_path, "/") == 0) {
        fprintf(stderr, "rm: refus de supprimer la racine\n");
        return -1;
    }

    int is_dir = 0;
    int idx = inode_index_for_path(sh, abs_path, &is_dir);
    if (idx == -1) {
        if (!force) fprintf(stderr, "rm: '%s' introuvable\n", abs_path);
        return force ? 0 : -1;
    }

    if (is_dir) {
        int has_child = 0;
        for (int i = 0; i < MAX_FILES; i++) {
            if (sh->fs->inodes[i].filename[0] != '\0' &&
                strcmp(sh->fs->inodes[i].parent_path, abs_path) == 0) {
                has_child = 1;
                if (!recursive) {
                    fprintf(stderr, "rm: '%s' n'est pas vide (utiliser -r)\n", abs_path);
                    return -1;
                }
            }
        }

        if (has_child && recursive) {
            for (int i = 0; i < MAX_FILES; i++) {
                if (sh->fs->inodes[i].filename[0] != '\0' &&
                    strcmp(sh->fs->inodes[i].parent_path, abs_path) == 0) {
                    char child_path[MAX_PATH];
                    build_full_path_from_inode(&sh->fs->inodes[i], child_path, sizeof(child_path));
                    if (delete_path(sh, child_path, recursive, force) != 0 && !force) {
                        return -1;
                    }
                }
            }
        }
    }

    sh->fs->inodes[idx].filename[0] = '\0';
    sh->fs->sb.num_files--;
    if (!force) printf("Supprimé: %s\n", abs_path);
    return 0;
}

static int expand_fs_glob(Shell *shell, const char *input, char results[][MAX_PATH], int max_results) {
    char pattern[MAX_PATH];

    if (input[0] == '/') {
        strncpy(pattern, input, sizeof(pattern) - 1);
        pattern[sizeof(pattern) - 1] = '\0';
    } else {
        if (strcmp(shell->current_path, "/") == 0) {
            snprintf(pattern, sizeof(pattern), "/%s", input);
        } else {
            snprintf(pattern, sizeof(pattern), "%s/%s", shell->current_path, input);
        }
    }

    strip_trailing_slash(pattern);

    int count = 0;
    for (int i = 0; i < MAX_FILES && count < max_results; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0') {
            char full_path[MAX_PATH];
            build_full_path_from_inode(&shell->fs->inodes[i], full_path, sizeof(full_path));

            if (wildcard_match(pattern, full_path)) {
                strncpy(results[count], full_path, MAX_PATH - 1);
                results[count][MAX_PATH - 1] = '\0';
                count++;
            }
        }
    }

    if ((strcmp(pattern, "/") == 0 || strcmp(pattern, "/*") == 0) && count < max_results) {
        strncpy(results[count], "/", MAX_PATH - 1);
        results[count][MAX_PATH - 1] = '\0';
        count++;
    }

    return count;
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
    printf("  find [chemin] [motif] - Rechercher par nom\n");
    printf("  cd <chemin>       - Changer de répertoire\n");
    printf("  mkdir <chemin>    - Créer un répertoire\n");
    printf("  add <fichier>     - Ajouter un fichier\n");
    printf("  cat <chemin>      - Afficher le contenu d'un fichier\n");
    printf("  stat <chemin>     - Métadonnées détaillées\n");
    printf("  cp <src> <dest>   - Copier un fichier\n");
    printf("  mv <src> <dest>   - Déplacer/renommer un fichier ou répertoire\n");
    printf("  extract <src> [dest] - Extraire un fichier\n");
    printf("  rm <chemin>       - Supprimer un fichier/répertoire\n");
    printf("  fetch [opts]      - Afficher infos type neofetch\n");
    printf("  clear             - Effacer l'écran\n");
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

static int cmd_clear(Shell *shell, Command *cmd) {
    (void)shell;
    (void)cmd;
    printf("\033[2J\033[H");
    fflush(stdout);
    return 0;
}

static int cmd_pwd(Shell *shell, Command *cmd) {
    (void)cmd;
    printf("%s\n", shell->current_path);
    return 0;
}

static int cmd_ls(Shell *shell, Command *cmd) {
    const char *path = (cmd->argc > 1) ? cmd->args[1] : ".";
    if (!has_glob(path)) {
        char *resolved = resolve_path(shell, path);
        fs_list(shell->fs, resolved);
        free(resolved);
        return 0;
    }

    char matches[MAX_FILES][MAX_PATH];
    int mcount = expand_fs_glob(shell, path, matches, MAX_FILES);
    if (mcount == 0) {
        fprintf(stderr, "ls: aucune correspondance pour '%s'\n", path);
        return -1;
    }

    for (int i = 0; i < mcount; i++) {
        fs_list(shell->fs, matches[i]);
    }
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

static int mkdir_p(Shell *shell, const char *path) {
    if (!path || *path == '\0' || strcmp(path, "/") == 0) return 0;

    if (fs_path_is_dir(shell, path)) return 0;

    char temp[MAX_PATH];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    size_t len = strlen(temp);
    if (len > 1 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
    }

    char *slash = strrchr(temp, '/');
    if (slash && slash != temp) {
        *slash = '\0';
        mkdir_p(shell, temp);
        *slash = '/';
    }

    fs_mkdir(shell->fs, path);
    return 0;
}

static Shell *g_add_shell = NULL;
static char g_add_base_src[MAX_PATH] = "";
static char g_add_base_dest[MAX_PATH] = "";
static int g_add_error = 0;

static int add_recursive_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)ftwbuf;
    if (typeflag == FTW_D || typeflag == FTW_DNR) {
        const char *rel = fpath + strlen(g_add_base_src);
        if (*rel == '/') rel++;
        if (*rel == '\0') return 0;

        char dest_dir[MAX_PATH];
        if (strcmp(g_add_base_dest, "/") == 0) {
            snprintf(dest_dir, sizeof(dest_dir), "/%s", rel);
        } else {
            snprintf(dest_dir, sizeof(dest_dir), "%s/%s", g_add_base_dest, rel);
        }
        mkdir_p(g_add_shell, dest_dir);
        return 0;
    } else if (typeflag == FTW_F) {
        const char *rel = fpath + strlen(g_add_base_src);
        if (*rel == '/') rel++;

        char dest_file[MAX_PATH];
        if (strcmp(g_add_base_dest, "/") == 0) {
            snprintf(dest_file, sizeof(dest_file), "/%s", rel);
        } else {
            snprintf(dest_file, sizeof(dest_file), "%s/%s", g_add_base_dest, rel);
        }

        if (fs_add_file(g_add_shell->fs, dest_file, fpath) != 0) {
            g_add_error = -1;
        }
        return 0;
    }
    return 0;
}

static int cmd_add(Shell *shell, Command *cmd) {
    int recursive = 0;
    int first_arg = 1;

    for (int i = 1; i < cmd->argc; i++) {
        if (strcmp(cmd->args[i], "-r") == 0 || strcmp(cmd->args[i], "-R") == 0) {
            recursive = 1;
            first_arg = i + 1;
        } else {
            break;
        }
    }

    if (cmd->argc < first_arg + 1) {
        fprintf(stderr, "add: usage -> add [-r] <fichier_source> [chemin_fs]\n");
        return -1;
    }

    glob_t g = {0};
    int gr = glob(cmd->args[first_arg], 0, NULL, &g);
    if (gr != 0 || g.gl_pathc == 0) {
        fprintf(stderr, "add: aucune correspondance pour '%s'\n", cmd->args[1]);
        globfree(&g);
        return -1;
    }

    int src_count = (int)g.gl_pathc;
    int dest_provided = (cmd->argc >= first_arg + 2);

    char dest_base[MAX_PATH] = "";
    char *resolved_dest = NULL;
    int dest_is_dir = 0;

    if (dest_provided) {
        strncpy(dest_base, cmd->args[first_arg + 1], sizeof(dest_base) - 1);
        dest_base[sizeof(dest_base) - 1] = '\0';
        resolved_dest = resolve_path(shell, dest_base);
        strip_trailing_slash(resolved_dest);
        dest_is_dir = fs_path_is_dir(shell, resolved_dest);
        size_t len = strlen(dest_base);
        if (len > 0 && dest_base[len - 1] == '/') dest_is_dir = 1;

        if (!dest_is_dir && src_count > 1) {
            fprintf(stderr, "add: la destination doit être un répertoire pour plusieurs sources\n");
            free(resolved_dest);
            globfree(&g);
            return -1;
        }
    }

    int ret = 0;

    for (int i = 0; i < src_count; i++) {
        const char *src_path = g.gl_pathv[i];
        struct stat st;
        if (stat(src_path, &st) != 0) {
            fprintf(stderr, "add: impossible d'accéder à '%s'\n", src_path);
            ret = -1;
            continue;
        }

        char base[MAX_FILENAME];
        basename_from_path(src_path, base, sizeof(base));

        char dest_path[MAX_PATH];

        if (dest_provided) {
            if (dest_is_dir) {
                if (strcmp(resolved_dest, "/") == 0) {
                    snprintf(dest_path, sizeof(dest_path), "/%s", base);
                } else {
                    snprintf(dest_path, sizeof(dest_path), "%s/%s", resolved_dest, base);
                }
            } else {
                strncpy(dest_path, resolved_dest, sizeof(dest_path) - 1);
                dest_path[sizeof(dest_path) - 1] = '\0';
            }
        } else {
            if (strcmp(shell->current_path, "/") == 0) {
                snprintf(dest_path, sizeof(dest_path), "/%s", base);
            } else {
                snprintf(dest_path, sizeof(dest_path), "%s/%s", shell->current_path, base);
            }
        }

        if (S_ISDIR(st.st_mode)) {
            if (!recursive) {
                fprintf(stderr, "add: '%s' est un répertoire (utiliser -r)\n", src_path);
                ret = -1;
                continue;
            }
            g_add_shell = shell;
            strncpy(g_add_base_src, src_path, sizeof(g_add_base_src) - 1);
            strncpy(g_add_base_dest, dest_path, sizeof(g_add_base_dest) - 1);
            g_add_error = 0;

            mkdir_p(shell, dest_path);

            if (nftw(src_path, add_recursive_callback, 20, FTW_PHYS) != 0) {
                fprintf(stderr, "add: erreur lors du parcours de '%s'\n", src_path);
                ret = -1;
            }
            if (g_add_error != 0) ret = g_add_error;
        } else {
            int r = fs_add_file(shell->fs, dest_path, src_path);
            if (r != 0) ret = r;
        }
    }

    if (resolved_dest) free(resolved_dest);
    globfree(&g);
    return ret;
}

static int cmd_cat(Shell *shell, Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "cat: argument requis\n");
        return -1;
    }

    char matches[MAX_FILES][MAX_PATH];
    int mcount = expand_fs_glob(shell, cmd->args[1], matches, MAX_FILES);
    if (mcount == 0) {
        fprintf(stderr, "cat: aucune correspondance pour '%s'\n", cmd->args[1]);
        return -1;
    }

    int ret = 0;
    for (int mi = 0; mi < mcount; mi++) {
        int is_dir = 0;
        int idx = inode_index_for_path(shell, matches[mi], &is_dir);
        if (idx == -1) {
            fprintf(stderr, "cat: '%s' introuvable\n", matches[mi]);
            ret = -1;
            continue;
        }
        if (is_dir) {
            fprintf(stderr, "cat: '%s' est un répertoire\n", matches[mi]);
            ret = -1;
            continue;
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

        if (inode->size > 0 && buffer[(inode->size - 1) % BLOCK_SIZE] != '\n') {
            printf("\n");
        }
    }

    return ret;
}

static void extract_recursive_dir(Shell *shell, const char *fs_path, const char *host_base, int *error) {
    char mkdir_cmd[MAX_PATH * 2];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", host_base);
    system(mkdir_cmd);

    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0' &&
            strcmp(shell->fs->inodes[i].parent_path, fs_path) == 0) {
            char child_fs[MAX_PATH];
            build_full_path_from_inode(&shell->fs->inodes[i], child_fs, sizeof(child_fs));

            char child_host[MAX_PATH];
            snprintf(child_host, sizeof(child_host), "%s/%s", host_base, shell->fs->inodes[i].filename);

            if (shell->fs->inodes[i].is_directory) {
                extract_recursive_dir(shell, child_fs, child_host, error);
            } else {
                if (fs_extract_file(shell->fs, child_fs, child_host) != 0) {
                    *error = -1;
                }
            }
        }
    }
}

static int cmd_extract(Shell *shell, Command *cmd) {
    int recursive = 0;
    int first_arg = 1;

    for (int i = 1; i < cmd->argc; i++) {
        if (strcmp(cmd->args[i], "-r") == 0 || strcmp(cmd->args[i], "-R") == 0) {
            recursive = 1;
            first_arg = i + 1;
        } else {
            break;
        }
    }

    if (cmd->argc < first_arg + 1) {
        fprintf(stderr, "extract: usage -> extract [-r] <chemin_fs> [destination]\n");
        return -1;
    }

    char matches[MAX_FILES][MAX_PATH];
    int mcount = expand_fs_glob(shell, cmd->args[first_arg], matches, MAX_FILES);
    if (mcount == 0) {
        fprintf(stderr, "extract: aucune correspondance pour '%s'\n", cmd->args[first_arg]);
        return -1;
    }

    const char *dest_arg = (cmd->argc >= first_arg + 2) ? cmd->args[first_arg + 1] : NULL;
    int dest_is_dir = 0;
    char dest_base[MAX_PATH] = "";

    if (dest_arg) {
        strncpy(dest_base, dest_arg, sizeof(dest_base) - 1);
        dest_base[sizeof(dest_base) - 1] = '\0';
        size_t len = strlen(dest_base);
        if (len > 0 && dest_base[len - 1] == '/') dest_is_dir = 1;

        if (!dest_is_dir && mcount > 1) {
            fprintf(stderr, "extract: la destination doit être un répertoire pour plusieurs sources\n");
            return -1;
        }
    } else {
        if (mcount > 1) dest_is_dir = 1;
    }

    int ret = 0;

    for (int i = 0; i < mcount; i++) {
        int is_dir = 0;
        int idx = inode_index_for_path(shell, matches[i], &is_dir);
        if (idx == -1) {
            fprintf(stderr, "extract: '%s' introuvable\n", matches[i]);
            ret = -1;
            continue;
        }

        char out_path[MAX_PATH];
        if (dest_arg) {
            if (dest_is_dir) {
                char base[MAX_FILENAME];
                basename_from_path(matches[i], base, sizeof(base));
                snprintf(out_path, sizeof(out_path), "%s%s", dest_base, base);
            } else {
                strncpy(out_path, dest_base, sizeof(out_path) - 1);
                out_path[sizeof(out_path) - 1] = '\0';
            }
        } else {
            char base[MAX_FILENAME];
            basename_from_path(matches[i], base, sizeof(base));
            strncpy(out_path, base, sizeof(out_path) - 1);
            out_path[sizeof(out_path) - 1] = '\0';
        }

        if (is_dir) {
            if (!recursive) {
                fprintf(stderr, "extract: '%s' est un répertoire (utiliser -r)\n", matches[i]);
                ret = -1;
                continue;
            }
            int err = 0;
            extract_recursive_dir(shell, matches[i], out_path, &err);
            if (err != 0) ret = err;
        } else {
            int r = fs_extract_file(shell->fs, matches[i], out_path);
            if (r != 0) ret = r;
        }
    }

    return ret;
}

static int cmd_cp(Shell *shell, Command *cmd) {
    if (cmd->argc < 3) {
        fprintf(stderr, "cp: arguments requis (source et destination)\n");
        return -1;
    }

    char matches[MAX_FILES][MAX_PATH];
    int mcount = expand_fs_glob(shell, cmd->args[1], matches, MAX_FILES);
    if (mcount == 0) {
        fprintf(stderr, "cp: aucune correspondance pour '%s'\n", cmd->args[1]);
        return -1;
    }

    char *dest_resolved = resolve_path(shell, cmd->args[2]);
    strip_trailing_slash(dest_resolved);
    int dest_is_dir = fs_path_is_dir(shell, dest_resolved);
    size_t dlen = strlen(cmd->args[2]);
    if (dlen > 0 && cmd->args[2][dlen - 1] == '/') dest_is_dir = 1;

    if (!dest_is_dir && mcount > 1) {
        fprintf(stderr, "cp: la destination doit être un répertoire pour plusieurs sources\n");
        free(dest_resolved);
        return -1;
    }

    int ret = 0;

    for (int mi = 0; mi < mcount; mi++) {
        int is_dir = 0;
        int idx = inode_index_for_path(shell, matches[mi], &is_dir);
        if (idx == -1) {
            fprintf(stderr, "cp: '%s' introuvable\n", matches[mi]);
            ret = -1;
            continue;
        }
        if (is_dir) {
            fprintf(stderr, "cp: '%s' est un répertoire (non supporté)\n", matches[mi]);
            ret = -1;
            continue;
        }

        char dest_path[MAX_PATH];
        if (dest_is_dir) {
            char base[MAX_FILENAME];
            basename_from_path(matches[mi], base, sizeof(base));
            if (strcmp(dest_resolved, "/") == 0) {
                snprintf(dest_path, sizeof(dest_path), "/%s", base);
            } else {
                snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_resolved, base);
            }
        } else {
            strncpy(dest_path, dest_resolved, sizeof(dest_path) - 1);
            dest_path[sizeof(dest_path) - 1] = '\0';
        }

        int r = fs_copy_file(shell->fs, matches[mi], dest_path);
        if (r != 0) ret = r;
    }

    free(dest_resolved);
    return ret;
}

static int cmd_mv(Shell *shell, Command *cmd) {
    if (cmd->argc < 3) {
        fprintf(stderr, "mv: arguments requis (source et destination)\n");
        return -1;
    }

    char matches[MAX_FILES][MAX_PATH];
    int mcount = expand_fs_glob(shell, cmd->args[1], matches, MAX_FILES);
    if (mcount == 0) {
        fprintf(stderr, "mv: aucune correspondance pour '%s'\n", cmd->args[1]);
        return -1;
    }

    char *dest_resolved = resolve_path(shell, cmd->args[2]);
    strip_trailing_slash(dest_resolved);
    int dest_is_dir = fs_path_is_dir(shell, dest_resolved);
    size_t dlen = strlen(cmd->args[2]);
    if (dlen > 0 && cmd->args[2][dlen - 1] == '/') dest_is_dir = 1;

    if (!dest_is_dir && mcount > 1) {
        fprintf(stderr, "mv: la destination doit être un répertoire pour plusieurs sources\n");
        free(dest_resolved);
        return -1;
    }

    int ret = 0;

    for (int mi = 0; mi < mcount; mi++) {
        int is_dir = 0;
        int idx = inode_index_for_path(shell, matches[mi], &is_dir);
        if (idx == -1) {
            fprintf(stderr, "mv: '%s' introuvable\n", matches[mi]);
            ret = -1;
            continue;
        }
        if (is_dir) {
            fprintf(stderr, "mv: '%s' est un répertoire (non supporté)\n", matches[mi]);
            ret = -1;
            continue;
        }

        char dest_path[MAX_PATH];
        if (dest_is_dir) {
            char base[MAX_FILENAME];
            basename_from_path(matches[mi], base, sizeof(base));
            if (strcmp(dest_resolved, "/") == 0) {
                snprintf(dest_path, sizeof(dest_path), "/%s", base);
            } else {
                snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_resolved, base);
            }
        } else {
            strncpy(dest_path, dest_resolved, sizeof(dest_path) - 1);
            dest_path[sizeof(dest_path) - 1] = '\0';
        }

        int r = fs_move_file(shell->fs, matches[mi], dest_path);
        if (r != 0) ret = r;
    }

    free(dest_resolved);
    return ret;
}

static int cmd_rm(Shell *shell, Command *cmd) {
    // Parse options -r (recursive) and -f (force)
    int recursive = 0;
    int force = 0;
    int first_path = -1;
    for (int i = 1; i < cmd->argc; i++) {
        if (cmd->args[i][0] == '-' && cmd->args[i][1] != '\0') {
            if (strcmp(cmd->args[i], "-r") == 0 || strcmp(cmd->args[i], "-R") == 0) {
                recursive = 1;
            } else if (strcmp(cmd->args[i], "-f") == 0) {
                force = 1;
            } else {
                fprintf(stderr, "rm: option inconnue '%s'\n", cmd->args[i]);
                return -1;
            }
        } else {
            first_path = i;
            break;
        }
    }

    if (first_path == -1) {
        if (!force) fprintf(stderr, "rm: argument requis\n");
        return force ? 0 : -1;
    }

    int ret = 0;

    for (int i = first_path; i < cmd->argc; i++) {
        char matches[MAX_FILES][MAX_PATH];
        int mcount = expand_fs_glob(shell, cmd->args[i], matches, MAX_FILES);
        if (mcount == 0) {
            if (!force) fprintf(stderr, "rm: aucune correspondance pour '%s'\n", cmd->args[i]);
            if (!force) ret = -1;
            continue;
        }

        for (int mi = 0; mi < mcount; mi++) {
            if (delete_path(shell, matches[mi], recursive, force) != 0 && !force) ret = -1;
        }
    }

    return ret;
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

    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0' &&
            strcmp(shell->fs->inodes[i].parent_path, path) == 0) {

            if (opts->dirs_only && !shell->fs->inodes[i].is_directory) continue;

            for (int d = 0; d < depth - 1; d++) {
                printf("%s   ", is_last[d] ? " " : "│");
            }
            if (depth > 0) {
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

            if (shell->fs->inodes[i].is_directory) {
                printf("\033[1;34m%s\033[0m/", shell->fs->inodes[i].filename);
            } else {
                printf("%s", shell->fs->inodes[i].filename);
            }

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

    printf("\033[1;34m%s\033[0m\n", resolved);

    int is_last[256] = {0};
    tree_recursive(shell, resolved, 1, &opts, is_last, 0);

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

static int name_matches(const char *name, const char *pattern) {
    if (!pattern) return 1;
    return strstr(name, pattern) != NULL;
}

static void find_recursive(Shell *shell, const char *path, const char *pattern) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (shell->fs->inodes[i].filename[0] != '\0' &&
            strcmp(shell->fs->inodes[i].parent_path, path) == 0) {
            char child_path[MAX_PATH];
            build_full_path_from_inode(&shell->fs->inodes[i], child_path, sizeof(child_path));

            if (name_matches(shell->fs->inodes[i].filename, pattern)) {
                printf("%s%s\n", child_path, shell->fs->inodes[i].is_directory ? "/" : "");
            }

            if (shell->fs->inodes[i].is_directory) {
                find_recursive(shell, child_path, pattern);
            }
        }
    }
}

static int cmd_find(Shell *shell, Command *cmd) {
    const char *start_arg = ".";
    const char *pattern = NULL;

    if (cmd->argc == 2) {
        if (cmd->args[1][0] == '/' || cmd->args[1][0] == '.') {
            start_arg = cmd->args[1];
        } else {
            pattern = cmd->args[1];
        }
    } else if (cmd->argc >= 3) {
        start_arg = cmd->args[1];
        pattern = cmd->args[2];
    }

    char *start_path = resolve_path(shell, start_arg);
    size_t start_len = strlen(start_path);
    if (start_len > 1 && start_path[start_len - 1] == '/') {
        start_path[start_len - 1] = '\0';
    }
    int is_dir = 0;
    int idx = inode_index_for_path(shell, start_path, &is_dir);

    if (idx == -1 && strcmp(start_path, "/") != 0) {
        fprintf(stderr, "find: '%s' introuvable\n", start_path);
        free(start_path);
        return -1;
    }

    // Si le point de départ est un fichier, on évalue uniquement ce fichier
    if (idx >= 0 && !is_dir) {
        if (name_matches(shell->fs->inodes[idx].filename, pattern)) {
            printf("%s\n", start_path);
        }
        free(start_path);
        return 0;
    }

    if (pattern && strcmp(start_path, "/") != 0 && name_matches(strrchr(start_path, '/') ? strrchr(start_path, '/') + 1 : start_path, pattern)) {
        printf("%s/\n", start_path);
    }

    find_recursive(shell, start_path, pattern);

    free(start_path);
    return 0;
}

static void print_stat_info(const char *path, const Inode *inode, int is_dir) {
    printf("Chemin : %s\n", path);
    printf("Type   : %s\n", is_dir ? "Répertoire" : "Fichier");

    if (inode) {
        printf("Taille : %lu octets\n", (unsigned long)inode->size);

        char created[32];
        char modified[32];
        struct tm tm_c, tm_m;
        strftime(created, sizeof(created), "%Y-%m-%d %H:%M", localtime_r(&inode->created, &tm_c));
        strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M", localtime_r(&inode->modified, &tm_m));
        printf("Créé   : %s\n", created);
        printf("Modifié: %s\n", modified);
        printf("Parent : %s\n", inode->parent_path);
    } else {
        printf("Taille : 0 octets\n");
        printf("Créé   : N/A\n");
        printf("Modifié: N/A\n");
        printf("Parent : (aucun)\n");
    }
}

static int cmd_stat(Shell *shell, Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "stat: usage -> stat <chemin>\n");
        return -1;
    }

    char *resolved = resolve_path(shell, cmd->args[1]);
    size_t len = strlen(resolved);
    if (len > 1 && resolved[len - 1] == '/') {
        resolved[len - 1] = '\0';
    }
        char matches[MAX_FILES][MAX_PATH];
        int mcount = expand_fs_glob(shell, cmd->args[1], matches, MAX_FILES);
        if (mcount == 0) {
            fprintf(stderr, "stat: aucune correspondance pour '%s'\n", cmd->args[1]);
            return -1;
        }

        int ret = 0;
        for (int mi = 0; mi < mcount; mi++) {
            int is_dir = 0;
            int idx = inode_index_for_path(shell, matches[mi], &is_dir);

            if (idx == -1 && strcmp(matches[mi], "/") != 0) {
                fprintf(stderr, "stat: '%s' introuvable\n", matches[mi]);
                ret = -1;
                continue;
            }

            if (idx == -1) {
                print_stat_info(matches[mi], NULL, 1);
            } else {
                print_stat_info(matches[mi], &shell->fs->inodes[idx], is_dir);
            }
        }

        return ret;
}

static int cmd_fetch(Shell *shell, Command *cmd) {
    int list = 0;
    int color = 1;
    const char *only[32];
    int only_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const char *a = cmd->args[i];
        if (strcmp(a, "--list") == 0 || strcmp(a, "-l") == 0) {
            list = 1;
        } else if (strcmp(a, "--no-color") == 0 || strcmp(a, "--no-colors") == 0) {
            color = 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "fetch: option inconnue '%s'\n", a);
            return -1;
        } else {
            if (only_count < (int)(sizeof(only)/sizeof(only[0]))) {
                only[only_count++] = a;
            }
        }
    }

    if (list) {
        fetch_list_modules();
        return 0;
    }

    return fetch_print(shell, only_count ? only : NULL, only_count, color);
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
    } else if (strcmp(command, "find") == 0) {
        ret = cmd_find(shell, &cmd);
    } else if (strcmp(command, "cd") == 0) {
        ret = cmd_cd(shell, &cmd);
    } else if (strcmp(command, "mkdir") == 0) {
        ret = cmd_mkdir(shell, &cmd);
    } else if (strcmp(command, "add") == 0) {
        ret = cmd_add(shell, &cmd);
    } else if (strcmp(command, "cat") == 0) {
        ret = cmd_cat(shell, &cmd);
    } else if (strcmp(command, "stat") == 0) {
        ret = cmd_stat(shell, &cmd);
    } else if (strcmp(command, "fetch") == 0) {
        ret = cmd_fetch(shell, &cmd);
    } else if (strcmp(command, "extract") == 0) {
        ret = cmd_extract(shell, &cmd);
    } else if (strcmp(command, "cp") == 0) {
        ret = cmd_cp(shell, &cmd);
    } else if (strcmp(command, "mv") == 0) {
        ret = cmd_mv(shell, &cmd);
    } else if (strcmp(command, "rm") == 0) {
        ret = cmd_rm(shell, &cmd);
    } else if (strcmp(command, "clear") == 0) {
        ret = cmd_clear(shell, &cmd);
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

        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

        shell_execute_command(shell, buffer);
    }

    printf("\nAu revoir!\n");
}
