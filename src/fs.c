#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *normalize_path(const char *path) {
    char *normalized = malloc(MAX_PATH);
    if (!normalized) return NULL;

    strncpy(normalized, path, MAX_PATH - 1);
    normalized[MAX_PATH - 1] = '\0';

    // Remove trailing slash unless it's root
    size_t len = strlen(normalized);
    if (len > 1 && normalized[len - 1] == '/') {
        normalized[len - 1] = '\0';
    }

    // Ensure it starts with /
    if (normalized[0] != '/') {
        char temp[MAX_PATH];
        snprintf(temp, MAX_PATH, "/%s", normalized);
        strncpy(normalized, temp, MAX_PATH - 1);
    }

    return normalized;
}

static void extract_filename(const char *path, char *name, size_t size) {
    const char *slash = strrchr(path, '/');
    if (!slash || slash[1] == '\0') {
        // No slash, or trailing slash
        strncpy(name, path, size - 1);
    } else {
        // Copy from after the slash
        strncpy(name, slash + 1, size - 1);
    }
    name[size - 1] = '\0';
}

static void extract_parent_path(const char *path, char *parent, size_t size) {
    char *normalized = normalize_path(path);
    const char *last_slash = strrchr(normalized, '/');

    if (!last_slash) {
        // No slash found, parent is root
        strncpy(parent, "/", size - 1);
    } else if (last_slash == normalized) {
        // Slash is at the beginning (e.g., "/docs")
        strncpy(parent, "/", size - 1);
    } else {
        // Copy up to the last slash
        size_t len = last_slash - normalized;
        strncpy(parent, normalized, len);
        parent[len] = '\0';
    }
    parent[size - 1] = '\0';
    free(normalized);
}

static int path_exists(FileSystem *fs, const char *path, int *is_dir) {
    char *normalized = normalize_path(path);
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(fs->inodes[i].parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", fs->inodes[i].filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s", fs->inodes[i].parent_path,
                         fs->inodes[i].filename);
            }
            if (strcmp(full_path, normalized) == 0) {
                if (is_dir) *is_dir = fs->inodes[i].is_directory;
                free(normalized);
                return i;
            }
        }
    }
    free(normalized);
    return -1;
}

static int parent_exists(FileSystem *fs, const char *parent_path) {
    if (strcmp(parent_path, "/") == 0) return 1;
    return path_exists(fs, parent_path, NULL) >= 0;
}

int fs_create(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("Impossible de créer le système de fichiers");
        return -1;
    }

    SuperBlock sb = {
        .magic = FS_MAGIC,
        .version = 1,
        .num_files = 0,
        .max_files = MAX_FILES,
        .data_offset = sizeof(SuperBlock) + (sizeof(Inode) * MAX_FILES)
    };

    if (fwrite(&sb, sizeof(SuperBlock), 1, f) != 1) {
        perror("Échec d'écriture du superblock");
        fclose(f);
        return -1;
    }

    Inode empty = {0};
    for (int i = 0; i < MAX_FILES; i++) {
        if (fwrite(&empty, sizeof(Inode), 1, f) != 1) {
            perror("Échec d'initialisation des inodes");
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    printf("Système de fichiers créé : %s\n", path);
    return 0;
}

FileSystem *fs_open(const char *path) {
    FileSystem *fs = malloc(sizeof(FileSystem));
    if (!fs) return NULL;

    fs->container = fopen(path, "r+b");
    if (!fs->container) {
        free(fs);
        perror("Impossible d'ouvrir le système de fichiers");
        return NULL;
    }

    if (fread(&fs->sb, sizeof(SuperBlock), 1, fs->container) != 1) {
        perror("Lecture du superblock échouée");
        fclose(fs->container);
        free(fs);
        return NULL;
    }

    if (fs->sb.magic != FS_MAGIC) {
        fprintf(stderr, "Erreur : ce n'est pas un système de fichiers valide\n");
        fclose(fs->container);
        free(fs);
        return NULL;
    }

    if (fread(fs->inodes, sizeof(Inode), MAX_FILES, fs->container) != MAX_FILES) {
        perror("Lecture de la table d'inodes échouée");
        fclose(fs->container);
        free(fs);
        return NULL;
    }

    return fs;
}

void fs_close(FileSystem *fs) {
    if (!fs) return;

    fseek(fs->container, 0, SEEK_SET);
    fwrite(&fs->sb, sizeof(SuperBlock), 1, fs->container);
    fwrite(fs->inodes, sizeof(Inode), MAX_FILES, fs->container);

    fclose(fs->container);
    free(fs);
}

static int find_free_inode(FileSystem *fs) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] == '\0') return i;
    }
    return -1;
}

