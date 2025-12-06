#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define FS_MAGIC 0x46534D47
#define MAX_FILENAME 256
#define MAX_FILES 1024
#define BLOCK_SIZE 4096

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_files;
    uint32_t max_files;
    uint64_t data_offset;
} SuperBlock;

typedef struct {
    char filename[MAX_FILENAME];
    uint32_t is_directory;
    uint64_t size;
    uint64_t offset;
    time_t created;
    time_t modified;
} Inode;

typedef struct {
    FILE *container;
    SuperBlock sb;
    Inode inodes[MAX_FILES];
} FileSystem;

int fs_create(const char *path);
FileSystem *fs_open(const char *path);
void fs_close(FileSystem *fs);
int fs_add_file(FileSystem *fs, const char *filename, const char *source_path);
int fs_extract_file(FileSystem *fs, const char *filename, const char *dest_path);
void fs_list(FileSystem *fs);

#endif // FS_H
