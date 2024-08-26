/*** includes ***/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** defines ***/
#define TTE_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
#define WRITEBUF_INIT {NULL, 0};

//== == == == == == == == == == == == == == == == == == == == == == == ==
/*** function declaration ***/

void editorClearScreen();

//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** data ***/

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARRPW_DOWN,
    DEL_KEY
};

struct editorConfig {
    int cx;
    int cy;
    int screen_rows;
    int screen_cols;
    struct termios org_termios;
};

struct editorConfig EC;

//== == == == == == == == == == == == == == == == == == == == == == == == ==
/*** buffers ***/

struct writeBuf {
    char *pointer;
    int len;
};

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
                        return ARRPW_DOWN;
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
    switch (key) {
        case ARROW_LEFT:
            if (EC.cx > 0) EC.cx--;
            break;
        case ARROW_RIGHT:
            if (EC.cx < EC.screen_cols) EC.cx++;
            break;
        case ARRPW_DOWN:
            if (EC.cy < EC.screen_rows) EC.cy++;
            break;
        case ARROW_UP:
            if (EC.cy > 0) EC.cy--;
            break;
    }
}

void editorProcessKeyPress() {
    int key_read = editorReadKey();

    switch (key_read) {
        case CTRL_KEY('q'):
            editorClearScreen();
            exit(EXIT_SUCCESS);
            break;
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARRPW_DOWN:
        case ARROW_UP:
            editorMoveCursor(key_read);
            break;
    }
}

//== == == == == == == == == == == == == == == == == == == == == == == == ==

/*** output ***/
void clearLineRight(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[K", 3); }

void editorDrawRows(struct writeBuf *wBuf) {
    int row_num;
    for (row_num = 0; row_num < EC.screen_rows; row_num++) {
        clearLineRight(wBuf);

        if (row_num == EC.screen_rows / 2) {
            char welcome[80];
            int welcomelen =
                snprintf(welcome, sizeof(welcome),
                         "Terminal Text editor -- version %s", TTE_VERSION);
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
        } else {
            bufAppend(wBuf, "~", 1);
        }
        if (row_num < EC.screen_rows - 1) {
            bufAppend(wBuf, "\r\n", 2);
        }
    }
}

void cursorToHome(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[H", 3); }

void cursorToPosition(struct writeBuf *wBuf) {
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", EC.cy + 1, EC.cx + 1);
    bufAppend(wBuf, buf, strlen(buf));
}

void editorClearScreen() { write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7); }

void hideCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25l", 6); }

void showCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25h", 6); }

void editorRefreshScreen() {
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
    if (getWindowSize(&EC.screen_rows, &EC.screen_cols) == -1) {
        die("getWindowSize");
    }
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    /***
            => Problem: Can't tell when while loop ends. is this good
    programming?
            => Problem: program Exits from different places. 3 possibilities
    Exit function for success, Exit function for failure and die function for
    error.
            => TODO: Combine all exits in 1 function -- Quit function. is this
    good programming?
    ***/
    while (true) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }

    return 0;
}
