#include "../../include/fetch.h"
#include "../../include/fs.h"
#include "../../include/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/utsname.h>
#include <unistd.h>

static int str_in_list(const char *name, const char **only, int only_count) {
    if (only_count == 0) return 1;
    for (int i = 0; i < only_count; i++) {
        if (strcmp(name, only[i]) == 0) return 1;
    }
    return 0;
}

static void print_kv(const char *k, const char *v, int color) {
    if (color) printf("\033[1m%-14s\033[0m %s\n", k, v);
    else printf("%-14s %s\n", k, v);
}

static void print_hr(int color) {
    if (color) printf("\033[90m----------------------------------------\033[0m\n");
    else printf("----------------------------------------\n");
}

// ============ Modules ============
static void mod_logo(Shell *shell, int color) {
    (void)shell;
    const char *lines[] = {
        "        _______        ",
        "       / _____ \\       ",
        "      / /     \\ \\      ",
        "     | |   C   | |     ",
        "      \\ \\_____/ /      ",
        "       \\_______/       "
    };
    for (size_t i = 0; i < sizeof(lines)/sizeof(lines[0]); i++) {
        if (color) printf("\033[34m%s\033[0m\n", lines[i]);
        else printf("%s\n", lines[i]);
    }
    print_hr(color);
}

static void mod_system(Shell *shell, int color) {
    (void)shell;
    struct utsname u;
    uname(&u);

    char host[256] = {0};
    gethostname(host, sizeof(host) - 1);

    time_t now = time(NULL);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    print_kv("Host", host, color);
    print_kv("OS", u.sysname, color);
    print_kv("Kernel", u.release, color);
    print_kv("Arch", u.machine, color);
    print_kv("Date", tbuf, color);
}

static void mod_fs(Shell *shell, int color) {
    (void)color;
    FileSystem *fs = shell->fs;
    unsigned long files = 0, dirs = 0;
    unsigned long long total = 0ULL;
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->inodes[i].filename[0] != '\0') {
            if (fs->inodes[i].is_directory) dirs++;
            else {
                files++;
                total += fs->inodes[i].size;
            }
        }
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%u", fs->sb.version);
    print_kv("FS Version", buf, color);

    snprintf(buf, sizeof(buf), "%u/%u", fs->sb.num_files, fs->sb.max_files);
    print_kv("Inodes", buf, color);

    snprintf(buf, sizeof(buf), "%lu", dirs);
    print_kv("Directories", buf, color);

    snprintf(buf, sizeof(buf), "%lu", files);
    print_kv("Files", buf, color);

    char sizebuf[64];
    if (total < 1024ULL) snprintf(sizebuf, sizeof(sizebuf), "%llu B", total);
    else if (total < 1024ULL * 1024ULL) snprintf(sizebuf, sizeof(sizebuf), "%.2f KiB", (double)total/1024.0);
    else if (total < 1024ULL * 1024ULL * 1024ULL) snprintf(sizebuf, sizeof(sizebuf), "%.2f MiB", (double)total/(1024.0*1024.0));
    else snprintf(sizebuf, sizeof(sizebuf), "%.2f GiB", (double)total/(1024.0*1024.0*1024.0));
    print_kv("Data Size", sizebuf, color);
}

static void mod_shell(Shell *shell, int color) {
    (void)color;
    print_kv("CWD", shell->current_path, color);
}

static void mod_colors(Shell *shell, int color) {
    (void)shell;
    if (!color) { print_kv("Colors", "disabled", color); return; }
    printf("Colors       ");
    printf("\033[30m██\033[0m ");
    printf("\033[31m██\033[0m ");
    printf("\033[32m██\033[0m ");
    printf("\033[33m██\033[0m ");
    printf("\033[34m██\033[0m ");
    printf("\033[35m██\033[0m ");
    printf("\033[36m██\033[0m ");
    printf("\033[37m██\033[0m\n");
}

static FetchModule modules[] = {
    {"logo",   "Logo CSFS",      mod_logo},
    {"system", "Système",        mod_system},
    {"fs",     "Système de fichiers", mod_fs},
    {"shell",  "Shell",          mod_shell},
    {"colors", "Palette",        mod_colors},
};

size_t fetch_count_modules(void) {
    return sizeof(modules)/sizeof(modules[0]);
}

void fetch_list_modules(void) {
    size_t n = fetch_count_modules();
    printf("Modules disponibles:\n\n");
    for (size_t i = 0; i < n; i++) {
        printf("  %-8s - %s\n", modules[i].name, modules[i].title);
    }
    printf("\nUtilisation: fetch [--list] [--no-color] [modules...]\n");
}

int fetch_print(Shell *shell, const char **only, int only_count, int color_enabled) {
    int printed_any = 0;
    size_t n = fetch_count_modules();
    for (size_t i = 0; i < n; i++) {
        if (!str_in_list(modules[i].name, only, only_count)) continue;
        modules[i].printer(shell, color_enabled);
        if (strcmp(modules[i].name, "logo") != 0) {
            // Separate modules (except right after the big logo divider)
            print_hr(color_enabled);
        }
        printed_any = 1;
    }
    if (!printed_any) {
        fprintf(stderr, "fetch: aucun module correspondant. Essayez --list.\n");
        return -1;
    }
    return 0;
}
