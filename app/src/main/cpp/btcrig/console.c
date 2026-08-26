#define _POSIX_C_SOURCE 200809L

#include "console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define BTC_ISATTY(fd) _isatty(fd)
#define BTC_STDOUT_FILENO _fileno(stdout)
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#define BTC_ISATTY(fd) isatty(fd)
#define BTC_STDOUT_FILENO STDOUT_FILENO
#endif

static int g_console_initialized = 0;
static int g_console_color_enabled = 0;

static int text_is_true(const char *value) {
    return value != NULL &&
           (strcmp(value, "1") == 0 ||
            strcmp(value, "true") == 0 ||
            strcmp(value, "yes") == 0 ||
            strcmp(value, "on") == 0);
}

static int text_is_false(const char *value) {
    return value != NULL &&
           (strcmp(value, "0") == 0 ||
            strcmp(value, "false") == 0 ||
            strcmp(value, "no") == 0 ||
            strcmp(value, "off") == 0);
}

static int terminal_supports_color(void) {
    if (!BTC_ISATTY(BTC_STDOUT_FILENO)) {
        return 0;
    }

#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (handle != NULL && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
            return 1;
        }
        if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            return 1;
        }
        return 0;
    }

    const char *term = getenv("TERM");
    return term != NULL && term[0] != '\0' && strcmp(term, "dumb") != 0;
#else
    return 1;
#endif
}

void console_init(void) {
    if (g_console_initialized) {
        return;
    }

    g_console_initialized = 1;

    const char *force = getenv("BTC_MINER_COLOR");
    if (text_is_true(force)) {
        g_console_color_enabled = 1;
        return;
    }
    if (text_is_false(force) || getenv("NO_COLOR") != NULL) {
        g_console_color_enabled = 0;
        return;
    }

    g_console_color_enabled = terminal_supports_color();
}

int console_color_enabled(void) {
    console_init();
    return g_console_color_enabled;
}

const char *console_color(console_color_t color) {
    console_init();
    if (!g_console_color_enabled) {
        return "";
    }

    switch (color) {
    case CONSOLE_RESET:
        return "\033[0m";
    case CONSOLE_BOLD:
        return "\033[1m";
    case CONSOLE_DIM:
        return "\033[2m";
    case CONSOLE_RED:
        return "\033[31m";
    case CONSOLE_GREEN:
        return "\033[32m";
    case CONSOLE_YELLOW:
        return "\033[93m";
    case CONSOLE_BLUE:
        return "\033[34m";
    case CONSOLE_MAGENTA:
        return "\033[35m";
    case CONSOLE_CYAN:
        return "\033[36m";
    case CONSOLE_WHITE:
        return "\033[37m";
    case CONSOLE_GRAY:
        return "\033[90m";
    case CONSOLE_BRIGHT_RED:
        return "\033[91m";
    case CONSOLE_BRIGHT_GREEN:
        return "\033[92m";
    case CONSOLE_BRIGHT_YELLOW:
        return "\033[93m";
    case CONSOLE_BRIGHT_CYAN:
        return "\033[96m";
    }

    return "";
}
