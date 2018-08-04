#ifndef UI_H
#define UI_H

#include <stdbool.h>

#define RTTY_FUNC_KEY		0x01E0
#define RTTY_FUNC_KEY_MASK	0x001f
#define RTTY_IS_FKEY(x)		((x & RTTY_FUNC_KEY) == RTTY_FUNC_KEY)
#define RTTY_FKEY(x)		(RTTY_FUNC_KEY | x)
#define RTTY_FKEY_VAL(x)	(x & RTTY_FUNC_KEY_MASK)

void setup_curses(void);
void update_tuning_aid(double mark, double space);
void mark_tx_extent(bool start);
int get_input(void);
void write_tx(char ch);
void write_rx(char ch);
bool check_input(void);
void printf_errno(const char *format, ...);
void show_reverse(bool rev);
void change_settings(void);
void load_config(void);
void display_charset(const char *name);
void audio_meter(int16_t envelope);
void reset_tuning_aid(void);
void clear_rx_window(void);

#endif
