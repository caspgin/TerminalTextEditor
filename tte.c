/*** includes ***/
#define _GNU_SOURCE
#include <ctype.h>
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
#define TTE_QUIT_TIMES 3
#define TTE_SIDE_PANEL_WIDTH 5
#define TTE_MAX_FILENAME_DISPLAYED 20
//== == == == == == == == == == == == == == == == == == == == == == == ==
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

enum graphicPara {
    BACKGROUND = 48,
    FOREGROUND = 38,
    D_BACKGROUND = 49,
    D_FOREGROUND = 39,
    INVERT_FG_BG = 7,
    RESET = 0
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

struct writeBuf {
    char *pointer;
    int len;
};
struct writeBuf dLog = WRITEBUF_INIT;

//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** function declaration ***/

void editorClearScreen();
void debugFileLog();
void editorSetStatusMsg(char *fmt, ...);
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorAppendClrToBuf(struct writeBuf *wBuf, int code, int r, int g, int b);
//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** terminal ***/

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
    //  prints the error message msg and errno.
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

void debugFormat(char *fmt, ...) {
    char buf[128];
    va_list p;
    va_start(p, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, p);
    bufAppend(&dLog, buf, len);
    va_end(p);
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

//== == == == == == == == == == == == == == == == == == == == == == == ==
//==
/*** row operations ***/
void expandBuffer() {
    int newSize = (EC.rows_cap == 0) ? 1 : (EC.rows_cap * 2);
    EC.row = realloc(EC.row, sizeof(erow) * newSize);
    EC.rows_cap = newSize;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            cur_rx += (TTE_TAB_STOP - 1) - (cur_rx % TTE_TAB_STOP);
        cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return cx;
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

void editorInsertRow(char *data, size_t len, int insertAt) {
    if (insertAt < 0 || insertAt > EC.data_rows) return;
    EC.row = realloc(EC.row, sizeof(erow) * (EC.data_rows + 1));
    memmove(&EC.row[insertAt + 1], &EC.row[insertAt],
            sizeof(erow) * (EC.data_rows - insertAt));

    if (EC.data_rows >= EC.rows_cap) {
        expandBuffer();
    }

    EC.row[insertAt].size = len;
    EC.row[insertAt].chars = malloc(len + 1);
    memcpy(EC.row[insertAt].chars, data, len);
    EC.row[insertAt].chars[len] = '\0';
    EC.data_rows++;

    if (EC.max_data_cols < len) {
        EC.max_data_cols = len;
    }

    EC.row[insertAt].rsize = 0;
    EC.row[insertAt].render = NULL;
    editorUpdateRenderRow(&EC.row[insertAt]);
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

void editorRowDelChar(erow *row, int delAt) {
    if (delAt < 0 || delAt > row->size) return;
    memmove(&row->chars[delAt], &row->chars[delAt + 1], row->size - delAt);
    row->size--;
    editorUpdateRenderRow(row);
    EC.dirty = true;
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    row->render = NULL;
    row->chars = NULL;
}

void editorDelRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= EC.data_rows) return;
    editorFreeRow(&EC.row[rowIndex]);
    memmove(&EC.row[rowIndex], &EC.row[rowIndex + 1],
            sizeof(erow) * (EC.data_rows - rowIndex - 1));
    EC.data_rows--;
    EC.dirty = true;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRenderRow(row);
    EC.dirty = true;
}

//== == == == == == == == == == == == == == == == == == == == == == == ==
//==
/*** editor operations ***/
void editorInsertChar(int c) {
    if (EC.cy == EC.data_rows) {
        editorInsertRow("", 0, EC.data_rows);
    }
    editorRowInsertChar(&EC.row[EC.cy], EC.cx, c);
    EC.cx++;
}

void editorDelChar() {
    if (EC.cy == EC.data_rows) return;
    if (EC.cx == 0 && EC.cy == 0) return;
    erow *row = &EC.row[EC.cy];
    if (EC.cx > 0) {
        editorRowDelChar(row, EC.cx - 1);
        EC.cx--;
    } else {
        EC.cx = EC.row[EC.cy - 1].size;
        editorRowAppendString(&EC.row[EC.cy - 1], row->chars, row->size);
        editorDelRow(EC.cy);
        EC.cy--;
    }
}

void editorInsertNewline() {
    if (EC.cx == 0) {
        editorInsertRow("", 0, EC.cy);
    } else {
        erow *row = &EC.row[EC.cy];
        editorInsertRow(&row->chars[EC.cx], row->size - EC.cx, EC.cy + 1);
        row = &EC.row[EC.cy];
        row->size = EC.cx;
        row->chars[row->size] = '\0';
        editorUpdateRenderRow(row);
    }
    EC.cy++;
    EC.cx = 0;
}

//== == == == == == == == == == == == == == == == == == == == == == == ==
//==
/*** file i/o ***/

bool fileExists(char *filename) {
    if (filename == NULL) return false;

    int fd = open(filename, O_RDONLY);
    if (fd == -1 && errno == ENOENT) {
        return false;
    }
    close(fd);
    return true;
}

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

        editorInsertRow(line, linelen, EC.data_rows);

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
    if (EC.filename == NULL) {
        EC.filename = editorPrompt("save as:%s", NULL);
        if (EC.filename == NULL) {
            editorSetStatusMsg("save aborted");
            return;
        }
    }

    if (fileExists(EC.filename)) {
        char *response;
        response = editorPrompt(
            "File already exists. overwrite? Enter [Y]es or [N]o?%s", NULL);
        if (response == NULL || (response[0] != 'y' && response[0] != 'Y')) {
            editorSetStatusMsg("save aborted");
            free(EC.filename);
            EC.filename = NULL;
            return;
        }
    }

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

//== == == == == == == == == == == == == == == == == == == == == == == ==
//==

char *editorFindCallbackRowSearch(int *currRow, int lastMatchRow, int direction,
                                  char *buf) {
    erow *row = &EC.row[*currRow];
    int colIndex = lastMatchRow == *currRow ? editorRowCxtoRx(row, EC.cx) : 0;
    char *match = NULL;
    if (colIndex >= row->rsize) {
        *currRow += direction;
        return match;
    }
    if (direction == 1) {
        if (lastMatchRow == *currRow) colIndex++;
        if (colIndex >= row->rsize || colIndex < 0) return match;
        match = strstr(&row->render[colIndex], buf);
    } else {
        if (lastMatchRow != *currRow) colIndex = row->rsize - 1;
        int tempIndex = 0;
        char *prevMatch = NULL;
        debugNumber(tempIndex);
        while (true) {
            if (tempIndex >= row->rsize || tempIndex < 0) break;
            match = strstr(&row->render[tempIndex], buf);
            if (match && match < &row->render[colIndex]) {
                prevMatch = match;
                tempIndex = match - row->render + 1;
            } else {
                break;
            }
        }
        if (prevMatch) {
            match = prevMatch;
        } else {
            match = NULL;
        }
    }
    return match;
}

void editorFindCallback(char *buf, int c) {
    static int last_match = -1;
    static int direction = 1;

    if (c == ARROW_DOWN || c == ARROW_RIGHT) {
        direction = 1;
    } else if (c == ARROW_LEFT || c == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
        if (c == '\r' || c == '\x1b') {
            return;
        }
    }
    if (last_match == -1) direction = 1;
    int current = last_match;
    if (current == -1 && direction == 1) current += direction;

    for (int loopNum = 0; loopNum < EC.data_rows; loopNum++) {
        if (current == EC.data_rows)
            current = 0;
        else if (current == -1)
            current = EC.data_rows - 1;

        char *match =
            editorFindCallbackRowSearch(&current, last_match, direction, buf);

        if (match) {
            last_match = current;
            EC.cy = current;
            EC.cx = editorRowRxToCx(&EC.row[current],
                                    match - EC.row[current].render);
            // EC.rowoff = EC.data_rows;
            current += direction;
            break;
        }
        current += direction;
    }
}

void editorFind() {
    int saved_cx = EC.cx;
    int saved_cy = EC.cy;
    char *query =
        editorPrompt("Search: %s (ESC to cancel)", editorFindCallback);
    if (query) {
        free(query);
    } else {
        EC.cx = saved_cx;
        EC.cy = saved_cy;
    }
}
//== == == == == == == == == == == == == == == == == == == == == ==
//== == ==
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
    // Invert Foreground and background color for status bar
    editorAppendClrToBuf(wBuf, INVERT_FG_BG, 0, 0, 0);

    int totalCols = EC.screen_cols + TTE_SIDE_PANEL_WIDTH;
    const int LINE_NUM_LEN = 9;
    char *status = malloc(totalCols + 1);

    const char *dirty = EC.dirty ? "*" : " ";
    const char *fileName = EC.filename ? EC.filename : "[NO Name]";
    const char *longFNDots =
        (EC.filename && strlen(EC.filename) > TTE_MAX_FILENAME_DISPLAYED)
            ? "..."
            : "";

    // Limit FileNameLen to 20 characters.
    int fileNameLen = strlen(fileName);
    fileNameLen = fileNameLen > TTE_MAX_FILENAME_DISPLAYED
                      ? TTE_MAX_FILENAME_DISPLAYED - 3
                      : fileNameLen;

    int len = strlen(dirty) + fileNameLen + strlen(longFNDots);
    int spaces = totalCols - LINE_NUM_LEN - len;

    len = snprintf(status, totalCols + 1, "%s%*s%.03s%*s<%3d:%-3d ", dirty,
                   fileNameLen, fileName, longFNDots, spaces, "", EC.cy + 1,
                   EC.rx + 1);

    bufAppend(wBuf, status, len);
    // Reset all graphical rendering settings to default.
    editorAppendClrToBuf(wBuf, RESET, 0, 0, 0);
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

void editorAppendClrToBuf(struct writeBuf *wBuf, int code, int r, int g,
                          int b) {
    char buf[32];
    int len = 0;
    switch (code) {
        case FOREGROUND:
            len = snprintf(buf, sizeof(buf), "\x1b[%d;2;%d;%d;%dm", code, r, g,
                           b);
            break;
        case BACKGROUND:
            len = snprintf(buf, sizeof(buf), "\x1b[%d;2;%d;%d;%dm", code, r, g,
                           b);
            break;
        default:
            len = snprintf(buf, sizeof(buf), "\x1b[%dm", code);
            break;
    }
    bufAppend(wBuf, buf, len);
}

void editorDrawSidePanel(struct writeBuf *wBuf, const int lineNumber) {
    char sideBuf[TTE_SIDE_PANEL_WIDTH + 1];
    int len = snprintf(sideBuf, sizeof(sideBuf), "%4d ", lineNumber);

    editorAppendClrToBuf(wBuf, BACKGROUND, 31, 31, 40);
    bufAppend(wBuf, sideBuf, len);
    editorAppendClrToBuf(wBuf, D_BACKGROUND, 0, 0, 0);
}

void editorDrawRows(struct writeBuf *wBuf) {
    int screen_line_num;
    int data_line_num = EC.rowoff;
    for (screen_line_num = 0; screen_line_num < EC.screen_rows;
         screen_line_num++) {
        editorDrawSidePanel(wBuf, data_line_num + 1);
        if (!EC.wrap_mode) {
            clearLineRight(wBuf);
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
            float size = EC.row[data_line_num].size;
            int loopLen = ceil(size / EC.screen_cols);
            for (int i = 0; i < loopLen; i++) {
                clearLineRight(wBuf);

                int startIndex = i * EC.screen_cols;
                int len =
                    i != (loopLen - 1) ? EC.screen_cols : size - startIndex;

                bufAppend(wBuf, &EC.row[data_line_num].render[startIndex], len);
                bufAppend(wBuf, "\r\n", 2);
            }
            screen_line_num += loopLen - 1;
        }
        data_line_num++;
    }
}

void cursorToHome(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[H", 3); }

void cursorToPosition(struct writeBuf *wBuf) {
    char buf[32];

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (EC.cy - EC.rowoff) + 1,
             (EC.rx - EC.coloff) + 1 + TTE_SIDE_PANEL_WIDTH);
    bufAppend(wBuf, buf, strlen(buf));
}

void cursorToStatusPos(int x) {
    char buf[32];

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", EC.screen_cols, x);
    write(STDOUT_FILENO, buf, strlen(buf));
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

//== == == == == == == == == == == == == == == == == == == == == ==
//== == ==
/*** input ***/
void editorSetStatusMsg(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(EC.status_msg, sizeof(EC.status_msg), fmt, ap);
    va_end(ap);
    EC.status_msg_time = time(NULL);
}

void editorMoveCursor(int key) {
    // curRow points to the row(line) of data that the cursor is
    // currently on.
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

    // if curosr x position is greater than current rows size then
    // move it back to the last character. 1 character exception as
    // it will be needed to type new data.
    curRow = (EC.cy >= EC.data_rows) ? NULL : &EC.row[EC.cy];
    int curRowLen = curRow ? curRow->size : 0;
    if (EC.cx > curRowLen) {
        EC.cx = curRowLen;
    }

    // Setting curRow to NULL
    curRow = NULL;
}

void editorProcessKeyPress() {
    static int quit_times = TTE_QUIT_TIMES;
    int key_read = editorReadKey();

    switch (key_read) {
        case CTRL_KEY('f'):
            editorFind();
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('w'):
            EC.wrap_mode = !EC.wrap_mode;
            break;
        case CTRL_KEY('q'):
            if (EC.dirty && quit_times > 0) {
                editorSetStatusMsg(
                    "WARNING!!! File has unsaved changes. Press "
                    "CTRL-q %d "
                    "times to quit.",
                    quit_times);
                quit_times--;
                return;
            }
            editorClearScreen();
            debugFileLog();
            bufFree(&dLog);
            exit(EXIT_SUCCESS);
            break;

            // Backspace and delete
            // CTRL + H because it gives out code 8 which was used as
            // backspace in the olden days
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (key_read == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;
            // Carriage Return
        case '\r':
            editorInsertNewline();
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
            // CTRL + L, used for refreshing but we refresh at every
            // frame so not needed
        case CTRL_KEY('l'):
        case '\x1b':
            break;

            // Insert Characters
        default:
            editorInsertChar(key_read);
            break;
    }
    quit_times = TTE_QUIT_TIMES;
}

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';
    while (1) {
        editorSetStatusMsg(prompt, buf);
        editorRefreshScreen();
        // cursorToStatusPos(strlen(prompt) + buflen);
        int c = editorReadKey();
        if (c == '\x1b') {
            editorSetStatusMsg("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen > 0) {
                buflen--;
                buf[buflen] = '\0';
            }
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMsg("");
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if (callback) callback(buf, c);
    }
}
//== == == == == == == == == == == == == == == == == == == == == ==
//== == ==
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
    EC.screen_cols -= TTE_SIDE_PANEL_WIDTH;
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
