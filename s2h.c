#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define BUF_SIZE 4096

typedef enum { STATE_IDLE, STATE_WORD, STATE_PREPROCESSOR, STATE_NUMBER,
               STATE_STRING, STATE_CHAR,
               STATE_LINE_COMMENT, STATE_BLOCK_COMMENT } State;
typedef enum { EVENT_NONE, EVENT_WORD_END, EVENT_PREPROCESSOR_END,
               EVENT_NUMBER_END, EVENT_STRING_END, EVENT_CHAR_END,
               EVENT_COMMENT_END } Event;

/* shared by the helper functions */
static int show_lines    = 0;
static int line          = 1;
static int at_line_start = 1;

static const char *keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default",
    "do", "double", "else", "enum", "extern", "float", "for", "goto",
    "if", "int", "long", "register", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", NULL
};

static int is_keyword(const char *word)
{
    for (int i = 0; keywords[i] != NULL; i++)
        if (strcmp(word, keywords[i]) == 0)
            return 1;
    return 0;
}

/* print the line number if we are at the start of a line */
static void start_line_if_needed(FILE *out)
{
    if (show_lines && at_line_start)
        fprintf(out, "%4d  ", line);
    at_line_start = 0;
}

/* print one character, with HTML escaping */
static void emit_char(FILE *out, int ch)
{
    start_line_if_needed(out);

    if (ch == '<')       fputs("&lt;", out);
    else if (ch == '>')  fputs("&gt;", out);
    else if (ch == '&')  fputs("&amp;", out);
    else                 fputc(ch, out);

    if (ch == '\n') {
        line++;
        at_line_start = 1;
    }
}

/* print a finished word, colored if it is a keyword */
static void emit_word(FILE *out, const char *word)
{
    start_line_if_needed(out);

    if (is_keyword(word))
        fprintf(out, "<span class=\"keyword\">%s</span>", word);
    else
        fputs(word, out);
}

/* WORD state: add the character, or report that the word ended */
static Event word_step(int ch, char *buf, int *len)
{
    if (isalnum(ch) || ch == '_') {
        if (*len < BUF_SIZE - 1)
            buf[(*len)++] = ch;
        return EVENT_NONE;
    }
    return EVENT_WORD_END;
}

/* print a finished preprocessor line in its color */
static void emit_preprocessor(FILE *out, const char *text)
{
    start_line_if_needed(out);            /* line number goes outside the span */
    fputs("<span class=\"preprocessor\">", out);
    for (int i = 0; text[i] != '\0'; i++)
        emit_char(out, text[i]);          /* emit_char does the HTML escaping */
    fputs("</span>", out);
}

/* PREPROCESSOR state: add the character, or report that the line ended */
static Event preprocessor_step(int ch, char *buf, int *len)
{
    if (ch != '\n') {
        if (*len < BUF_SIZE - 1)
            buf[(*len)++] = ch;
        return EVENT_NONE;
    }
    return EVENT_PREPROCESSOR_END;
}

/* print a finished number in its color */
static void emit_number(FILE *out, const char *text)
{
    start_line_if_needed(out);
    fprintf(out, "<span class=\"number\">%s</span>", text);
}

/* NUMBER state: add the character, or report that the number ended */
static Event number_step(int ch, char *buf, int *len)
{
    if (isalnum(ch) || ch == '.') {
        if (*len < BUF_SIZE - 1)
            buf[(*len)++] = ch;
        return EVENT_NONE;
    }
    return EVENT_NUMBER_END;
}

/* print a finished string or character constant in its color */
static void emit_quoted(FILE *out, const char *css_class, const char *text)
{
    start_line_if_needed(out);
    fprintf(out, "<span class=\"%s\">", css_class);
    for (int i = 0; text[i] != '\0'; i++)
        emit_char(out, text[i]);          /* HTML escaping */
    fputs("</span>", out);
}

