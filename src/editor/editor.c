#include "../../include/editor.h"
#include "../../include/fs.h"
#include "../../include/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/ioctl.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_VERSION "1.0"
#define EDITOR_TAB_STOP 4

enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

typedef struct EditorRow {
    int size;
    char *chars;
    int rsize;
    char *render;
} EditorRow;

typedef struct EditorConfig {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    EditorRow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    char cmdbuf[80];
    int cmdmode;
    int insert_mode;
    int quit;
    struct termios orig_termios;
    Shell *shell;
} EditorConfig;

static EditorConfig E;

// ==================== Terminal ====================

static void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

static void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

static int read_key(void) {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

static int get_cursor_position(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

static int get_window_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// ==================== Row Operations ====================

static int cx_to_rx(EditorRow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (EDITOR_TAB_STOP - 1) - (rx % EDITOR_TAB_STOP);
        rx++;
    }
    return rx;
}

// static int rx_to_cx(EditorRow *row, int rx) {
//     int cur_rx = 0;
//     int cx;
//     for (cx = 0; cx < row->size; cx++) {
//         if (row->chars[cx] == '\t')
//             cur_rx += (EDITOR_TAB_STOP - 1) - (cur_rx % EDITOR_TAB_STOP);
//         cur_rx++;
//         if (cur_rx > rx) return cx;
//     }
//     return cx;
// }

static void update_row(EditorRow *row) {
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (EDITOR_TAB_STOP - 1) + 1);

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % EDITOR_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

static void insert_row(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(EditorRow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(EditorRow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    update_row(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

static void free_row(EditorRow *row) {
    free(row->render);
    free(row->chars);
}

static void del_row(int at) {
    if (at < 0 || at >= E.numrows) return;
    free_row(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(EditorRow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

static void row_insert_char(EditorRow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    update_row(row);
    E.dirty++;
}

static void row_append_string(EditorRow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    update_row(row);
    E.dirty++;
}

static void row_del_char(EditorRow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    update_row(row);
    E.dirty++;
}

// ==================== Editor Operations ====================

static void insert_char(int c) {
    if (E.cy == E.numrows) {
        insert_row(E.numrows, "", 0);
    }
    row_insert_char(&E.row[E.cy], E.cx, c);
    E.cx++;
}

static void insert_newline(void) {
    if (E.cx == 0) {
        insert_row(E.cy, "", 0);
    } else {
        EditorRow *row = &E.row[E.cy];
        insert_row(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        update_row(row);
    }
    E.cy++;
    E.cx = 0;
}

static void del_char(void) {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    EditorRow *row = &E.row[E.cy];
    if (E.cx > 0) {
        row_del_char(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        row_append_string(&E.row[E.cy - 1], row->chars, row->size);
        del_row(E.cy);
        E.cy--;
    }
}

// ==================== File I/O ====================

static char *rows_to_string(int *buflen) {
    int totlen = 0;
    for (int j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

static void load_from_fs(void) {
    // Résoudre le chemin dans le FS
    char resolved[MAX_PATH];
    if (E.filename[0] == '/') {
        strncpy(resolved, E.filename, sizeof(resolved) - 1);
    } else if (strcmp(E.shell->current_path, "/") == 0) {
        snprintf(resolved, sizeof(resolved), "/%s", E.filename);
    } else {
        snprintf(resolved, sizeof(resolved), "%s/%s", E.shell->current_path, E.filename);
    }
    resolved[sizeof(resolved) - 1] = '\0';

    // Chercher l'inode
    int idx = -1;
    for (int i = 0; i < E.shell->fs->sb.max_files; i++) {
        Inode *inode = get_inode(E.shell->fs, i);
        if (inode->filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(inode->parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", inode->filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s", 
                         inode->parent_path,
                         inode->filename);
            }
            if (strcmp(full_path, resolved) == 0) {
                idx = i;
                break;
            }
        }
    }

    if (idx == -1 || get_inode(E.shell->fs, idx)->is_directory) {
        // Nouveau fichier ou répertoire
        snprintf(E.statusmsg, sizeof(E.statusmsg), "Nouveau fichier");
        return;
    }

    Inode *inode = get_inode(E.shell->fs, idx);
    fseek(E.shell->fs->container, (long)inode->offset, SEEK_SET);

    // char *line = NULL;
    // size_t linecap = 0;
    // ssize_t linelen;
    
    // Lire le contenu en mémoire
    char *content = malloc(inode->size + 1);
    fread(content, 1, inode->size, E.shell->fs->container);
    content[inode->size] = '\0';

    // Parser ligne par ligne
    char *p = content;
    char *start = content;
    while (p - content < (long)inode->size) {
        if (*p == '\n' || p - content == (long)inode->size) {
            insert_row(E.numrows, start, p - start);
            start = p + 1;
        }
        p++;
    }
    // Dernière ligne sans newline
    if (start < p) {
        insert_row(E.numrows, start, p - start);
    }

    free(content);
    E.dirty = 0;
    snprintf(E.statusmsg, sizeof(E.statusmsg), 
             "%d lignes chargées", E.numrows);
}

static int save_to_fs(void) {
    if (E.filename == NULL) return -1;

    int len;
    char *buf = rows_to_string(&len);

    // Résoudre le chemin
    char resolved[MAX_PATH];
    if (E.filename[0] == '/') {
        strncpy(resolved, E.filename, sizeof(resolved) - 1);
    } else if (strcmp(E.shell->current_path, "/") == 0) {
        snprintf(resolved, sizeof(resolved), "/%s", E.filename);
    } else {
        snprintf(resolved, sizeof(resolved), "%s/%s", E.shell->current_path, E.filename);
    }
    resolved[sizeof(resolved) - 1] = '\0';

    // Créer un fichier temporaire
    char tmpfile[] = "/tmp/csfs_edit_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd == -1) {
        free(buf);
        return -1;
    }
    
    if (write(fd, buf, len) != len) {
        close(fd);
        unlink(tmpfile);
        free(buf);
        return -1;
    }
    close(fd);

    // Supprimer l'ancien si existe
    for (int i = 0; i < MAX_FILES; i++) {
        Inode *inode = get_inode(E.shell->fs, i);
        if (inode->filename[0] != '\0') {
            char full_path[MAX_PATH];
            if (strcmp(inode->parent_path, "/") == 0) {
                snprintf(full_path, MAX_PATH, "/%s", inode->filename);
            } else {
                snprintf(full_path, MAX_PATH, "%s/%s",
                         inode->parent_path,
                         inode->filename);
            }
            if (strcmp(full_path, resolved) == 0) {
                inode->filename[0] = '\0';
                mark_inode_dirty(E.shell->fs, i);
                E.shell->fs->sb.num_files--;
                break;
            }
        }
    }

    // Ajouter le nouveau fichier
    int ret = fs_add_file(E.shell->fs, resolved, tmpfile);
    unlink(tmpfile);
    free(buf);

    if (ret == 0) {
        E.dirty = 0;
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "%d octets écrits", len);
        return 0;
    }
    return -1;
}

// ==================== Output ====================

static void scroll(void) {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = cx_to_rx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

struct AppendBuffer {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

static void ab_append(struct AppendBuffer *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

static void ab_free(struct AppendBuffer *ab) {
    free(ab->b);
}

static void draw_rows(struct AppendBuffer *ab) {
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "CSFS Editor -- version %s", EDITOR_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    ab_append(ab, "~", 1);
                    padding--;
                }
                while (padding--) ab_append(ab, " ", 1);
                ab_append(ab, welcome, welcomelen);
            } else {
                ab_append(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            ab_append(ab, &E.row[filerow].render[E.coloff], len);
        }

        ab_append(ab, "\x1b[K", 3);
        ab_append(ab, "\r\n", 2);
    }
}

static void draw_status_bar(struct AppendBuffer *ab) {
    ab_append(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    const char *mode = E.cmdmode ? "-- COMMANDE --" : (E.insert_mode ? "-- INSERT --" : "-- NORMAL --");
    int len = snprintf(status, sizeof(status), "%.20s - %d lignes %s [%s]",
        E.filename ? E.filename : "[Pas de nom]", E.numrows,
        E.dirty ? "(modifié)" : "", mode);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
        E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols;
    ab_append(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            ab_append(ab, rstatus, rlen);
            break;
        } else {
            ab_append(ab, " ", 1);
            len++;
        }
    }
    ab_append(ab, "\x1b[m", 3);
    ab_append(ab, "\r\n", 2);
}

static void draw_message_bar(struct AppendBuffer *ab) {
    ab_append(ab, "\x1b[K", 3);
    if (E.cmdmode) {
        ab_append(ab, E.cmdbuf, strlen(E.cmdbuf));
    } else {
        int msglen = strlen(E.statusmsg);
        if (msglen > E.screencols) msglen = E.screencols;
        ab_append(ab, E.statusmsg, msglen);
    }
}

static void refresh_screen(void) {
    scroll();

    struct AppendBuffer ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H", 3);

    draw_rows(&ab);
    draw_status_bar(&ab);
    draw_message_bar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (E.cy - E.rowoff) + 1,
             (E.rx - E.coloff) + 1);
    ab_append(&ab, buf, strlen(buf));

    ab_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

static void set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
}

static void move_cursor(int key);

static void process_normal_mode_key(int c) {
    switch (c) {
        case 'h':
            move_cursor(ARROW_LEFT);
            break;
        case 'j':
            move_cursor(ARROW_DOWN);
            break;
        case 'k':
            move_cursor(ARROW_UP);
            break;
        case 'l':
            move_cursor(ARROW_RIGHT);
            break;
        case 'i':
            E.insert_mode = 1;
            set_status_message("");
            break;
        case 'I':
            E.cx = 0;
            E.insert_mode = 1;
            set_status_message("");
            break;
        case 'a':
            if (E.cy < E.numrows && E.cx < E.row[E.cy].size) {
                E.cx++;
            }
            E.insert_mode = 1;
            set_status_message("");
            break;
        case 'A':
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
            }
            E.insert_mode = 1;
            set_status_message("");
            break;
        case 'o':
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
            }
            insert_newline();
            E.insert_mode = 1;
            set_status_message("");
            break;
        case 'O':
            if (E.cx != 0 || E.cy != 0) {
                E.cx = 0;
                insert_newline();
                E.cy--;
            } else {
                insert_row(0, "", 0);
            }
            E.insert_mode = 1;
            set_status_message("");
            break;
        case 'x':
            if (E.cy < E.numrows && E.cx < E.row[E.cy].size) {
                move_cursor(ARROW_RIGHT);
                del_char();
            }
            break;
        case 'd':
            if (read_key() == 'd') {
                if (E.numrows > 0) {
                    del_row(E.cy);
                    if (E.cy >= E.numrows && E.cy > 0) {
                        E.cy--;
                    }
                    E.cx = 0;
                }
            }
            break;
        case '0':
            E.cx = 0;
            break;
        case '$':
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
            }
            break;
        case 'g':
            if (read_key() == 'g') {
                E.cy = 0;
                E.cx = 0;
            }
            break;
        case 'G':
            E.cy = E.numrows > 0 ? E.numrows - 1 : 0;
            E.cx = 0;
            break;
        case ':':
            E.cmdmode = 1;
            strcpy(E.cmdbuf, ":");
            break;
    }
}

static void process_command(void) {
    if (strcmp(E.cmdbuf, ":q") == 0) {
        if (E.dirty) {
            set_status_message("Fichier non sauvegardé! Utilisez :q! pour forcer ou :wq pour sauvegarder.");
            return;
        }
        E.quit = 1;
    } else if (strcmp(E.cmdbuf, ":q!") == 0) {
        E.quit = 1;
    } else if (strcmp(E.cmdbuf, ":w") == 0) {
        if (save_to_fs() == 0) {
            set_status_message("Fichier sauvegardé");
        } else {
            set_status_message("Erreur de sauvegarde!");
        }
    } else if (strcmp(E.cmdbuf, ":wq") == 0) {
        if (save_to_fs() == 0) {
            E.quit = 1;
        } else {
            set_status_message("Erreur de sauvegarde!");
        }
    } else if (strcmp(E.cmdbuf, ":x") == 0) {
        if (E.dirty) {
            if (save_to_fs() != 0) {
                set_status_message("Erreur de sauvegarde!");
                return;
            }
        }
        E.quit = 1;
    } else if (E.cmdbuf[0] == ':') {
        set_status_message("Commande inconnue: %s", E.cmdbuf);
    }
}

// ==================== Input ====================

static void move_cursor(int key) {
    EditorRow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

static void process_keypress(void) {
    int c = read_key();

    // Mode commande
    if (E.cmdmode) {
        switch (c) {
            case '\r':
                E.cmdmode = 0;
                process_command();
                E.cmdbuf[0] = '\0';
                break;
            case '\x1b':
                E.cmdmode = 0;
                E.cmdbuf[0] = '\0';
                set_status_message("");
                break;
            case BACKSPACE:
            case CTRL_KEY('h'):
            case DEL_KEY:
                if (strlen(E.cmdbuf) > 1) {
                    E.cmdbuf[strlen(E.cmdbuf) - 1] = '\0';
                }
                break;
            default:
                if (!iscntrl(c) && c < 128) {
                    size_t len = strlen(E.cmdbuf);
                    if (len < sizeof(E.cmdbuf) - 1) {
                        E.cmdbuf[len] = c;
                        E.cmdbuf[len + 1] = '\0';
                    }
                }
                break;
        }
        return;
    }

    // Mode normal
    if (!E.insert_mode) {
        switch (c) {
            case CTRL_KEY('q'):
                if (E.dirty) {
                    set_status_message("ATTENTION: fichier non sauvegardé! "
                        "Ctrl-Q pour forcer, :q! ou :wq.");
                    if (read_key() == CTRL_KEY('q')) {
                        E.quit = 1;
                    }
                    set_status_message("");
                    return;
                }
                E.quit = 1;
                break;
            case CTRL_KEY('s'):
                if (save_to_fs() == 0) {
                    set_status_message("Sauvegardé!");
                } else {
                    set_status_message("Erreur de sauvegarde!");
                }
                break;
            case ARROW_UP:
            case ARROW_DOWN:
            case ARROW_LEFT:
            case ARROW_RIGHT:
                move_cursor(c);
                break;
            case PAGE_UP:
            case PAGE_DOWN:
                {
                    if (c == PAGE_UP) {
                        E.cy = E.rowoff;
                    } else if (c == PAGE_DOWN) {
                        E.cy = E.rowoff + E.screenrows - 1;
                        if (E.cy > E.numrows) E.cy = E.numrows;
                    }
                    int times = E.screenrows;
                    while (times--)
                        move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;
            default:
                process_normal_mode_key(c);
                break;
        }
        return;
    }

    // Mode insert
    switch (c) {
        case '\x1b':
            E.insert_mode = 0;
            if (E.cx > 0) E.cx--;
            set_status_message("");
            break;

        case '\r':
            insert_newline();
            break;

        case CTRL_KEY('q'):
            if (E.dirty) {
                set_status_message("ATTENTION: fichier non sauvegardé! "
                    "Ctrl-Q pour forcer, :q! ou :wq.");
                if (read_key() == CTRL_KEY('q')) {
                    E.quit = 1;
                }
                set_status_message("");
                return;
            }
            E.quit = 1;
            break;

        case CTRL_KEY('s'):
            if (save_to_fs() == 0) {
                set_status_message("Sauvegardé!");
            } else {
                set_status_message("Erreur de sauvegarde!");
            }
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].size;
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) move_cursor(ARROW_RIGHT);
            del_char();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }

                int times = E.screenrows;
                while (times--)
                    move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            move_cursor(c);
            break;

        case CTRL_KEY('l'):
            break;

        default:
            insert_char(c);
            break;
    }
}

// ==================== Init ====================

static void init_editor(Shell *shell, const char *filename) {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = filename ? strdup(filename) : NULL;
    E.statusmsg[0] = '\0';
    E.cmdbuf[0] = '\0';
    E.cmdmode = 0;
    E.insert_mode = 0;
    E.quit = 0;
    E.shell = shell;

    if (get_window_size(&E.screenrows, &E.screencols) == -1) die("get_window_size");
    E.screenrows -= 2;
}

int editor_open(Shell *shell, const char *fs_path) {
    init_editor(shell, fs_path);
    load_from_fs();
    
    enable_raw_mode();
    
    set_status_message(
        "MODE NORMAL: i=insert | :w=sauvegarder | :q=quitter | :wq=sauv+quit | hjkl=navigation");

    while (!E.quit) {
        refresh_screen();
        process_keypress();
    }

    // Restaurer le mode terminal avant de quitter
    disable_raw_mode();
    
    // Nettoyage de l'écran avant de quitter
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    
    // Libérer la mémoire
    for (int i = 0; i < E.numrows; i++) {
        free_row(&E.row[i]);
    }
    free(E.row);
    if (E.filename) free(E.filename);

    return 0;
}
