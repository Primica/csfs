#include "../include/fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Fonction de hash simple pour les chemins
static uint32_t hash_path(const char *path) {
    uint32_t hash = 5381;
    int c;
    while ((c = *path++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_TABLE_SIZE;
}

// Initialise la hash table
static void hash_table_init(FileSystem *fs) {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        fs->hash_table[i].inode_index = -1;
        fs->hash_table[i].full_path[0] = '\0';
    }
}

// Insere une entree dans la hash table avec gestion des collisions (linear probing)
static void hash_table_insert(FileSystem *fs, const char *full_path, int inode_index) {
    uint32_t idx = hash_path(full_path);
    int attempts = 0;
    
    while (attempts < HASH_TABLE_SIZE) {
        if (fs->hash_table[idx].inode_index == -1) {
            fs->hash_table[idx].inode_index = inode_index;
            strncpy(fs->hash_table[idx].full_path, full_path, MAX_PATH - 1);
            fs->hash_table[idx].full_path[MAX_PATH - 1] = '\0';
            return;
        }
        idx = (idx + 1) % HASH_TABLE_SIZE;
        attempts++;
    }
    fprintf(stderr, "Avertissement : hash table pleine, insertion impossible\n");
}

// Recherche dans la hash table - retourne l'index de l'inode ou -1
static int hash_table_lookup(FileSystem *fs, const char *full_path) {
    uint32_t idx = hash_path(full_path);
    int attempts = 0;
    
    while (attempts < HASH_TABLE_SIZE) {
        if (fs->hash_table[idx].inode_index == -1) {
            return -1;
        }
        if (strcmp(fs->hash_table[idx].full_path, full_path) == 0) {
            return fs->hash_table[idx].inode_index;
        }
        idx = (idx + 1) % HASH_TABLE_SIZE;
        attempts++;
    }
    return -1;
}

// Supprime une entree de la hash table
static void hash_table_delete(FileSystem *fs, const char *full_path) {
    uint32_t idx = hash_path(full_path);
    int attempts = 0;
    
    while (attempts < HASH_TABLE_SIZE) {
        if (fs->hash_table[idx].inode_index == -1) {
            return;
        }
        if (strcmp(fs->hash_table[idx].full_path, full_path) == 0) {
            fs->hash_table[idx].inode_index = -1;
            fs->hash_table[idx].full_path[0] = '\0';
            return;
        }
        idx = (idx + 1) % HASH_TABLE_SIZE;
        attempts++;
    }
}

// --- Gestion du Cache LRU ---

static void write_inode_to_disk(FileSystem *fs, int inode_index, const Inode *inode) {
    uint64_t offset = sizeof(SuperBlock) + (uint64_t)inode_index * sizeof(Inode);
    fseek(fs->container, offset, SEEK_SET);
    fwrite(inode, sizeof(Inode), 1, fs->container);
}

static void read_inode_from_disk(FileSystem *fs, int inode_index, Inode *inode) {
    uint64_t offset = sizeof(SuperBlock) + (uint64_t)inode_index * sizeof(Inode);
    fseek(fs->container, offset, SEEK_SET);
    if (fread(inode, sizeof(Inode), 1, fs->container) != 1) {
        memset(inode, 0, sizeof(Inode));
    }
}

static void cache_remove(FileSystem *fs, CacheNode *node) {
    if (node->prev) node->prev->next = node->next;
    else fs->cache_head = node->next;
    
    if (node->next) node->next->prev = node->prev;
    else fs->cache_tail = node->prev;
    
    node->prev = node->next = NULL;
}

static void cache_push_front(FileSystem *fs, CacheNode *node) {
    node->next = fs->cache_head;
    node->prev = NULL;
    if (fs->cache_head) fs->cache_head->prev = node;
    fs->cache_head = node;
    if (!fs->cache_tail) fs->cache_tail = node;
}

Inode* get_inode(FileSystem *fs, int inode_index) {
    if (inode_index < 0 || inode_index >= MAX_FILES) return NULL;
    
    // Chercher dans le cache
    for (int i = 0; i < fs->cache_count; i++) {
        if (fs->cache_nodes[i]->inode_index == inode_index) {
            CacheNode *node = fs->cache_nodes[i];
            // Déplacer à l'avant (LRU)
            cache_remove(fs, node);
            cache_push_front(fs, node);
            return &node->inode;
        }
    }
    
    // Pas dans le cache, charger depuis le disque
    CacheNode *node = NULL;
    if (fs->cache_count < LRU_CACHE_SIZE) {
        node = malloc(sizeof(CacheNode));
        fs->cache_nodes[fs->cache_count++] = node;
    } else {
        // Évincer le plus ancien (tail)
        node = fs->cache_tail;
        if (node->dirty) {
            write_inode_to_disk(fs, node->inode_index, &node->inode);
        }
        cache_remove(fs, node);
    }
    
    node->inode_index = inode_index;
    read_inode_from_disk(fs, inode_index, &node->inode);
    node->dirty = 0;
    cache_push_front(fs, node);
    
    return &node->inode;
}

void mark_inode_dirty(FileSystem *fs, int inode_index) {
    for (int i = 0; i < fs->cache_count; i++) {
        if (fs->cache_nodes[i]->inode_index == inode_index) {
            fs->cache_nodes[i]->dirty = 1;
            return;
        }
    }
}

// Reconstruit la hash table a partir des inodes actuels
static void hash_table_rebuild(FileSystem *fs) {
    hash_table_init(fs);
    for (int i = 0; i < MAX_FILES; i++) {
        Inode inode;
        read_inode_from_disk(fs, i, &inode);
        if (inode.filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(inode.parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", inode.filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s", inode.parent_path,
                         inode.filename);
            }
            hash_table_insert(fs, full_path, i);
        }
    }
}