static uint64_t find_data_end(const FileSystem *fs) {
    uint64_t offset = fs->sb.data_offset;
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] != '\0' && !fs->inodes[i].is_directory) {
            uint64_t end = fs->inodes[i].offset + fs->inodes[i].size;
            if (end > offset) offset = end;
        }
    }
    return offset;
}

int fs_mkdir(FileSystem *fs, const char *path) {
    char *normalized = normalize_path(path);
    char parent_path[MAX_PATH];
    char dirname[MAX_FILENAME];

    extract_parent_path(normalized, parent_path, MAX_PATH);
    extract_filename(normalized, dirname, MAX_FILENAME);

    // Check if directory already exists
    if (path_exists(fs, normalized, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized);
        free(normalized);
        return -1;
    }

    // Check if parent exists
    if (!parent_exists(fs, parent_path)) {
        fprintf(stderr, "Erreur : le répertoire parent '%s' n'existe pas\n", parent_path);
        free(normalized);
        return -1;
    }

    int idx = find_free_inode(fs);
    if (idx == -1) {
        fprintf(stderr, "Erreur : pas d'inode disponible\n");
        free(normalized);
        return -1;
    }

    strncpy(fs->inodes[idx].filename, dirname, MAX_FILENAME - 1);
    fs->inodes[idx].filename[MAX_FILENAME - 1] = '\0';
    strncpy(fs->inodes[idx].parent_path, parent_path, MAX_PATH - 1);
    fs->inodes[idx].parent_path[MAX_PATH - 1] = '\0';
    fs->inodes[idx].is_directory = 1;
    fs->inodes[idx].size = 0;
    fs->inodes[idx].offset = 0;
    fs->inodes[idx].created = time(NULL);
    fs->inodes[idx].modified = fs->inodes[idx].created;

    fs->sb.num_files++;

    printf("Répertoire créé : %s\n", normalized);
    free(normalized);
    return 0;
}

int fs_add_file(FileSystem *fs, const char *fs_path, const char *source_path) {
    if (fs->sb.num_files >= fs->sb.max_files) {
        fprintf(stderr, "Erreur : système de fichiers plein\n");
        return -1;
    }

    FILE *src = fopen(source_path, "rb");
    if (!src) {
        perror("Impossible d'ouvrir le fichier source");
        return -1;
    }

    char *normalized = normalize_path(fs_path);
    char parent_path[MAX_PATH];
    char filename[MAX_FILENAME];

    extract_parent_path(normalized, parent_path, MAX_PATH);
    extract_filename(normalized, filename, MAX_FILENAME);

    // Check if file already exists
    if (path_exists(fs, normalized, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized);
        fclose(src);
        free(normalized);
        return -1;
    }

    // Check if parent directory exists
    if (!parent_exists(fs, parent_path)) {
        fprintf(stderr, "Erreur : le répertoire parent '%s' n'existe pas\n", parent_path);
        fclose(src);
        free(normalized);
        return -1;
    }

    fseek(src, 0, SEEK_END);
    uint64_t size = (uint64_t)ftell(src);
    fseek(src, 0, SEEK_SET);

    int idx = find_free_inode(fs);
    if (idx == -1) {
        fclose(src);
        fprintf(stderr, "Erreur : pas d'inode disponible\n");
        free(normalized);
        return -1;
    }

    uint64_t offset = find_data_end(fs);

    strncpy(fs->inodes[idx].filename, filename, MAX_FILENAME - 1);
    fs->inodes[idx].filename[MAX_FILENAME - 1] = '\0';
    strncpy(fs->inodes[idx].parent_path, parent_path, MAX_PATH - 1);
    fs->inodes[idx].parent_path[MAX_PATH - 1] = '\0';
    fs->inodes[idx].is_directory = 0;
    fs->inodes[idx].size = size;
    fs->inodes[idx].offset = offset;
    fs->inodes[idx].created = time(NULL);
    fs->inodes[idx].modified = fs->inodes[idx].created;

    fseek(fs->container, (long)offset, SEEK_SET);
    char buffer[BLOCK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BLOCK_SIZE, src)) > 0) {
        fwrite(buffer, 1, bytes_read, fs->container);
    }

    fclose(src);
    fs->sb.num_files++;

    printf("Fichier ajouté : %s (%lu octets)\n", normalized, (unsigned long)size);
    free(normalized);
    return 0;
}

