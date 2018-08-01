#ifndef UI_H
#define UI_H

#include <stdbool.h>

void setup_curses(void);
void update_tuning_aid(double mark, double space);
void mark_tx_extent(bool start);
int get_input(void);
void write_tx(char ch);
void write_rx(char ch);
bool check_input(void);
void printf_errno(const char *format, ...);

#endif
