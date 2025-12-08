#ifndef GIT_H
#define GIT_H

#include "fs.h"

// Maximum commits per repository
#define MAX_COMMITS 100

// Git commit structure
typedef struct {
    char hash[41];                  // Commit hash (40 chars + null)
    char message[512];              // Commit message
    char author[256];               // Author name (default: CSFS Git)
    char timestamp[64];             // Commit timestamp
    char branch[256];               // Branch name
} GitCommit;

// Git repository structure
typedef struct {
    char url[512];                  // Repository URL
    char name[256];                 // Repository name
    char clone_path[MAX_PATH];      // Clone path in FS
    int cloned;                     // Clone status
    char current_branch[256];       // Current branch
    
    // Commit history
    GitCommit *commits;             // Array of commits
    int commit_count;               // Number of commits
    int max_commits;                // Max commits capacity
    
    // Branch management
    char branches[20][256];         // List of branches
    int branch_count;               // Number of branches
    
    // Staging area
    char staged_files[50][MAX_PATH]; // Staged files for commit
    int staged_count;               // Number of staged files
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