int fs_extract_file(FileSystem *fs, const char *fs_path, const char *dest_path) {
    char *normalized = normalize_path(fs_path);

    int idx = path_exists(fs, normalized, NULL);
    if (idx == -1) {
        fprintf(stderr, "Erreur : fichier '%s' introuvable\n", normalized);
        free(normalized);
        return -1;
    }

    if (fs->inodes[idx].is_directory) {
        fprintf(stderr, "Erreur : '%s' est un répertoire, pas un fichier\n", normalized);
        free(normalized);
        return -1;
    }

    Inode *inode = &fs->inodes[idx];

    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        perror("Impossible de créer le fichier de destination");
        free(normalized);
        return -1;
    }

    fseek(fs->container, (long)inode->offset, SEEK_SET);
    char buffer[BLOCK_SIZE];
    uint64_t remaining = inode->size;

    while (remaining > 0) {
        size_t to_read = (remaining < BLOCK_SIZE) ? (size_t)remaining : BLOCK_SIZE;
        size_t bytes_read = fread(buffer, 1, to_read, fs->container);
        fwrite(buffer, 1, bytes_read, dest);
        remaining -= bytes_read;
    }

    fclose(dest);
    printf("Fichier extrait : %s -> %s\n", normalized, dest_path);
    free(normalized);
    return 0;
}

