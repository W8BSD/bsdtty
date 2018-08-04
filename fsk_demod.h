#ifndef FSK_DEMOD_H
#define FSK_DEMOD_H

enum afsk_bit {
	AFSK_SPACE,
	AFSK_MARK,
	AFSK_STOP,
	AFSK_UNKNOWN
};

int get_rtty_ch(int state);
void setup_rx(void);
void reset_rx(void);
void toggle_reverse(bool *rev);
void send_afsk_char(char ch);
void end_afsk_tx(void);

#endif
