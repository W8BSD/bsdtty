#ifndef BSDTTY_H
#define BSDTTY_H

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
};

extern struct bt_settings settings;

#endif
