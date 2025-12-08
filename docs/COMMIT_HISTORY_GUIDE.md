# CSFS Git Commit History Implementation

## Overview

CSFS now has a fully functional Git commit history system that tracks commits, manages branches, handles staging areas, and stores commit information in the `.git` directory structure.

## Features Implemented

### 1. **Commit History Tracking**
- Each repository can store up to **100 commits** per repository
- Commits are stored in an array with complete metadata:
  - **Hash**: 40-character SHA1-like identifier
  - **Message**: Full commit message (up to 512 chars)
  - **Author**: Author name (default: "CSFS Git")
  - **Timestamp**: Full date/time of commit creation
  - **Branch**: Which branch the commit belongs to

### 2. **Branch Management**
- Up to 20 branches per repository
- Default branch is `main` when cloning
- Create branches with: `git branch <branch-name>`
- Switch branches with: `git checkout <branch-name>`
- List branches with: `git branch`
- Current branch marked with `*` in listing

### 3. **Staging Area**
- Supports up to 50 staged files per repository
- `git add <files>` adds files to staging area
- Staging area is cleared when switching branches
- `git status` shows all staged files

### 4. **.git Directory Structure**
CSFS automatically creates:
```
<repo>/.git/
├── objects/        # Repository objects
├── refs/          # Branch references
└── logs/
    └── HEAD       # Commit history log
```

## Usage Examples

### Clone a Repository
```bash
$ git clone https://github.com/octocat/Hello-World.git myrepo
Clonage depuis https://github.com/octocat/Hello-World.git...
  Dépôt : octocat/Hello-World
  Branche : master
  Téléchargement de l'archive complète...
Dépôt cloné : https://github.com/octocat/Hello-World.git -> myrepo
```

### Check Repository Status
```bash
$ cd myrepo
$ git status
Dépôt: Hello-World
Branch courante: main
URL: https://github.com/octocat/Hello-World.git
Chemin local: /path/to/myrepo

Historique:
  Total commits: 1
  Dernier commit: 6936eec769371f02deadbea66936af6e10d63af1
  Message: Initial commit

Branches disponibles:
  * main

Fichiers en staging (0):
  (aucun)
```

### View Commit History
```bash
$ git log
Historique du dépôt: Hello-World
Branch courante: main
-----
commit 6936eec769371f02deadbea66936af6e10d63af1
Author: CSFS Git
Date: Mon Dec 08 11:58:54 2025
Branch: main

    Initial commit
```

### Create and Switch Branches
```bash
$ git branch develop
Branch 'develop' créée

$ git branch feature/new
Branch 'feature/new' créée

$ git branch
* main
  develop
  feature/new

$ git checkout develop
Branche changée à: develop

$ git branch
  main
* develop
  feature/new
```

### Stage and Commit Files
```bash
$ git add myfile.txt
Fichiers en staging (1):
  + myfile.txt

$ git commit -m "Add new feature"
Commit créé: 098103a8098133e5deadbecd6936af753ab50c2a
Branch: develop
Message: Add new feature

$ git log
Historique du dépôt: Hello-World
Branch courante: develop
-----
commit 098103a8098133e5deadbecd6936af753ab50c2a
Author: CSFS Git
Date: Mon Dec 08 11:59:01 2025
Branch: develop

    Add new feature

commit 6936eec769371f02deadbea66936af6e10d63af1
Author: CSFS Git
Date: Mon Dec 08 11:58:54 2025
Branch: main

    Initial commit
```

## Internal Structure

### GitCommit Structure
```c
typedef struct {
    char hash[41];           // 40-char SHA1 identifier + null terminator
    char message[512];       // Commit message
    char author[256];        // Commit author
    char timestamp[64];      // Full timestamp
    char branch[256];        // Branch name
} GitCommit;
```

### GitRepository Enhanced Fields
```c
typedef struct {
    // ... existing fields ...
    
    // Commit history
    GitCommit *commits;      // Array of commits
    int commit_count;        // Current number of commits
    int max_commits;         // Maximum commits (100)
    
    // Branch management
    char branches[20][256];  // Array of branch names
    int branch_count;        // Number of branches
    char current_branch[256];// Currently checked-out branch
    
    // Staging area
    char staged_files[50][MAX_PATH];  // Staged file paths
    int staged_count;        // Number of staged files
} GitRepository;
```

## Implementation Details

### Commit Hash Generation
Commits are assigned unique 40-character hashes using:
1. Current timestamp
2. Commit message content
3. Random seed
4. XOR operations for uniqueness

Example hash:
```
6936eec769371f02deadbea66936af6e10d63af1
```

### Commit Log Storage
When commits are made, a log file is written to `.git/logs/HEAD`:
```
hash1 branch1 commit_message_1
hash2 branch2 commit_message_2
hash3 branch3 commit_message_3
```

### Branch Switching Behavior
When switching branches with `git checkout`:
1. Verifies the branch exists
2. Clears the staging area
3. Updates `current_branch` pointer
4. Displays confirmation message

## Limitations

- **Maximum 100 commits** per repository (can be increased via `MAX_COMMITS` constant)
- **Maximum 20 branches** per repository
- **Maximum 50 staged files** per repository
- Commit messages are truncated to 512 characters
- No merge conflict resolution implemented
- Branch history is per-branch but displayed chronologically across all branches

## Future Enhancements

1. **Merge capability**: Merge branches together
2. **Revert commits**: Revert to previous commits
3. **Stash support**: Save uncommitted changes temporarily
4. **Tags**: Create named release points
5. **diff support**: Show changes between commits
6. **blame**: Track who made which changes
7. **cherry-pick**: Apply specific commits to branches

## Testing the System

```bash
# Create a test filesystem
./csfs /tmp/test.csfs create

# Enter the shell
./csfs /tmp/test.csfs shell

# Test full workflow
mkdir /work
cd /work
git clone https://github.com/octocat/Hello-World.git repo
cd repo
git branch develop
git checkout develop
git commit -m "Work in progress"
git checkout main
git log
git branch
git status
exit
```

## Troubleshooting

**"git status: pas de dépôt trouvé"**
- You must be inside a cloned repository directory
- Use `cd` to navigate to the repository

**"git checkout: branche 'X' n'existe pas"**
- The branch doesn't exist yet
- Create it first with `git branch <branch-name>`

**"Commit créé: 098103..."**
- This is success! The commit has been created
- Use `git log` to see all commits

**".git/logs/HEAD exists already"**
- This is a minor warning when writing logs
- The commit was still created successfully