int fs_copy_file(FileSystem *fs, const char *src_path, const char *dest_path) {
    if (fs->sb.num_files >= fs->sb.max_files) {
        fprintf(stderr, "Erreur : système de fichiers plein\n");
        return -1;
    }

    char *normalized_src = normalize_path(src_path);
    char *normalized_dest = normalize_path(dest_path);

    // Find source file
    int src_idx = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(fs->inodes[i].parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", fs->inodes[i].filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s", fs->inodes[i].parent_path,
                         fs->inodes[i].filename);
            }
            if (strcmp(full_path, normalized_src) == 0) {
                src_idx = i;
                break;
            }
        }
    }

    if (src_idx == -1) {
        fprintf(stderr, "Erreur : fichier source '%s' introuvable\n", normalized_src);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    if (fs->inodes[src_idx].is_directory) {
        fprintf(stderr, "Erreur : '%s' est un répertoire, pas un fichier\n", normalized_src);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    // Check if destination already exists
    if (path_exists(fs, normalized_dest, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized_dest);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    // Extract parent and filename from destination
    char parent_path[MAX_PATH];
    char filename[MAX_FILENAME];
    extract_parent_path(normalized_dest, parent_path, MAX_PATH);
    extract_filename(normalized_dest, filename, MAX_FILENAME);

    // Check if parent directory exists
    if (!parent_exists(fs, parent_path)) {
        fprintf(stderr, "Erreur : le répertoire parent '%s' n'existe pas\n", parent_path);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    // Find free inode
    int dest_idx = find_free_inode(fs);
    if (dest_idx == -1) {
        fprintf(stderr, "Erreur : pas d'inode disponible\n");
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    // Copy file data
    uint64_t offset = find_data_end(fs);
    Inode *src_inode = &fs->inodes[src_idx];

    fseek(fs->container, (long)offset, SEEK_SET);
    fseek(fs->container, (long)src_inode->offset, SEEK_SET);

    char buffer[BLOCK_SIZE];
    uint64_t remaining = src_inode->size;
    fseek(fs->container, (long)offset, SEEK_SET);

    while (remaining > 0) {
        size_t to_read = (remaining < BLOCK_SIZE) ? (size_t)remaining : BLOCK_SIZE;
        fseek(fs->container, (long)src_inode->offset + (src_inode->size - remaining), SEEK_SET);
        size_t bytes_read = fread(buffer, 1, to_read, fs->container);
        fseek(fs->container, (long)offset + (src_inode->size - remaining), SEEK_SET);
        fwrite(buffer, 1, bytes_read, fs->container);
        remaining -= bytes_read;
    }

    // Create destination inode
    strncpy(fs->inodes[dest_idx].filename, filename, MAX_FILENAME - 1);
    fs->inodes[dest_idx].filename[MAX_FILENAME - 1] = '\0';
    strncpy(fs->inodes[dest_idx].parent_path, parent_path, MAX_PATH - 1);
    fs->inodes[dest_idx].parent_path[MAX_PATH - 1] = '\0';
    fs->inodes[dest_idx].is_directory = 0;
    fs->inodes[dest_idx].size = src_inode->size;
    fs->inodes[dest_idx].offset = offset;
    fs->inodes[dest_idx].created = time(NULL);
    fs->inodes[dest_idx].modified = fs->inodes[dest_idx].created;

    fs->sb.num_files++;

    printf("Fichier copié : %s -> %s (%lu octets)\n", normalized_src, normalized_dest, 
           (unsigned long)src_inode->size);
    free(normalized_src);
    free(normalized_dest);
    return 0;
}

int fs_move_file(FileSystem *fs, const char *src_path, const char *dest_path) {
    if (fs->sb.num_files >= fs->sb.max_files) {
        fprintf(stderr, "Erreur : système de fichiers plein\n");
        return -1;
    }

    char *normalized_src = normalize_path(src_path);
    char *normalized_dest = normalize_path(dest_path);

    // Find source file
    int src_idx = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(fs->inodes[i].parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", fs->inodes[i].filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s", fs->inodes[i].parent_path,
                         fs->inodes[i].filename);
            }
            if (strcmp(full_path, normalized_src) == 0) {
                src_idx = i;
                break;
            }
        }
    }

    if (src_idx == -1) {
        fprintf(stderr, "Erreur : '%s' introuvable\n", normalized_src);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    // Check if destination already exists
    if (path_exists(fs, normalized_dest, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized_dest);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    // Extract parent and filename from destination
    char parent_path[MAX_PATH];
    char filename[MAX_FILENAME];
    extract_parent_path(normalized_dest, parent_path, MAX_PATH);
    extract_filename(normalized_dest, filename, MAX_FILENAME);

    // Check if parent directory exists
    if (!parent_exists(fs, parent_path)) {
        fprintf(stderr, "Erreur : le répertoire parent '%s' n'existe pas\n", parent_path);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    // For files: update inode in place
    if (!fs->inodes[src_idx].is_directory) {
        strncpy(fs->inodes[src_idx].filename, filename, MAX_FILENAME - 1);
        fs->inodes[src_idx].filename[MAX_FILENAME - 1] = '\0';
        strncpy(fs->inodes[src_idx].parent_path, parent_path, MAX_PATH - 1);
        fs->inodes[src_idx].parent_path[MAX_PATH - 1] = '\0';
        fs->inodes[src_idx].modified = time(NULL);

        printf("Déplacé : %s -> %s\n", normalized_src, normalized_dest);
    } else {
        // For directories: update and cascade rename children
        strncpy(fs->inodes[src_idx].filename, filename, MAX_FILENAME - 1);
        fs->inodes[src_idx].filename[MAX_FILENAME - 1] = '\0';
        strncpy(fs->inodes[src_idx].parent_path, parent_path, MAX_PATH - 1);
        fs->inodes[src_idx].parent_path[MAX_PATH - 1] = '\0';
        fs->inodes[src_idx].modified = time(NULL);

        // Cascade update all children
        for (int i = 0; i < MAX_FILES; i++) {
            if (fs->inodes[i].filename[0] != '\0' && i != src_idx) {
                // Check if this entry is under the old directory path
                if (strncmp(fs->inodes[i].parent_path, normalized_src,
                           strlen(normalized_src)) == 0) {
                    // Rebuild parent path with new directory location
                    char old_parent[MAX_PATH];
                    strncpy(old_parent, fs->inodes[i].parent_path, MAX_PATH - 1);

                    char new_parent[MAX_PATH];
                    size_t offset = strlen(normalized_src);
                    snprintf(new_parent, MAX_PATH, "%s%s", normalized_dest,
                            old_parent + offset);

                    strncpy(fs->inodes[i].parent_path, new_parent, MAX_PATH - 1);
                    fs->inodes[i].parent_path[MAX_PATH - 1] = '\0';
                }
            }
        }

        printf("Répertoire déplacé : %s -> %s\n", normalized_src, normalized_dest);
    }

    free(normalized_src);
    free(normalized_dest);
    return 0;
}

void fs_list(FileSystem *fs, const char *path) {
    fs_list_recursive(fs, path, 0);
}

void fs_list_recursive(FileSystem *fs, const char *path, int depth) {
    char *normalized = normalize_path(path);

    // Verify path exists and is a directory
    int idx = path_exists(fs, normalized, NULL);
    if (idx != -1 && !fs->inodes[idx].is_directory) {
        fprintf(stderr, "Erreur : '%s' n'est pas un répertoire\n", normalized);
        free(normalized);
        return;
    }

    if (depth == 0) {
        printf("\n=== Contenu du système de fichiers ===\n");
        printf("Répertoire : %s\n\n", normalized);
        printf("%-40s %12s %20s\n", "Nom", "Taille", "Date");
        printf("---------------------------------------------------------------------\n");
    }

    // List entries in this directory
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] != '\0' &&
            strcmp(fs->inodes[i].parent_path, normalized) == 0) {

            char time_str[20];
            struct tm *tm_info = localtime(&fs->inodes[i].modified);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

            char indent[64] = "";
            for (int j = 0; j < depth; j++) strcat(indent, "  ");

            if (fs->inodes[i].is_directory) {
                printf("%s%-38s %12s %20s\n", indent, fs->inodes[i].filename,
                       "[DIR]", time_str);
            } else {
                printf("%s%-38s %10lu B  %20s\n", indent,
                       fs->inodes[i].filename,
                       (unsigned long)fs->inodes[i].size, time_str);
            }
        }
    }

    if (depth == 0) printf("\n");
    free(normalized);
}
