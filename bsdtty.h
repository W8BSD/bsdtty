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

extern struct bt_settings settings;
extern bool reverse;
extern char *their_callsign;
extern unsigned serial;

int strtoi(const char *, char **endptr, int base);
void captured_callsign(const char *str);
const char *format_freq(uint64_t freq);
void reinit(void);
uint64_t get_rig_freq(void);
const char *get_rig_mode(char *buf, size_t sz);
void send_string(const char *str);
bool get_rig_ptt(void);
bool set_rig_ptt(bool val);
bool do_macro(int fkey);
void get_rig_freq_mode(uint64_t *freq, char *buf, size_t sz);

#endif