static char *normalize_path(const char *path) {
    char *result = malloc(MAX_PATH);
    if (!result) return NULL;

    char temp[MAX_PATH];
    strncpy(temp, path, MAX_PATH - 1);
    temp[MAX_PATH - 1] = '\0';

    char *components[256];
    int comp_count = 0;

    int is_absolute = (path[0] == '/');
    char *saveptr = NULL;
    char *token = strtok_r(temp, "/", &saveptr);

    while (token && comp_count < 256) {
        if (strcmp(token, ".") == 0) {
            // Ignorer .
        } else if (strcmp(token, "..") == 0) {
            if (comp_count > 0) {
                free(components[comp_count - 1]);
                comp_count--;
            }
        } else if (token[0] != '\0') {
            components[comp_count] = malloc(strlen(token) + 1);
            strcpy(components[comp_count], token);
            comp_count++;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    size_t off = 0;
    if (is_absolute) {
        result[off++] = '/';
        result[off] = '\0';
    } else {
        result[0] = '\0';
    }

    for (int i = 0; i < comp_count; i++) {
        size_t need = strlen(components[i]) + ((off > 0 && result[off - 1] != '/') ? 1 : 0);
        if (off + need >= MAX_PATH) {
            free(components[i]);
            break;
        }
        if (off > 0 && result[off - 1] != '/') {
            result[off++] = '/';
        }
        memcpy(result + off, components[i], strlen(components[i]));
        off += strlen(components[i]);
        result[off] = '\0';
        free(components[i]);
    }

    if (off == 0) {
        result[0] = '/';
        result[1] = '\0';
    }

    size_t len = strlen(result);
    if (len > 1 && result[len - 1] == '/') result[len - 1] = '\0';

    return result;
}

static void extract_filename(const char *path, char *name, size_t size) {
    const char *slash = strrchr(path, '/');
    if (!slash || slash[1] == '\0') {
        strncpy(name, path, size - 1);
    } else {
        strncpy(name, slash + 1, size - 1);
    }
    name[size - 1] = '\0';
}

static void extract_parent_path(const char *path, char *parent, size_t size) {
    char *normalized = normalize_path(path);
    const char *last_slash = strrchr(normalized, '/');

    if (!last_slash) {
        strncpy(parent, "/", size - 1);
    } else if (last_slash == normalized) {
        strncpy(parent, "/", size - 1);
    } else {
        size_t len = last_slash - normalized;
        strncpy(parent, normalized, len);
        parent[len] = '\0';
    }
    parent[size - 1] = '\0';
    free(normalized);
}

static int path_exists(FileSystem *fs, const char *path, int *is_dir) {
    char *normalized = normalize_path(path);
    
    int idx = hash_table_lookup(fs, normalized);
    if (idx >= 0) {
        if (is_dir) {
            Inode *inode = get_inode(fs, idx);
            *is_dir = inode ? inode->is_directory : 0;
        }
        free(normalized);
        return idx;
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

    // Initialiser le cache LRU
    fs->cache_head = NULL;
    fs->cache_tail = NULL;
    fs->cache_count = 0;
    for (int i = 0; i < LRU_CACHE_SIZE; i++) {
        fs->cache_nodes[i] = NULL;
    }

    // Construire la hash table pour recherche O(1)
    hash_table_rebuild(fs);

    return fs;
}

void fs_close(FileSystem *fs) {
    if (!fs) return;

    // Sauvegarder le SuperBlock
    fseek(fs->container, 0, SEEK_SET);
    fwrite(&fs->sb, sizeof(SuperBlock), 1, fs->container);

    // Écrire les inodes sales du cache sur le disque
    for (int i = 0; i < fs->cache_count; i++) {
        if (fs->cache_nodes[i]->dirty) {
            write_inode_to_disk(fs, fs->cache_nodes[i]->inode_index, &fs->cache_nodes[i]->inode);
        }
    }

    // Libérer la mémoire du cache
    for (int i = 0; i < fs->cache_count; i++) {
        free(fs->cache_nodes[i]);
    }

    fclose(fs->container);
    free(fs);
}

static int find_free_inode(FileSystem *fs) {
    for (int i = 0; i < MAX_FILES; i++) {
        Inode inode;
        read_inode_from_disk(fs, i, &inode);
        if (inode.filename[0] == '\0') return i;
    }
    return -1;
}

static uint64_t find_data_end(const FileSystem *fs) {
    uint64_t offset = fs->sb.data_offset;
    for (int i = 0; i < MAX_FILES; i++) {
        Inode inode;
        // On doit utiliser read_inode_from_disk ici car fs est const dans la signature originale, 
        // mais FileSystem* n'est pas vraiment const si on doit lire du disque (fseek/fread sur container).
        // On va tricher un peu sur le const pour l'instant ou changer la signature.
        uint64_t inode_offset = sizeof(SuperBlock) + (uint64_t)i * sizeof(Inode);
        fseek(fs->container, inode_offset, SEEK_SET);
        if (fread(&inode, sizeof(Inode), 1, fs->container) == 1) {
            if (inode.filename[0] != '\0' && !inode.is_directory) {
                uint64_t end = inode.offset + inode.size;
                if (end > offset) offset = end;
            }
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

    if (path_exists(fs, normalized, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized);
        free(normalized);
        return -1;
    }

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

    Inode *inode = get_inode(fs, idx);
    strncpy(inode->filename, dirname, MAX_FILENAME - 1);
    inode->filename[MAX_FILENAME - 1] = '\0';
    strncpy(inode->parent_path, parent_path, MAX_PATH - 1);
    inode->parent_path[MAX_PATH - 1] = '\0';
    inode->is_directory = 1;
    inode->size = 0;
    inode->offset = 0;
    inode->created = time(NULL);
    inode->modified = inode->created;
    mark_inode_dirty(fs, idx);

    fs->sb.num_files++;

    // Ajouter a la hash table pour acces O(1)
    hash_table_insert(fs, normalized, idx);

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

    if (path_exists(fs, normalized, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized);
        fclose(src);
        free(normalized);
        return -1;
    }

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

    Inode *inode = get_inode(fs, idx);
    strncpy(inode->filename, filename, MAX_FILENAME - 1);
    inode->filename[MAX_FILENAME - 1] = '\0';
    strncpy(inode->parent_path, parent_path, MAX_PATH - 1);
    inode->parent_path[MAX_PATH - 1] = '\0';
    inode->is_directory = 0;
    inode->size = size;
    inode->offset = offset;
    inode->created = time(NULL);
    inode->modified = inode->created;
    mark_inode_dirty(fs, idx);

    fseek(fs->container, (long)offset, SEEK_SET);
    char buffer[BLOCK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BLOCK_SIZE, src)) > 0) {
        fwrite(buffer, 1, bytes_read, fs->container);
    }

    fclose(src);
    fs->sb.num_files++;

    // Ajouter a la hash table pour acces O(1)
    hash_table_insert(fs, normalized, idx);

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

    Inode *inode = get_inode(fs, idx);
    if (!inode || inode->is_directory) {
        fprintf(stderr, "Erreur : '%s' est un répertoire, pas un fichier\n", normalized);
        free(normalized);
        return -1;
    }

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

    // Utiliser hash table pour recherche rapide
    int src_idx = hash_table_lookup(fs, normalized_src);

    if (src_idx == -1) {
        fprintf(stderr, "Erreur : fichier source '%s' introuvable\n", normalized_src);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    Inode *src_inode_ptr = get_inode(fs, src_idx);
    if (src_inode_ptr->is_directory) {
        fprintf(stderr, "Erreur : '%s' est un répertoire, pas un fichier\n", normalized_src);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }
    // Copie de l'inode car src_inode_ptr peut être invalidé par get_inode(fs, dest_idx)
    Inode src_inode_val = *src_inode_ptr;

    if (path_exists(fs, normalized_dest, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized_dest);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    char parent_path[MAX_PATH];
    char filename[MAX_FILENAME];
    extract_parent_path(normalized_dest, parent_path, MAX_PATH);
    extract_filename(normalized_dest, filename, MAX_FILENAME);

    if (!parent_exists(fs, parent_path)) {
        fprintf(stderr, "Erreur : le répertoire parent '%s' n'existe pas\n", parent_path);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    int dest_idx = find_free_inode(fs);
    if (dest_idx == -1) {
        fprintf(stderr, "Erreur : pas d'inode disponible\n");
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    uint64_t offset = find_data_end(fs);

    char buffer[BLOCK_SIZE];
    uint64_t remaining = src_inode_val.size;

    while (remaining > 0) {
        size_t to_read = (remaining < BLOCK_SIZE) ? (size_t)remaining : BLOCK_SIZE;
        fseek(fs->container, (long)src_inode_val.offset + (src_inode_val.size - remaining), SEEK_SET);
        size_t bytes_read = fread(buffer, 1, to_read, fs->container);
        fseek(fs->container, (long)offset + (src_inode_val.size - remaining), SEEK_SET);
        fwrite(buffer, 1, bytes_read, fs->container);
        remaining -= bytes_read;
    }

    Inode *dest_inode = get_inode(fs, dest_idx);
    strncpy(dest_inode->filename, filename, MAX_FILENAME - 1);
    dest_inode->filename[MAX_FILENAME - 1] = '\0';
    strncpy(dest_inode->parent_path, parent_path, MAX_PATH - 1);
    dest_inode->parent_path[MAX_PATH - 1] = '\0';
    dest_inode->is_directory = 0;
    dest_inode->size = src_inode_val.size;
    dest_inode->offset = offset;
    dest_inode->created = time(NULL);
    dest_inode->modified = dest_inode->created;
    mark_inode_dirty(fs, dest_idx);

    fs->sb.num_files++;

    printf("Fichier copié : %s -> %s (%lu octets)\n", normalized_src, normalized_dest,
           (unsigned long)src_inode_val.size);
    
    // Ajouter a la hash table pour acces O(1)
    hash_table_insert(fs, normalized_dest, dest_idx);
    
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

    // Utiliser hash table pour recherche rapide
    int src_idx = hash_table_lookup(fs, normalized_src);

    if (src_idx == -1) {
        fprintf(stderr, "Erreur : '%s' introuvable\n", normalized_src);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    if (path_exists(fs, normalized_dest, NULL) >= 0) {
        fprintf(stderr, "Erreur : '%s' existe déjà\n", normalized_dest);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    char parent_path[MAX_PATH];
    char filename[MAX_FILENAME];
    extract_parent_path(normalized_dest, parent_path, MAX_PATH);
    extract_filename(normalized_dest, filename, MAX_FILENAME);

    if (!parent_exists(fs, parent_path)) {
        fprintf(stderr, "Erreur : le répertoire parent '%s' n'existe pas\n", parent_path);
        free(normalized_src);
        free(normalized_dest);
        return -1;
    }

    Inode *src_inode = get_inode(fs, src_idx);
    if (!src_inode->is_directory) {
        // Supprimer l'ancienne entree de la hash table
        hash_table_delete(fs, normalized_src);
        
        strncpy(src_inode->filename, filename, MAX_FILENAME - 1);
        src_inode->filename[MAX_FILENAME - 1] = '\0';
        strncpy(src_inode->parent_path, parent_path, MAX_PATH - 1);
        src_inode->parent_path[MAX_PATH - 1] = '\0';
        src_inode->modified = time(NULL);
        mark_inode_dirty(fs, src_idx);

        // Ajouter la nouvelle entree a la hash table
        hash_table_insert(fs, normalized_dest, src_idx);

        printf("Déplacé : %s -> %s\n", normalized_src, normalized_dest);
    } else {
        // Supprimer l'ancienne entree de la hash table
        hash_table_delete(fs, normalized_src);
        
        strncpy(src_inode->filename, filename, MAX_FILENAME - 1);
        src_inode->filename[MAX_FILENAME - 1] = '\0';
        strncpy(src_inode->parent_path, parent_path, MAX_PATH - 1);
        src_inode->parent_path[MAX_PATH - 1] = '\0';
        src_inode->modified = time(NULL);
        mark_inode_dirty(fs, src_idx);

        // Ajouter la nouvelle entree a la hash table
        hash_table_insert(fs, normalized_dest, src_idx);

        // Mettre a jour les entrees enfants dans la hash table
        for (int i = 0; i < MAX_FILES; i++) {
            Inode *inode = get_inode(fs, i);
            if (inode->filename[0] != '\0' && i != src_idx) {
                if (strncmp(inode->parent_path, normalized_src,
                           strlen(normalized_src)) == 0) {
                    char old_full_path[MAX_PATH];
                    snprintf(old_full_path, MAX_PATH, "%s/%s", inode->parent_path,
                            inode->filename);
                    
                    hash_table_delete(fs, old_full_path);
                    
                    char old_parent[MAX_PATH];
                    strncpy(old_parent, inode->parent_path, MAX_PATH - 1);

                    char new_parent[MAX_PATH];
                    size_t offset = strlen(normalized_src);
                    snprintf(new_parent, MAX_PATH, "%s%s", normalized_dest,
                            old_parent + offset);

                    strncpy(inode->parent_path, new_parent, MAX_PATH - 1);
                    inode->parent_path[MAX_PATH - 1] = '\0';
                    mark_inode_dirty(fs, i);
                    
                    // Ajouter la nouvelle entree a la hash table
                    char new_full_path[MAX_PATH];
                    snprintf(new_full_path, MAX_PATH, "%s/%s", new_parent,
                            inode->filename);
                    hash_table_insert(fs, new_full_path, i);
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

    int idx = path_exists(fs, normalized, NULL);
    if (idx != -1) {
        Inode *inode = get_inode(fs, idx);
        if (inode && !inode->is_directory) {
            fprintf(stderr, "Erreur : '%s' n'est pas un répertoire\n", normalized);
            free(normalized);
            return;
        }
    }

    if (depth == 0) {
        printf("\n=== Contenu du système de fichiers ===\n");
        printf("Répertoire : %s\n\n", normalized);
        printf("%-40s %12s %20s\n", "Nom", "Taille", "Date");
        printf("---------------------------------------------------------------------\n");
    }

    for (int i = 0; i < MAX_FILES; i++) {
        Inode *inode = get_inode(fs, i);
        if (inode->filename[0] != '\0' &&
            strcmp(inode->parent_path, normalized) == 0) {

            char time_str[20];
            struct tm *tm_info = localtime(&inode->modified);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

            char indent[64] = "";
            for (int j = 0; j < depth; j++) strcat(indent, "  ");

            if (inode->is_directory) {
                printf("%s%-38s %12s %20s\n", indent, inode->filename,
                       "[DIR]", time_str);
            } else {
                printf("%s%-38s %10lu B  %20s\n", indent,
                       inode->filename,
                       (unsigned long)inode->size, time_str);
            }
        }
    }

    if (depth == 0) printf("\n");
    free(normalized);
}
