#ifndef BSDTTY_H
#define BSDTTY_H

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
#ifdef WITH_OUTRIGGER
	char		*or_rig;
	char		*or_dev;
#endif
	bool		rigctld;
	char		*rigctld_host;
	uint16_t	rigctld_port;
	int		freq_offset;
};

extern struct bt_settings settings;
extern bool reverse;

char asc2baudot(int asc, bool figs);
char baudot2asc(int baudot, bool figs);
int strtoi(const char *, char **endptr, int base);
void captured_callsign(const char *str);
const char *format_freq(uint64_t freq);
void reinit(void);
void fix_config(void);
uint64_t get_rig_freq(void);
const char *get_rig_mode(char *buf, size_t sz);

#endif
