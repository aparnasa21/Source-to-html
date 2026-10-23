# Source to HTML

A C program that reads a C source file and creates an HTML file with syntax highlighting.

## Features
- Colors for keywords, preprocessor directives, numbers, strings, characters and comments
- Optional line numbers
- Built with a state-event parser (8 states)

## How to build and run
gcc s2h.c -o s2h
./s2h test.c -o example.html -n

Options:
- -o file.html  sets the output file name
- -n            adds line numbers

Open the HTML file in a browser to see the result.