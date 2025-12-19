#ifndef FETCH_H
#define FETCH_H

#include "shell.h"

typedef void (*FetchPrinter)(Shell *shell, int color_enabled);

typedef struct {
    const char *name;
    const char *title;
    FetchPrinter printer;
} FetchModule;

// Print all or a selection of modules (names in `only`, or all if only_count==0)
int fetch_print(Shell *shell, const char **only, int only_count, int color_enabled);

// List available modules on stdout
void fetch_list_modules(void);

// Number of available modules
size_t fetch_count_modules(void);

#endif // FETCH_H
