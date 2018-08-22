#ifndef AFSK_SEND_H
#define AFSK_SEND_H

void afsk_toggle_reverse(void);
void end_afsk_tx(void);
void send_afsk_preamble(void);
void send_afsk_char(char ch);
void setup_afsk(int tty);
void diddle_afsk(void);

#endif
