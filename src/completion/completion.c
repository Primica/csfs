#include "../../include/completion.h"
#include "../../include/fs.h"
#include "../../include/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Liste des commandes disponibles
static const char *commands[] = {
    "help", "man", "pwd", "ls", "tree", "find", "cd", "mkdir",
    "add", "cat", "stat", "extract", "cp", "mv", "rm", "clear",
    "fetch", "edit", "exit", "quit"
};
static const int num_commands = sizeof(commands) / sizeof(commands[0]);

void completions_free(Completions *comp) {
    if (!comp) return;
    for (int i = 0; i < comp->count; i++) {
        free(comp->suggestions[i]);
    }
    free(comp->suggestions);
    comp->count = 0;
}

// Récupère le chemin à compléter depuis le buffer
static void extract_path(const char *buffer, int pos, char *path_to_complete) {
    // Trouver le dernier token (après le dernier espace)
    int start = pos - 1;
    while (start >= 0 && buffer[start] != ' ') {
        start--;
    }
    start++;

    int len = pos - start;
    if (len > 0) {
        strncpy(path_to_complete, &buffer[start], len);
        path_to_complete[len] = '\0';
    } else {
        path_to_complete[0] = '\0';
    }
}

// Complète les commandes disponibles
static int complete_commands(const char *partial, char **suggestions, int max_count) {
    int count = 0;
    for (int i = 0; i < num_commands && count < max_count; i++) {
        if (strncmp(commands[i], partial, strlen(partial)) == 0) {
            suggestions[count] = strdup(commands[i]);
            count++;
        }
    }
    return count;
}

// Récupère le répertoire parent d'un chemin
static void get_parent_dir(Shell *shell, const char *path, char *parent) {
    if (!path || path[0] == '\0') {
        strcpy(parent, shell->current_path);
        return;
    }

    if (path[0] == '/') {
        strcpy(parent, path);
    } else if (strcmp(shell->current_path, "/") == 0) {
        snprintf(parent, MAX_PATH, "/%s", path);
    } else {
        snprintf(parent, MAX_PATH, "%s/%s", shell->current_path, path);
    }

    // Supprimer le dernier caractère s'il y a un chemin incomplet
    size_t len = strlen(parent);
    if (len > 1 && parent[len - 1] == '/') {
        parent[len - 1] = '\0';
    }
}

// Complète les chemins (fichiers et répertoires du FS)
static int complete_paths(Shell *shell, const char *partial, char **suggestions, int max_count) {
    int count = 0;
    char resolved[MAX_PATH];

    // Déterminer le répertoire parent
    char parent[MAX_PATH];
    const char *filename_partial = partial;

    if (strchr(partial, '/')) {
        // Il y a un chemin, extraire le répertoire parent
        strcpy(resolved, partial);
        char *slash = strrchr(resolved, '/');
        if (slash) {
            *slash = '\0';
            filename_partial = slash + 1;
            get_parent_dir(shell, resolved, parent);
        }
    } else {
        // Pas de slash, chercher dans le répertoire courant
        strcpy(parent, shell->current_path);
    }

    // Parcourir les inodes pour trouver les fichiers/répertoires correspondants
    for (int i = 0; i < MAX_FILES && count < max_count; i++) {
        Inode *inode = get_inode(shell->fs, i);
        if (inode->filename[0] == '\0') continue;

        // Vérifier si le parent correspond
        if (strcmp(inode->parent_path, parent) != 0) {
            continue;
        }

        // Vérifier si le nom commence par le partial
        if (strncmp(inode->filename, filename_partial, strlen(filename_partial)) == 0) {
            char full_name[MAX_FILENAME];
            strcpy(full_name, inode->filename);
            if (inode->is_directory) {
                strcat(full_name, "/");
            }
            suggestions[count] = strdup(full_name);
            count++;
        }
    }

    return count;
}

// Compte le nombre de arguments dans la ligne de commande
static int count_args(const char *buffer, int pos) {
    int args = 0;
    int in_word = 0;
    for (int i = 0; i < pos; i++) {
        if (buffer[i] == ' ') {
            in_word = 0;
        } else if (!in_word) {
            args++;
            in_word = 1;
        }
    }
    return args;
}

// Affiche les complétions disponibles
static void display_completions(const char **suggestions, int count) {
    if (count == 0) return;
    
    printf("\n");
    for (int i = 0; i < count; i++) {
        printf("  %s\n", suggestions[i]);
    }
}

int shell_complete(Shell *shell, char *buffer, int *pos, int show_all) {
    char path_to_complete[MAX_PATH];
    char **suggestions = malloc(MAX_FILES * sizeof(char *));
    int count = 0;

    extract_path(buffer, *pos, path_to_complete);

    // Premier argument = commande
    int arg_num = count_args(buffer, *pos);
    
    if (arg_num == 1) {
        // Compléter une commande
        count = complete_commands(path_to_complete, suggestions, MAX_FILES);
    } else {
        // Compléter un chemin
        count = complete_paths(shell, path_to_complete, suggestions, MAX_FILES);
    }

    if (count == 0) {
        free(suggestions);
        return 0;
    }

    if (show_all || count > 1) {
        // Afficher toutes les options
        display_completions((const char **)suggestions, count);
    } else if (count == 1) {
        // Auto-compléter si une seule option
        // Trouver le début du mot à remplacer
        int start = *pos - 1;
        while (start >= 0 && buffer[start] != ' ') {
            start--;
        }
        start++;

        // Calculer combien de caractères ajouter
        int word_len = *pos - start;
        int new_len = strlen(suggestions[0]);
        
        // Supprimer l'ancien mot
        memmove(&buffer[start], &buffer[start + word_len], 
                strlen(&buffer[start + word_len]) + 1);
        
        // Insérer le nouveau mot
        strcpy(&buffer[start], suggestions[0]);
        *pos = start + new_len;
    }

    // Libérer la mémoire
    for (int i = 0; i < count; i++) {
        free(suggestions[i]);
    }
    free(suggestions);

    return count;
}
