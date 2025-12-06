#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

int fs_add_file(FileSystem *fs, const char *filename, const char *source_path) {
    if (fs->sb.num_files >= fs->sb.max_files) {
        fprintf(stderr, "Erreur : système de fichiers plein\n");
        return -1;
    }

    FILE *src = fopen(source_path, "rb");
    if (!src) {
        perror("Impossible d'ouvrir le fichier source");
        return -1;
    }

    fseek(src, 0, SEEK_END);
    uint64_t size = (uint64_t)ftell(src);
    fseek(src, 0, SEEK_SET);

    int idx = find_free_inode(fs);
    if (idx == -1) {
        fclose(src);
        fprintf(stderr, "Erreur : pas d'inode disponible\n");
        return -1;
    }

    uint64_t offset = find_data_end(fs);

    strncpy(fs->inodes[idx].filename, filename, MAX_FILENAME - 1);
    fs->inodes[idx].filename[MAX_FILENAME - 1] = '\0';
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

    printf("Fichier ajouté : %s (%lu octets)\n", filename, (unsigned long)size);
    return 0;
}

int fs_extract_file(FileSystem *fs, const char *filename, const char *dest_path) {
    int idx = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(fs->inodes[i].filename, filename) == 0) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        fprintf(stderr, "Erreur : fichier '%s' introuvable\n", filename);
        return -1;
    }

    Inode *inode = &fs->inodes[idx];

    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        perror("Impossible de créer le fichier de destination");
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
    printf("Fichier extrait : %s -> %s\n", filename, dest_path);
    return 0;
}

void fs_list(FileSystem *fs) {
    printf("\n=== Contenu du système de fichiers ===\n");
    printf("Nombre de fichiers : %u/%u\n\n", fs->sb.num_files, fs->sb.max_files);

    printf("%-30s %12s %20s\n", "Nom", "Taille", "Date");
    printf("---------------------------------------------------------------\n");

    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] != '\0') {
            char time_str[20];
            struct tm *tm_info = localtime(&fs->inodes[i].modified);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

            printf("%-30s %10lu B  %20s\n",
                   fs->inodes[i].filename,
                   (unsigned long)fs->inodes[i].size,
                   time_str);
        }
    }
    printf("\n");
}
