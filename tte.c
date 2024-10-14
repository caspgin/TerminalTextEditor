/*** includes ***/
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** defines ***/
#define TTE_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
#define WRITEBUF_INIT \
    { NULL, 0 }

//== == == == == == == == == == == == == == == == == == == == == == == ==
/*** function declaration ***/

void editorClearScreen();
void debugFileLog();
//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** data ***/

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY
};

typedef struct erow {
    size_t size;
    char *chars;
} erow;

struct editorConfig {
    int cx;             // Cursor Position X in the buffer
    int cy;             // Cursor Position Y in the buffer
    int rowoff;         // Offset row number
    int coloff;         // offset column number
    int screen_rows;    // Number of Rows on screen
    int screen_cols;    // Number of Columns on Screen
    int max_data_cols;  // longest row in the buffer.
    int data_rows;  // Actual number of data lines/rows i.e. Actual number of
                    // buffer row filled
    int rows_cap;   // Actual capacity of buffer
    erow *row;      // Array Buffer
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
    int bytes_read = read(STDIN_FILENO, &char_read, 1);
    while (bytes_read != 1) {
        if (bytes_read == -1) {
            die("read");  // Program Ends, Error Handling Section
        }
        bytes_read = read(STDIN_FILENO, &char_read,
                          1);  // Read Again because nothing read
    }

    if (char_read == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3':
                            return DEL_KEY;
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
/*** input ***/

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
        case CTRL_KEY('q'):
            editorClearScreen();
            debugFileLog();
            bufFree(&dLog);
            exit(EXIT_SUCCESS);
            break;
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_DOWN:
        case ARROW_UP:
            editorMoveCursor(key_read);
            break;
    }
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** row operations ***/

void expandBuffer() {
    int newSize = (EC.rows_cap == 0) ? 1 : (EC.rows_cap * 2);
    EC.row = realloc(EC.row, sizeof(erow) * newSize);
    EC.rows_cap = newSize;
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
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** file i/o ***/

void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

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
}

void debugFileLog() {
    FILE *fp = fopen("Log.txt", "w");
    if (!fp) die("dFileLog");

    fprintf(fp, "%s", dLog.pointer);

    fclose(fp);
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

void editorDrawRows(struct writeBuf *wBuf) {
    int screen_line_num;
    for (screen_line_num = 0; screen_line_num < EC.screen_rows;
         screen_line_num++) {
        clearLineRight(wBuf);
        int data_line_num = screen_line_num + EC.rowoff;
        if (screen_line_num >= EC.data_rows) {
            if (EC.data_rows == 0 && screen_line_num == EC.screen_rows / 2) {
                printWelcomeMsg(wBuf);
            } else {
                bufAppend(wBuf, "~", 1);
            }
        } else {
            int len = EC.row[data_line_num].size - EC.coloff;
            if (len < 0)
                len = 0;
            else if (len > EC.screen_cols)
                len = EC.screen_cols;
            bufAppend(wBuf, &EC.row[data_line_num].chars[EC.coloff], len);
        }
        if (screen_line_num < EC.screen_rows - 1) {
            bufAppend(wBuf, "\r\n", 2);
        }
    }
}

void cursorToHome(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[H", 3); }

void cursorToPosition(struct writeBuf *wBuf) {
    char buf[32];

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (EC.cy - EC.rowoff) + 1,
             (EC.cx - EC.coloff) + 1);
    bufAppend(wBuf, buf, strlen(buf));
}

void editorClearScreen() { write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7); }

void hideCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25l", 6); }

void showCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25h", 6); }

void editorScroll() {
    // Update Row offset
    if (EC.cy < EC.rowoff) {
        EC.rowoff = EC.cy;
    }
    if (EC.cy >= (EC.rowoff + EC.screen_rows)) {
        EC.rowoff = EC.cy - EC.screen_rows + 1;
    }

    // Update Coll offset
    if (EC.cx < EC.coloff) {
        EC.coloff = EC.cx;
    }

    if (EC.cx >= (EC.coloff + EC.screen_cols)) {
        EC.coloff = EC.cx - EC.screen_cols + 1;
    }
}

void editorRefreshScreen() {
    editorScroll();
    struct writeBuf wBuf = WRITEBUF_INIT;

    hideCursor(&wBuf);
    cursorToHome(&wBuf);
    editorDrawRows(&wBuf);
    cursorToPosition(&wBuf);
    showCursor(&wBuf);

    write(STDOUT_FILENO, wBuf.pointer, wBuf.len);
    bufFree(&wBuf);
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** init ***/

void initEditor() {
    EC.cx = 0;
    EC.cy = 0;
    EC.rowoff = 0;
    EC.coloff = 0;
    EC.data_rows = 0;
    EC.rows_cap = 0;
    EC.row = NULL;
    if (getWindowSize(&EC.screen_rows, &EC.screen_cols) == -1) {
        die("getWindowSize");
    }
    EC.max_data_cols = EC.screen_cols;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    /***
            => Problem: Can't tell when while loop ends. is this good
    programming?
            => Problem: program Exits from different places. 3 possibilities
    Exit function for success, Exit function for failure and die function
    for error.
            => TODO: Combine all exits in 1 function -- Quit function. is
    this good programming?
    ***/
    // dLog = WRITEBUF_INIT;
    int frameNumber = 0;
    while (true) {
        frameNumber++;
        debugMsg("\n\n\n\n\n\nFRAME NUMBER : ");
        debugNumber(frameNumber);
        debugMsg("\n Cy is: ");
        debugNumber(EC.cy);
        debugMsg("\n Cx is: ");
        debugNumber(EC.cx);
        editorRefreshScreen();
        editorProcessKeyPress();
        debugMsg("\n\n\n");
    }
    return 0;
}
