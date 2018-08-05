#ifndef BSDTTY_H
#define BSDTTY_H

#ifdef WITH_OUTRIGGER
#include "api/api.h"
#include "iniparser/src/dictionary.h"

extern dictionary *or_d;
extern struct rig *rig;
#endif

struct bt_settings {
	char	*log_name;
	char	*tty_name;
	char	*dsp_name;
	double	bp_filter_q;
	double	lp_filter_q;
	double	mark_freq;
	double	space_freq;
	int	dsp_rate;
	int	baud_denominator;
	int	baud_numerator;
	char	*macros[10];
	int	charset;
	bool	afsk;
	char	*callsign;
#ifdef WITH_OUTRIGGER
	bool	or_ptt;
	char	*or_rig;
	char	*or_dev;
#endif
};

extern struct bt_settings settings;
extern bool reverse;

char asc2baudot(int asc, bool figs);
char baudot2asc(int baudot, bool figs);
int strtoi(const char *, char **endptr, int base);
void captured_callsign(const char *str);
const char *mode_name(enum rig_modes mode);
const char *format_freq(uint64_t freq);

#endif
