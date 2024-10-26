/*** includes ***/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** defines ***/
#define TTE_VERSION "0.0.1"
#define TTE_TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)
#define WRITEBUF_INIT {NULL, 0}

//== == == == == == == == == == == == == == == == == == == == == == == ==
/*** function declaration ***/

void editorClearScreen();
void debugFileLog();
void editorSetStatusMsg(char *fmt, ...);
//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** data ***/

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN,
    HOME,
    END,
};

typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig {
    int cx;             // Cursor Position X in the buffer (chars)
    int cy;             // Cursor Position Y in the buffer (chars)
    int rx;             // Cursor Position X in the buffer (render)
    int ry;             // Cursor Position Y in the buffer (render)
    int rowoff;         // Offset row number
    int coloff;         // offset column number
    int screen_rows;    // Number of Rows on screen
    int screen_cols;    // Number of Columns on Screen
    int max_data_cols;  // longest row in the buffer.
    int data_rows;  // Actual number of data lines/rows i.e. Actual number of
                    // buffer row filled
    int rows_cap;   // Actual capacity of buffer
    erow *row;      // Array buffer
    bool wrap_mode;
    char *filename;
    char status_msg[80];
    time_t status_msg_time;
    bool dirty;
    struct termios org_termios;
};

struct editorConfig EC;

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** buffers ***/

struct writeBuf {
    char *pointer;
    int len;
};
struct writeBuf dLog = WRITEBUF_INIT;

// realocate new memory for additional characters to append of size appendLen to
// buffer wBuf
void bufAppend(struct writeBuf *wBuf, const char *appendSrc, int appendLen) {
    char *newBuf = realloc(wBuf->pointer, wBuf->len + appendLen);
    if (newBuf == NULL) {
        return;  // reallocation failed. original buffer still intact.
    }
    memcpy(&newBuf[wBuf->len], appendSrc, appendLen);
    wBuf->pointer = newBuf;
    wBuf->len += appendLen;
}

// dealocate memory using free function from stdlib
void bufFree(struct writeBuf *wBuf) { free(wBuf->pointer); }

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** terminal ***/
int editorRowCxtoRx(erow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') {
            rx += (TTE_TAB_STOP - 1) - (rx % TTE_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

// Error printing before exiting
void die(const char *msg) {
    editorClearScreen();
    // prints the error message msg and errno.
    perror(msg);
    debugFileLog();
    bufFree(&dLog);
    exit(EXIT_FAILURE);
}

// set the terminal attributes to original values
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &EC.org_termios) == -1) {
        die("tcsetattr");
    }
}

// enable raw mode by turning the ECHO OFF and saving the default values of
// terminal in org_termios
void enableRawMode() {
    // get the default value
    if (tcgetattr(STDIN_FILENO, &EC.org_termios) == -1) {
        die("tcgetattr");
    }
    // register the disabel function so it runs when program ends
    atexit(disableRawMode);

    struct termios raw = EC.org_termios;
    // turn off input flags
    // IXON - for pausing input
    // ICRNL - carriage return and new line
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON);
    raw.c_cflag |= (CS8);
    raw.c_oflag &= ~(OPOST);
    // Turn off local bits
    // ECHO - echos what you type
    // ICANON - canonical mode
    // ISIG - signals that interupt(SIGINT) or quit or suspend(SIGTSTP) the
    // terminal IEXTEN - Paste feature
    // TODO - Paste feature is not disabled yet figure out why
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // set the changed values of terminal
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

void debugMsg(char *msg) {
    char buf[32];
    int len = sprintf(buf, "%s\n", msg);
    bufAppend(&dLog, buf, len);
}

void debugNumber(int num) {
    char buf[32];
    int len = sprintf(buf, "%d\n", num);
    bufAppend(&dLog, buf, len);
}
int editorReadKey() {
    char char_read;
    int bytes_read = read(STDIN_FILENO, &char_read, 1);  // Read the first byte
    while (bytes_read != 1) {
        if (bytes_read == -1) {
            die("read");  // Program Ends, Error Handling Section
        }
        bytes_read = read(STDIN_FILENO, &char_read,
                          1);  // Read Again because nothing read
    }

    if (char_read == '\x1b') {  // if first byte is escape character
        char seq[3];

        // Read 2 more bytes
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {  // if seq[1] is a number
                // Read another character
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3':
                            return DEL_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'F':
                        return END;
                    case 'H':
                        return HOME;
                }
            }
        }
        return '\x1b';
    }
    return char_read;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 ||
        ws.ws_row == 0) {
        return -1;
    }

    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** row operations ***/

