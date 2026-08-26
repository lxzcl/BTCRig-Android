#ifndef BTC_MINER_CONSOLE_H
#define BTC_MINER_CONSOLE_H

typedef enum {
    CONSOLE_RESET = 0,
    CONSOLE_BOLD,
    CONSOLE_DIM,
    CONSOLE_RED,
    CONSOLE_GREEN,
    CONSOLE_YELLOW,
    CONSOLE_BLUE,
    CONSOLE_MAGENTA,
    CONSOLE_CYAN,
    CONSOLE_WHITE,
    CONSOLE_GRAY,
    CONSOLE_BRIGHT_RED,
    CONSOLE_BRIGHT_GREEN,
    CONSOLE_BRIGHT_YELLOW,
    CONSOLE_BRIGHT_CYAN,
} console_color_t;

void console_init(void);
int console_color_enabled(void);
const char *console_color(console_color_t color);

#define C_RESET console_color(CONSOLE_RESET)
#define C_BOLD console_color(CONSOLE_BOLD)
#define C_DIM console_color(CONSOLE_DIM)
#define C_RED console_color(CONSOLE_RED)
#define C_GREEN console_color(CONSOLE_GREEN)
#define C_YELLOW console_color(CONSOLE_YELLOW)
#define C_BLUE console_color(CONSOLE_BLUE)
#define C_MAGENTA console_color(CONSOLE_MAGENTA)
#define C_CYAN console_color(CONSOLE_CYAN)
#define C_WHITE console_color(CONSOLE_WHITE)
#define C_GRAY console_color(CONSOLE_GRAY)
#define C_BRIGHT_RED console_color(CONSOLE_BRIGHT_RED)
#define C_BRIGHT_GREEN console_color(CONSOLE_BRIGHT_GREEN)
#define C_BRIGHT_YELLOW console_color(CONSOLE_BRIGHT_YELLOW)
#define C_BRIGHT_CYAN console_color(CONSOLE_BRIGHT_CYAN)

#endif
