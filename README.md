# Terminal Text Editor

A terminal-based text editor for Linux, made by following the kilo editor and extending with additional functionality.

## Purpose
- To learn how to make a text editor.
- To learn software development with a project.

## Features

- **Basic Text Editing**: Create, view, and edit text files directly in the terminal.
- **File Management**:
  - Open existing files
  - Create new files
  - Save files with overwrite confirmation
- **Search Functionality**: Incremental word search within documents.

## Installation

1. Clone this repository:
2. Use the Make command
3. Compiles and creates a tte executable

## Usage

To open a file:
1. Run the executable file and give the file name to be opened.

To create a new file:
1. Run the executable file
2. `Ctrl-S`: Save the file. It will prompt for a name.
3. Don't forget to pass the file type at the end.


## Keyboard Shortcuts

[List any keyboard shortcuts your editor uses, e.g.:]

- `Ctrl-S`: Save file
- `Ctrl-Q`: Quit
- `Ctrl-F`: Find in the file

## Acknowledgements
- Based on the [kilo editor tutorial](https://viewsourcecode.org/snaptoken/kilo/) by snaptoken.

## Tools used
- Language -> C
- Neovim
- WSL -> Ubuntu distro
- <termios.h> library
- GCC compiler


## Further Development

Features I would like to add and learn.

1. Support for more filetypes
2. Wrapping
3. Syntax Highlighting
4. Multiple Buffers

## Made a windows version https://github.com/caspgin/TerminalTextEditorWindows.git