/* STRING and CHAR states: store the character, detect the closing quote */
static Event quote_step(int ch, char *buf, int *len, int quote,
                        int *escaped, Event end_event)
{
    if (*len < BUF_SIZE - 1)
        buf[(*len)++] = ch;

    if (*escaped) {              /* this char follows a backslash: skip it */
        *escaped = 0;
        return EVENT_NONE;
    }
    if (ch == '\\') {            /* next char is escaped */
        *escaped = 1;
        return EVENT_NONE;
    }
    if (ch == quote)             /* closing quote found */
        return end_event;

    return EVENT_NONE;
}

/* print a finished comment; a multi-line comment is split per line so
   that line numbers stay outside the colored span */
static void emit_comment(FILE *out, const char *text)
{
    int in_span = 0;

    for (int i = 0; text[i] != '\0'; i++) {
        if (!in_span) {
            start_line_if_needed(out);
            fputs("<span class=\"comment\">", out);
            in_span = 1;
        }
        if (text[i] == '\n') {
            fputs("</span>", out);
            in_span = 0;
            emit_char(out, '\n');
        } else {
            emit_char(out, text[i]);
        }
    }
    if (in_span)
        fputs("</span>", out);
}

/* LINE_COMMENT state: ends at end of line */
static Event line_comment_step(int ch, char *buf, int *len)
{
    if (ch != '\n') {
        if (*len < BUF_SIZE - 1)
            buf[(*len)++] = ch;
        return EVENT_NONE;
    }
    return EVENT_COMMENT_END;
}

/* BLOCK_COMMENT state: ends when '*' is followed by '/' */
static Event block_comment_step(int ch, char *buf, int *len, int *star_seen)
{
    if (*len < BUF_SIZE - 1)
        buf[(*len)++] = ch;

    if (*star_seen && ch == '/')
        return EVENT_COMMENT_END;

    *star_seen = (ch == '*');
    return EVENT_NONE;
}

