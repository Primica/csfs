#ifndef SHELL_H
#define SHELL_H

#include "fs.h"
#include "git.h"

typedef struct {
    FileSystem *fs;
    char current_path[MAX_PATH];
    int running;
    GitManager *git_manager;        // Git support
} Shell;

Shell *shell_create(FileSystem *fs);
void shell_destroy(Shell *shell);
void shell_run(Shell *shell);
int shell_execute_command(Shell *shell, const char *cmd_line);

#endif // SHELL_H