void expandBuffer() {
    int newSize = (EC.rows_cap == 0) ? 1 : (EC.rows_cap * 2);
    EC.row = realloc(EC.row, sizeof(erow) * newSize);
    EC.rows_cap = newSize;
}

void editorUpdateRenderRow(erow *row) {
    int tabs = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') tabs++;
    }

    free(row->render);
    row->render = malloc(row->size + tabs * (TTE_TAB_STOP - 1) + 1);
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TTE_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorRowAppend(char *data, size_t len) {
    if (EC.data_rows >= EC.rows_cap) {
        expandBuffer();
    }

    int line_num = EC.data_rows;
    EC.row[line_num].size = len;
    EC.row[line_num].chars = malloc(len + 1);
    memcpy(EC.row[line_num].chars, data, len);
    EC.row[line_num].chars[len] = '\0';
    EC.data_rows++;

    if (EC.max_data_cols < len) {
        EC.max_data_cols = len;
    }

    EC.row[line_num].rsize = 0;
    EC.row[line_num].render = NULL;
    editorUpdateRenderRow(&EC.row[line_num]);
    EC.dirty = true;
}

void editorRowInsertChar(erow *row, int insertAt, int c) {
    if (insertAt < 0 || insertAt > row->size) insertAt = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[insertAt + 1], &row->chars[insertAt],
            row->size - insertAt + 1);
    row->size++;
    row->chars[insertAt] = c;
    editorUpdateRenderRow(row);
    EC.dirty = true;
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** editor operations ***/
void editorInsertChar(int c) {
    if (EC.cy == EC.data_rows) {
        editorRowAppend("", 0);
    }
    editorRowInsertChar(&EC.row[EC.cy], EC.cx, c);
    EC.cx++;
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** file i/o ***/

void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    free(EC.filename);
    EC.filename = strdup(filename);

    char *line = NULL;
    size_t linecap = 0;
    size_t linelen;
    linelen = getline(&line, &linecap, fp);

    while (linelen != (size_t)-1) {
        while (linelen > 0 &&
               (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }

        editorRowAppend(line, linelen);

        linelen = getline(&line, &linecap, fp);
    }

    free(line);
    fclose(fp);
    EC.dirty = false;
}

void debugFileLog() {
    if (!dLog.len) {
        return;
    }
    FILE *fp = fopen("Log.txt", "w");
    if (!fp) die("dFileLog");

    fprintf(fp, "%s", dLog.pointer);

    fclose(fp);
}

char *editorRowsToString(int *buflen) {
    int totalLen = 0;
    for (int rowIndex = 0; rowIndex < EC.data_rows; rowIndex++)
        totalLen += EC.row[rowIndex].size + 1;

    *buflen = totalLen;

    char *outBuf = malloc(totalLen);
    char *rowPtr = outBuf;

    for (int rowIndex = 0; rowIndex < EC.data_rows; rowIndex++) {
        memcpy(rowPtr, EC.row[rowIndex].chars, EC.row[rowIndex].size);
        rowPtr += EC.row[rowIndex].size;
        *rowPtr = '\n';
        rowPtr++;
    }
    rowPtr = NULL;
    return outBuf;
}

void editorSave() {
    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(EC.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                editorSetStatusMsg("%d bytes written to disk. File is saved!",
                                   len);
                EC.dirty = false;
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMsg("Saving Failed! ERROR: %s", strerror(errno));
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** output ***/
void clearLineRight(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[K", 3); }

void printWelcomeMsg(struct writeBuf *wBuf) {
    char welcome[80];
    int welcomelen =
        snprintf(welcome, sizeof(welcome), "Terminal Text editor -- version %s",
                 TTE_VERSION);
    if (welcomelen > EC.screen_cols) {
        welcomelen = EC.screen_cols;
    }
    int padding = (EC.screen_cols - welcomelen) / 2;
    if (padding) {
        bufAppend(wBuf, "~", 1);
        padding--;
    }
    while (padding--) bufAppend(wBuf, " ", 1);
    bufAppend(wBuf, welcome, welcomelen);
}

void editorDrawStatusBar(struct writeBuf *wBuf) {
    bufAppend(wBuf, "\x1b[7m", 4);
    char status[30], lineNumber[30];
    int len =
        snprintf(status, sizeof(status), "%s%.20s%.03s", EC.dirty ? "*" : " ",
                 EC.filename ? EC.filename : "[NO Name]",
                 strlen(EC.filename) > 20 ? "..." : "");
    if (len > 30) len = 30;
    bufAppend(wBuf, status, len);

    int lineNumberLen =
        snprintf(lineNumber, sizeof(lineNumber), "<%3d:%-3d ", EC.cy, EC.cx);

    while (len < EC.screen_cols) {
        if (len == EC.screen_cols - lineNumberLen) {
            bufAppend(wBuf, lineNumber, lineNumberLen);
            break;
        }
        bufAppend(wBuf, " ", 1);
        len++;
    }

    bufAppend(wBuf, "\x1b[m", 3);
    bufAppend(wBuf, "\r\n", 2);
}

void editorDrawStatusMsgBar(struct writeBuf *wBuf) {
    clearLineRight(wBuf);
    int msgLen = strlen(EC.status_msg);
    if (msgLen > EC.screen_cols - 2) msgLen = EC.screen_cols - 2;
    int timeLeft = time(NULL) - EC.status_msg_time;
    bufAppend(wBuf, " ", 1);
    if (msgLen && timeLeft < 5) bufAppend(wBuf, EC.status_msg, msgLen);
}

void editorDrawRows(struct writeBuf *wBuf) {
    int screen_line_num;
    int data_line_num = 0;
    for (screen_line_num = 0; screen_line_num < EC.screen_rows;
         screen_line_num++) {
        if (!EC.wrap_mode) {
            clearLineRight(wBuf);
            data_line_num = screen_line_num + EC.rowoff;
            if (screen_line_num >= EC.data_rows) {
                if (EC.data_rows == 0 &&
                    screen_line_num == EC.screen_rows / 2) {
                    printWelcomeMsg(wBuf);
                } else {
                    bufAppend(wBuf, "~", 1);
                }
            } else {
                int len = EC.row[data_line_num].rsize - EC.coloff;
                if (len < 0)
                    len = 0;
                else if (len > EC.screen_cols)
                    len = EC.screen_cols;
                bufAppend(wBuf, &EC.row[data_line_num].render[EC.coloff], len);
            }

            bufAppend(wBuf, "\r\n", 2);
        } else {
            if (data_line_num > EC.data_rows) continue;
            int size = EC.row[data_line_num].size;
            int loopLen = ceil(size / EC.screen_cols);
            for (int i = 0; i < loopLen; i++) {
                clearLineRight(wBuf);

                int startIndex = i * EC.screen_cols;
                int len =
                    i != (loopLen - 1) ? EC.screen_cols : size - startIndex;

                bufAppend(wBuf, &EC.row[data_line_num].chars[startIndex], len);
                if (screen_line_num < EC.screen_rows - 1)
                    bufAppend(wBuf, "\r\n", 2);
                else
                    break;
                if (i != (loopLen - 1)) screen_line_num++;
            }
            data_line_num++;
        }
    }
}

void cursorToHome(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[H", 3); }

void cursorToPosition(struct writeBuf *wBuf) {
    char buf[32];

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (EC.cy - EC.rowoff) + 1,
             (EC.rx - EC.coloff) + 1);
    bufAppend(wBuf, buf, strlen(buf));
}

void editorClearScreen() { write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7); }

void hideCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25l", 6); }

void showCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25h", 6); }

void editorScroll() {
    EC.rx = 0;
    if (EC.cy < EC.data_rows) {
        EC.rx = editorRowCxtoRx(&EC.row[EC.cy], EC.cx);
    }

    // Update Row offset
    if (EC.cy < EC.rowoff) {
        EC.rowoff = EC.cy;
    }
    if (EC.cy >= (EC.rowoff + EC.screen_rows)) {
        EC.rowoff = EC.cy - EC.screen_rows + 1;
    }

    // Update Coll offset
    if (EC.rx < EC.coloff) {
        EC.coloff = EC.rx;
    }

    if (EC.rx >= (EC.coloff + EC.screen_cols)) {
        EC.coloff = EC.rx - EC.screen_cols + 1;
    }
}

void editorRefreshScreen() {
    editorScroll();
    struct writeBuf wBuf = WRITEBUF_INIT;

    hideCursor(&wBuf);
    cursorToHome(&wBuf);

    editorDrawRows(&wBuf);
    editorDrawStatusBar(&wBuf);
    editorDrawStatusMsgBar(&wBuf);

    cursorToPosition(&wBuf);
    showCursor(&wBuf);

    write(STDOUT_FILENO, wBuf.pointer, wBuf.len);
    bufFree(&wBuf);
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** input ***/
void editorSetStatusMsg(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(EC.status_msg, sizeof(EC.status_msg), fmt, ap);
    va_end(ap);
    EC.status_msg_time = time(NULL);
}

void editorMoveCursor(int key) {
    // curRow points to the row(line) of data that the cursor is currently on.
    erow *curRow = (EC.cy >= EC.data_rows) ? NULL : &EC.row[EC.cy];

    // Update Curosor Position
    switch (key) {
        case ARROW_LEFT:
            if (EC.cx > 0) {
                EC.cx--;
            } else if (EC.cy > 0) {
                EC.cy--;
                EC.cx = EC.row[EC.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (curRow && EC.cx < curRow->size) {
                EC.cx++;
            } else if (EC.cy < EC.data_rows) {
                EC.cy++;
                EC.cx = 0;
            }
            break;
        case ARROW_DOWN:
            if (EC.cy < EC.data_rows) EC.cy++;
            break;
        case ARROW_UP:
            if (EC.cy > 0) EC.cy--;
            break;
        case PAGE_UP: {
            int newYPos = EC.cy - EC.screen_rows;
            EC.cy = newYPos >= 0 ? newYPos : 0;
        } break;
        case PAGE_DOWN: {
            int newYPos = EC.cy + EC.screen_rows;
            EC.cy = newYPos <= EC.data_rows ? newYPos : EC.data_rows;
        } break;
        case END:
            EC.cx = curRow->size;
            break;
        case HOME:
            EC.cx = 0;
            break;
    }

    // if curosr x position is greater than current rows size then move it back
    // to the last character. 1 character exception as it will be needed to type
    // new data.
    curRow = (EC.cy >= EC.data_rows) ? NULL : &EC.row[EC.cy];
    int curRowLen = curRow ? curRow->size : 0;
    if (EC.cx > curRowLen) {
        EC.cx = curRowLen;
    }

    // Setting curRow to NULL
    curRow = NULL;
}

void editorProcessKeyPress() {
    int key_read = editorReadKey();

    switch (key_read) {
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('q'):
            editorClearScreen();
            debugFileLog();
            bufFree(&dLog);
            exit(EXIT_SUCCESS);
            break;

            // Backspace and delete
            // CTRL + H because it gives out code 8 which was used as backspace
            // in the olden days
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            break;
            // Carriage Return
        case '\r':
            break;
            // Move Cursor
        case END:
        case HOME:
        case PAGE_UP:
        case PAGE_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_DOWN:
        case ARROW_UP:
            editorMoveCursor(key_read);
            break;
            // escape
            // CTRL + L, used for refreshing but we refresh at every frame so
            // not needed
        case CTRL_KEY('l'):
        case '\x1b':
            break;

            // Insert Characters
        default:
            editorInsertChar(key_read);
            break;
    }
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** init ***/

void initEditor() {
    EC.cx = 0;
    EC.cy = 0;
    EC.rx = 0;
    EC.ry = 0;
    EC.rowoff = 0;
    EC.coloff = 0;
    EC.data_rows = 0;
    EC.rows_cap = 0;
    EC.row = NULL;
    EC.dirty = false;
    EC.wrap_mode = false;
    if (getWindowSize(&EC.screen_rows, &EC.screen_cols) == -1) {
        die("getWindowSize");
    }
    EC.max_data_cols = EC.screen_cols;
    EC.screen_rows -= 2;
    EC.filename = NULL;
    EC.status_msg[0] = '\0';
    EC.status_msg_time = 0;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    editorSetStatusMsg("HELP: CTRL-s to save | CTRL-q to quit");
    while (true) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }
    return 0;
}
