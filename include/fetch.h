#ifndef FETCH_H
#define FETCH_H

#include "shell.h"

typedef void (*FetchPrinter)(Shell *shell, int color_enabled);

typedef struct {
    const char *name;
    const char *title;
    FetchPrinter printer;
} FetchModule;

int fetch_print(Shell *shell, const char **only, int only_count, int color_enabled);

void fetch_list_modules(void);

size_t fetch_count_modules(void);

#endif // FETCH_H
