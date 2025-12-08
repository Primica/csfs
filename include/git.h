#ifndef GIT_H
#define GIT_H

#include "fs.h"

// Git repository structure
typedef struct {
    char url[512];                  // Repository URL
    char name[256];                 // Repository name
    char clone_path[MAX_PATH];      // Clone path in FS
    int cloned;                     // Clone status
    char current_branch[256];       // Current branch
    char last_commit[41];           // Last commit hash
    char last_commit_msg[512];      // Last commit message
} GitRepository;

// Git manager structure
typedef struct {
    GitRepository *repos;
    int repo_count;
    int max_repos;
} GitManager;

// API functions
GitManager *git_manager_create(int max_repos);
void git_manager_destroy(GitManager *manager);

#endif // GIT_H
