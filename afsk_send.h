#ifndef AFSK_SEND_H
#define AFSK_SEND_H

enum afsk_bit {
	AFSK_SPACE,
	AFSK_MARK,
	AFSK_STOP,
	AFSK_UNKNOWN
};

void afsk_toggle_reverse(void);
void end_afsk_tx(void);
void send_afsk_bit(enum afsk_bit bit);
void send_afsk_char(char ch);
void setup_afsk_audio(void);

#endif
