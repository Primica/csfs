#include "../../include/fetch.h"
#include "../../include/fs.h"
#include "../../include/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/utsname.h>
#include <unistd.h>

#define MAX_LOGO_LINES 50
#define MAX_LINE_LENGTH 256
#define MAX_INFO_LINES 50

// ASCII art intégré directement dans le binaire
static const char *default_ascii[] = {
    "        ;++       ",
    "      ;;+++X;     ",
    "    :;;;;;XXXX    ",
    "    :::::XXXXXX   ",
    "   ::..::XXXXXX   ",
    "   $+   .Xxx+++   ",
    "  $$$X  .:++++++  ",
    " X$$$$X$&&$X+;;;+ ",
    ";XXXXX$&$$$$$$;;. ",
    "  XXXX$$$$$XXXX   ",
    "    XX$$XXXXXXX   ",
    "         ;XXXX    "
};
static const int default_ascii_count = sizeof(default_ascii) / sizeof(default_ascii[0]);

static char logo_lines[MAX_LOGO_LINES][MAX_LINE_LENGTH];
static int logo_line_count = 0;
static int logo_max_width = 0;

static char info_lines[MAX_INFO_LINES][MAX_LINE_LENGTH];
static int info_line_count = 0;

static void load_ascii_art(void) {
    // Utiliser l'ASCII art intégré par défaut
    logo_line_count = default_ascii_count;
    logo_max_width = 0;
    
    for (int i = 0; i < default_ascii_count; i++) {
        strncpy(logo_lines[i], default_ascii[i], MAX_LINE_LENGTH - 1);
        logo_lines[i][MAX_LINE_LENGTH - 1] = '\0';
        int len = strlen(logo_lines[i]);
        if (len > logo_max_width) logo_max_width = len;
    }
}

static void add_info_line(const char *text) {
    if (info_line_count < MAX_INFO_LINES) {
        strncpy(info_lines[info_line_count], text, MAX_LINE_LENGTH - 1);
        info_lines[info_line_count][MAX_LINE_LENGTH - 1] = '\0';
        info_line_count++;
    }
}

static void add_info_kv(const char *key, const char *value, int color) {
    char line[MAX_LINE_LENGTH];
    if (color) {
        snprintf(line, sizeof(line), "\033[1;36m%s:\033[0m %s", key, value);
    } else {
        snprintf(line, sizeof(line), "%s: %s", key, value);
    }
    add_info_line(line);
}

static void add_separator(int color) {
    if (color) {
        add_info_line("\033[90m────────────────────────────────\033[0m");
    } else {
        add_info_line("────────────────────────────────");
    }
}

static void collect_system_info(Shell *shell, int color) {
    (void)shell;
    struct utsname u;
    uname(&u);

    char host[256] = {0};
    gethostname(host, sizeof(host) - 1);

    // Username@hostname
    char *username = getenv("USER");
    if (username) {
        char userhost[512];
        snprintf(userhost, sizeof(userhost), "%s@%s", username, host);
        if (color) {
            char colored[1024];
            snprintf(colored, sizeof(colored), "\033[1;32m%s\033[0m", userhost);
            add_info_line(colored);
        } else {
            add_info_line(userhost);
        }
    }
    
    add_separator(color);
    
    add_info_kv("OS", u.sysname, color);
    add_info_kv("Kernel", u.release, color);
    add_info_kv("Arch", u.machine, color);
    
    add_separator(color);
}

static void collect_fs_info(Shell *shell, int color) {
    FileSystem *fs = shell->fs;
    unsigned long files = 0, dirs = 0;
    unsigned long long total = 0ULL;
    
    for (int i = 0; i < MAX_FILES; i++) {
        Inode *inode = get_inode(fs, i);
        if (inode->filename[0] != '\0') {
            if (inode->is_directory) dirs++;
            else {
                files++;
                total += inode->size;
            }
        }
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), "%u", fs->sb.version);
    add_info_kv("FS Version", buf, color);

    snprintf(buf, sizeof(buf), "%u/%u", fs->sb.num_files, fs->sb.max_files);
    add_info_kv("Inodes", buf, color);

    snprintf(buf, sizeof(buf), "%lu", dirs);
    add_info_kv("Directories", buf, color);

    snprintf(buf, sizeof(buf), "%lu", files);
    add_info_kv("Files", buf, color);

    if (total < 1024ULL) 
        snprintf(buf, sizeof(buf), "%llu B", total);
    else if (total < 1024ULL * 1024ULL) 
        snprintf(buf, sizeof(buf), "%.2f KiB", (double)total/1024.0);
    else if (total < 1024ULL * 1024ULL * 1024ULL) 
        snprintf(buf, sizeof(buf), "%.2f MiB", (double)total/(1024.0*1024.0));
    else 
        snprintf(buf, sizeof(buf), "%.2f GiB", (double)total/(1024.0*1024.0*1024.0));
    add_info_kv("Data Size", buf, color);
    
    add_separator(color);
    add_info_kv("CWD", shell->current_path, color);
    
    add_separator(color);
}

static void collect_colors(int color) {
    if (!color) {
        add_info_line("Colors: disabled");
        return;
    }
    
    char line[MAX_LINE_LENGTH];
    snprintf(line, sizeof(line), 
             "\033[40m   \033[41m   \033[42m   \033[43m   \033[44m   \033[45m   \033[46m   \033[47m   \033[0m");
    add_info_line(line);
}

static void render_side_by_side(int color) {
    int max_lines = (logo_line_count > info_line_count) ? logo_line_count : info_line_count;
    
    for (int i = 0; i < max_lines; i++) {
        // Print logo line (left side)
        if (i < logo_line_count) {
            if (color) {
                printf("\033[1;35m%s\033[0m", logo_lines[i]);
            } else {
                printf("%s", logo_lines[i]);
            }
            // Pad to align info
            int padding = logo_max_width - (int)strlen(logo_lines[i]);
            for (int j = 0; j < padding; j++) {
                printf(" ");
            }
        } else {
            // Pad empty logo lines
            for (int j = 0; j < logo_max_width; j++) {
                printf(" ");
            }
        }
        
        // Space between logo and info
        printf("   ");
        
        // Print info line (right side)
        if (i < info_line_count) {
            printf("%s", info_lines[i]);
        }
        
        printf("\n");
    }
}

void fetch_list_modules(void) {
    printf("fetch - Affiche les informations système style neofetch\n\n");
    printf("Utilisation: fetch [--no-color]\n");
}

int fetch_print(Shell *shell, const char **only, int only_count, int color_enabled) {
    (void)only;
    (void)only_count;
    
    load_ascii_art();
    
    info_line_count = 0;
    
    collect_system_info(shell, color_enabled);
    collect_fs_info(shell, color_enabled);
    collect_colors(color_enabled);
    
    render_side_by_side(color_enabled);
    
    return 0;
}

size_t fetch_count_modules(void) {
    return 1;
}
