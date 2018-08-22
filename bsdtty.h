#ifndef BSDTTY_H
#define BSDTTY_H

#include <inttypes.h>

struct bt_settings {
	char		*log_name;
	char		*tty_name;
	char		*dsp_name;
	double		bp_filter_q;
	double		lp_filter_q;
	double		mark_freq;
	double		space_freq;
	int		dsp_rate;
	int		baud_denominator;
	int		baud_numerator;
	char		*macros[10];
	int		charset;
	bool		afsk;
	char		*callsign;
	bool		ctl_ptt;
	bool		rigctld;
	char		*rigctld_host;
	uint16_t	rigctld_port;
	int		freq_offset;
	char		*xmlrpc_host;
	uint16_t	xmlrpc_port;
};

struct send_fsk_api {
	void (*toggle_reverse)(void);
	void (*end_tx)(void);
	void (*send_preamble)(void);
	void (*send_char)(char ch);
	void (*setup)(void);
	void (*diddle)(void);
};

extern struct bt_settings settings;
extern bool reverse;
extern char *their_callsign;
extern unsigned serial;
extern struct send_fsk_api *send_fsk;

int strtoi(const char *, char **endptr, int base);
unsigned int strtoui(const char *nptr, char **endptr, int base);
void captured_callsign(const char *str);
const char *format_freq(uint64_t freq);
void reinit(void);
void send_string(const char *str);
bool do_macro(int fkey);

#endif
