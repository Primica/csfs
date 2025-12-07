#ifndef MAN_H
#define MAN_H

typedef struct {
    const char *name;
    const char *synopsis;
    const char *description;
    const char *options;
    const char *examples;
    const char *see_also;
} ManPage;

void man_display(const char *command);
void man_list_all(void);
const ManPage *man_get_page(const char *command);

#endif // MAN_H
