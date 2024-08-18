/*** includes ***/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

void editorClearScreen();

/*** data ***/

struct editorConfig {
    int screen_rows;
    int screen_cols;
    struct termios org_termios;
};

struct editorConfig EC;

/*** buffers ***/

struct writeBuf {
    char *pointer;
    int len;
};

#define WRITEBUF_INIT {NULL, 0};

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

char editorReadKey() {
    char char_read;
    int bytes_read = read(STDIN_FILENO, &char_read, 1);
    while (bytes_read != 1) {
        if (bytes_read == -1) {
            die("read");  // Program Ends, Error Handling Section
        }
        bytes_read = read(STDIN_FILENO, &char_read,
                          1);  // Read Again because nothing read
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
/*** input ***/

void editorProcessKeyPress() {
    char key_read = editorReadKey();

    switch (key_read) {
        case CTRL_KEY('q'):
            editorClearScreen();
            exit(EXIT_SUCCESS);
            break;
    }
}

/*** output ***/

void editorDrawRows(struct writeBuf *wBuf) {
    int row_num;
    for (row_num = 0; row_num < EC.screen_rows; row_num++) {
        bufAppend(wBuf, "~", 1);
        if (row_num < EC.screen_rows - 1) {
            bufAppend(wBuf, "\r\n", 2);
        }
    }
}

void editorClearBufScreen(struct writeBuf *wBuf) {
    bufAppend(wBuf, "\x1b[2J", 4);
    bufAppend(wBuf, "\x1b[H", 3);
}

void editorClearScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void hideCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25l", 6); }

void showCursor(struct writeBuf *wBuf) { bufAppend(wBuf, "\x1b[?25h", 6); }

void editorRefreshScreen() {
    struct writeBuf wBuf = WRITEBUF_INIT;

    hideCursor(&wBuf);
    editorClearBufScreen(&wBuf);
    editorDrawRows(&wBuf);
    bufAppend(&wBuf, "\x1b[H", 3);
    showCursor(&wBuf);

    write(STDOUT_FILENO, wBuf.pointer, wBuf.len);
    bufFree(&wBuf);
}

/*** init ***/

void initEditor() {
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
