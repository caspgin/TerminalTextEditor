/*** includes ***/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

void editorClearScreen();

/*** data ***/

struct termios org_termios;

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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &org_termios) == -1) {
        die("tcsetattr");
    }
}

// enable raw mode by turning the ECHO OFF and saving the default values of
// terminal in org_termios
void enableRawMode() {
    // get the default value
    if (tcgetattr(STDIN_FILENO, &org_termios) == -1) {
        die("tcgetattr");
    }
    // register the disabel function so it runs when program ends
    atexit(disableRawMode);

    struct termios raw = org_termios;
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
    char charRead;
    int bytesRead = read(STDIN_FILENO, &charRead, 1);
    while (bytesRead != 1) {
        if (bytesRead == -1) {
            die("read");  // Program Ends, Error Handling Section
        }
        bytesRead = read(STDIN_FILENO, &charRead,
                         1);  // Read Again because nothing read
    }

    return charRead;
}

/*** input ***/

void editorProcessKeyPress() {
    char keyRead = editorReadKey();

    switch (keyRead) {
        case CTRL_KEY('q'):
            editorClearScreen();
            exit(EXIT_SUCCESS);
            break;
    }
}

/*** output ***/

void editorClearScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** init ***/

int main(int argc, char *argv[]) {
    enableRawMode();
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
        editorClearScreen();
        editorProcessKeyPress();
    }

    return 0;
}
