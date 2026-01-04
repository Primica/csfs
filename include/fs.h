#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define FS_MAGIC 0x46534D47 // 'FSMG'
#define MAX_FILENAME 256
#define MAX_FILES 1024
#define BLOCK_SIZE 4096
#define MAX_PATH 2048
#define HASH_TABLE_SIZE 1024
#define LRU_CACHE_SIZE 128

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_files;
    uint32_t max_files;
    uint64_t data_offset;
} SuperBlock;

typedef struct {
    char filename[MAX_FILENAME];
    char parent_path[MAX_PATH];
    uint32_t is_directory;
    uint64_t size;
    uint64_t offset;
    time_t created;
    time_t modified;
} Inode;

typedef struct {
    int inode_index;      // Index dans la table d'Inodes (-1 si non utilise)
    char full_path[MAX_PATH];
} HashEntry;

typedef struct CacheNode {
    int inode_index;
    Inode inode;
    int dirty;
    struct CacheNode *prev;
    struct CacheNode *next;
} CacheNode;

typedef struct {
    FILE *container;
    SuperBlock sb;
    HashEntry hash_table[HASH_TABLE_SIZE];  // Index pour recherche rapide O(1)
    
    // Cache LRU
    CacheNode *cache_head;
    CacheNode *cache_tail;
    int cache_count;
    CacheNode *cache_nodes[LRU_CACHE_SIZE]; // Pour un accès rapide par index de cache
} FileSystem;

int fs_create(const char *path);
FileSystem *fs_open(const char *path);
void fs_close(FileSystem *fs);

int fs_mkdir(FileSystem *fs, const char *path);
int fs_add_file(FileSystem *fs, const char *fs_path, const char *source_path);
int fs_extract_file(FileSystem *fs, const char *fs_path, const char *dest_path);
int fs_copy_file(FileSystem *fs, const char *src_path, const char *dest_path);
int fs_move_file(FileSystem *fs, const char *src_path, const char *dest_path);
void fs_list(FileSystem *fs, const char *path);
void fs_list_recursive(FileSystem *fs, const char *path, int depth);

// Fonctions pour le cache d'inodes
Inode* get_inode(FileSystem *fs, int inode_index);
void mark_inode_dirty(FileSystem *fs, int inode_index);

#endif // FS_H