int main(int argc, char *argv[])
{
    char *input_name  = NULL;
    char *output_name = "output.html";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_name = argv[i + 1];
                i++;
            } else {
                printf("-o needs a file name\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-n") == 0) {
            show_lines = 1;
        } else {
            input_name = argv[i];
        }
    }

    if (input_name == NULL) {
        printf("Usage: %s input.c [-o output.html] [-n]\n", argv[0]);
        return 1;
    }

    FILE *in  = fopen(input_name, "r");
    FILE *out = fopen(output_name, "w");

    if (in == NULL || out == NULL) {
        printf("Error opening file\n");
        return 1;
    }

    fprintf(out, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(out, "<title>Source Code</title>\n");
    fprintf(out, "<style>\n");
    fprintf(out, "body { background: #1e1e1e; color: #d4d4d4; }\n");
    fprintf(out, "pre { font-family: monospace; }\n");
    fprintf(out, ".keyword { color: #569cd6; }\n");
    fprintf(out, ".preprocessor { color: #c586c0; }\n");
    fprintf(out, ".number { color: #b5cea8; }\n");
    fprintf(out, ".string { color: #ce9178; }\n");
    fprintf(out, ".char { color: #d7ba7d; }\n");
    fprintf(out, ".comment { color: #6a9955; }\n");
    fprintf(out, "</style>\n</head>\n<body>\n<pre>\n");

    State state = STATE_IDLE;
    char  buf[BUF_SIZE];
    int   len = 0;
    int   escaped = 0;
    int   star_seen = 0;
    int   ch;

        while ((ch = fgetc(in)) != EOF) {
        switch (state) {

        case STATE_IDLE:
            if (isalpha(ch) || ch == '_') {
                state = STATE_WORD;           /* a word starts */
                buf[0] = ch;
                len = 1;
            }
             else if (ch == '#') {
                state = STATE_PREPROCESSOR;   /* a directive starts */
                buf[0] = ch;
                len = 1;
            } else if (isdigit(ch)) {
                state = STATE_NUMBER;         /* a number starts */
                buf[0] = ch;
                len = 1;
            }else if (ch == '"') {
                state = STATE_STRING;         /* a string starts */
                buf[0] = ch;
                len = 1;
                escaped = 0;
            } else if (ch == '\'') {
                state = STATE_CHAR;           /* a character constant starts */
                buf[0] = ch;
                len = 1;
                escaped = 0;
            } else if (ch == '/') {
                int next = fgetc(in);             /* look one character ahead */
                if (next == '/') {
                    state = STATE_LINE_COMMENT;
                    buf[0] = '/'; buf[1] = '/';
                    len = 2;
                } else if (next == '*') {
                    state = STATE_BLOCK_COMMENT;
                    buf[0] = '/'; buf[1] = '*';
                    len = 2;
                    star_seen = 0;
                } else {                          /* just a division sign */
                    emit_char(out, '/');
                    if (next != EOF)
                        ungetc(next, in);
                }
            }else {
                emit_char(out, ch);
            }
            
            break;

        case STATE_WORD:
            if (word_step(ch, buf, &len) == EVENT_WORD_END) {
                buf[len] = '\0';
                emit_word(out, buf);
                len = 0;
                state = STATE_IDLE;
                ungetc(ch, in);               /* handle this char again in IDLE */
            }
            break;

        case STATE_PREPROCESSOR:
            if (preprocessor_step(ch, buf, &len) == EVENT_PREPROCESSOR_END) {
                buf[len] = '\0';
                emit_preprocessor(out, buf);
                len = 0;
                state = STATE_IDLE;
                ungetc(ch, in);               /* let IDLE print the newline */
            }
            break;

        case STATE_NUMBER:
            if (number_step(ch, buf, &len) == EVENT_NUMBER_END) {
                buf[len] = '\0';
                emit_number(out, buf);
                len = 0;
                state = STATE_IDLE;
                ungetc(ch, in);               /* handle this char again in IDLE */
            }
            break;

            case STATE_STRING:
            if (quote_step(ch, buf, &len, '"', &escaped, EVENT_STRING_END)
                    == EVENT_STRING_END) {
                buf[len] = '\0';
                emit_quoted(out, "string", buf);
                len = 0;
                state = STATE_IDLE;           /* no ungetc: quote belongs to the token */
            }
            break;

        case STATE_CHAR:
            if (quote_step(ch, buf, &len, '\'', &escaped, EVENT_CHAR_END)
                    == EVENT_CHAR_END) {
                buf[len] = '\0';
                emit_quoted(out, "char", buf);
                len = 0;
                state = STATE_IDLE;
            }
            break;

                    case STATE_LINE_COMMENT:
            if (line_comment_step(ch, buf, &len) == EVENT_COMMENT_END) {
                buf[len] = '\0';
                emit_comment(out, buf);
                len = 0;
                state = STATE_IDLE;
                ungetc(ch, in);                   /* let IDLE print the newline */
            }
            break;

        case STATE_BLOCK_COMMENT:
            if (block_comment_step(ch, buf, &len, &star_seen) == EVENT_COMMENT_END) {
                buf[len] = '\0';
                emit_comment(out, buf);
                len = 0;
                state = STATE_IDLE;               /* no ungetc: '/' is part of the comment */
            }
            break;
        }

    }
    /* the file may end while a word is still being read */
            if (state == STATE_WORD) {
        buf[len] = '\0';
        emit_word(out, buf);
    } else if (state == STATE_PREPROCESSOR) {
        buf[len] = '\0';
        emit_preprocessor(out, buf);
    } else if (state == STATE_NUMBER) {
        buf[len] = '\0';
        emit_number(out, buf);
    } else if (state == STATE_STRING) {
        buf[len] = '\0';
        emit_quoted(out, "string", buf);
    } else if (state == STATE_CHAR) {
        buf[len] = '\0';
        emit_quoted(out, "char", buf);
    } else if (state == STATE_LINE_COMMENT || state == STATE_BLOCK_COMMENT) {
        buf[len] = '\0';
        emit_comment(out, buf);
    }

    fprintf(out, "</pre>\n</body>\n</html>\n");

    fclose(in);
    fclose(out);
    return 0;
}