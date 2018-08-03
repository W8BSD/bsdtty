/*-
 * Copyright (c) 2018 Stephen Hurd, W8BSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "bsdtty.h"
#include "fsk_demod.h"
#include "ui.h"

static bool do_macro(int fkey, bool *figs);
static bool do_tx(void);
static void done(void);
static bool get_rts(void);
static void input_loop(void);
static void send_char(const char ch, bool *figs);
static void setup_log(void);
static void setup_tty(void);
static void usage(const char *cmd);
static void setup_defaults(void);

struct bt_settings settings = {
	.baud_numerator = 1000,
	.baud_denominator = 22,
	.dsp_rate = 48000,
	.bp_filter_q = 10,
	.lp_filter_q = 0.5,
	.mark_freq = 2125,
	.space_freq = 2295,
};

/* UART Stuff */
static int tty = -1;

/* RX in reverse mode */
static bool reverse = false;

/* Log thing */
static FILE *log_file;

static const char b2a[] = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- \x07" "87"
		  "\r$4',!:("
		  "5\")2#601"
		  "9?&\x0e" "./;\x0f";

static const char us2a[] = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- '" "87"
		  "\r$4\x07,!:("
		  "5\")2#601"
		  "9?&\x0e" "./;\x0f";

int main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "d:l:m:n:p:q:Q:r:s:t:1:2:3:4:5:6:7:8:9:0:")) != -1) {
		while (optarg && isspace(*optarg))
			optarg++;
		switch (ch) {
			case 'n':	// baud_numerator
				settings.baud_numerator = strtoi(optarg, NULL, 10);
				break;
			case 'd':	// baud_denominator
				settings.baud_denominator = strtoi(optarg, NULL, 10);
				break;
			case 'r':	// dsp_rate
				settings.dsp_rate = strtoi(optarg, NULL, 10);
				break;
			case 'l':	// log_name
				settings.log_name = strdup(optarg);
				break;
			case 'q':	// bp_filter_q
				settings.bp_filter_q = strtod(optarg, NULL);
				break;
			case 'Q':	// lp_filter_q1
				settings.lp_filter_q = strtod(optarg, NULL);
				break;
			case 'm':	// mark_freq
				settings.mark_freq = strtod(optarg, NULL);
				break;
			case 's':	// space_freq
				settings.space_freq = strtod(optarg, NULL);
				break;
			case 't':	// tty_name
				settings.tty_name = strdup(optarg);
				break;
			case 'p':	// dsp_name
				settings.dsp_name = strdup(optarg);
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				settings.macros[ch-'1'] = strdup(optarg);
				break;
			case '0':
				settings.macros[9] = strdup(optarg);
				break;
			default:
				usage(argv[0]);
		}
	}

	setup_defaults();
	load_config();

	setup_tty();

	setlocale(LC_ALL, "");
	atexit(done);

	// Now set up curses
	setup_curses();

	// Set up the FSK stuff.
	setup_rx();

	// Set up the log file
	setup_log();

	// Finally, do the thing.
	input_loop();
	return EXIT_SUCCESS;
}

static void
setup_defaults(void)
{
	/*
	 * We do this here so NULL means uninitialized and we can always
	 * free() strings.
	 */

	if (settings.log_name == NULL)
		settings.log_name = strdup("bsdtty.log");
	if (settings.tty_name == NULL)
		settings.tty_name = strdup("/dev/ttyu9");
	if (settings.dsp_name == NULL)
		settings.dsp_name = strdup("/dev/dsp8");
	if (settings.macros[1] == NULL)
		settings.macros[1] = strdup("CQ CQ CQ CQ CQ CQ DE W8BSD W8BSD W8BSD PSE K\t");
}

static void
setup_log(void)
{
	time_t now;

	if (log_file != NULL)
		fclose(log_file);
	log_file = fopen(settings.log_name, "wb");
	if (log_file == NULL)
		printf_errno("opening log file");
	now = time(NULL);
	if (log_file != NULL)
		fprintf(log_file, "\r\n\r\n%s\r\n", ctime(&now));
}

/*
 * Since opening the TTY and setting the attributes always forces DTR
 * and RTS, don't initialize these at the start since it's dangerous.
 */
static void
setup_tty(void)
{
	struct termios t;
	int state = TIOCM_DTR | TIOCM_RTS;
	struct baud_fraction bf;

	// Set up the UART
	tty = open(settings.tty_name, O_RDWR|O_DIRECT|O_NONBLOCK);
	if (tty == -1)
		printf_errno("unable to open %s");

	/*
	 * In case stty wasn't used on the init device, turn off DTR and
	 * CTS hopefully before anyone notices
	 */
	if (ioctl(tty, TIOCMBIC, &state) != 0)
		printf_errno("unable clear RTS/DTR on '%s'", settings.tty_name);

	if (tcgetattr(tty, &t) == -1)
		printf_errno("unable to read term caps");

	cfmakeraw(&t);

	/* May as well set to 45 for devices that don't support FBAUD */
	if (cfsetspeed(&t, settings.baud_numerator/settings.baud_denominator) == -1)
		printf_errno("unable to set speed to 45 baud");

	/*
	 * NOTE: With 8250 compatible UARTs, CS5 | CSTOPB is 1.5 stop
	 * bits, not 2 as documented in the man page.  This is good since
	 * it's what we want anyway.
	 */
	t.c_iflag = IGNBRK;
	t.c_oflag = 0;
	t.c_cflag = CS5 | CSTOPB | CLOCAL | CNO_RTSDTR;

	if (tcsetattr(tty, TCSADRAIN, &t) == -1)
		printf_errno("unable to set attributes");

	if (tcgetattr(tty, &t) == -1)
		printf_errno("unable to read term caps");

	if (ioctl(tty, TIOCMBIC, &state) != 0)
		printf_errno("unable clear RTS/DTR");
	bf.bf_numerator = settings.baud_numerator;
	bf.bf_denominator = settings.baud_denominator;
	ioctl(tty, TIOCSFBAUD, &bf);
	ioctl(tty, TIOCGFBAUD, &bf);
}

char
asc2baudot(int asc, bool figs)
{
	char *ch = NULL;

	asc = toupper(asc);
	if (figs)
		ch = memchr(b2a + 0x20, asc, 0x20);
	if (ch == NULL)
		ch = memchr(b2a, asc, 0x40);
	if (ch == NULL)
		ch = memchr(us2a, asc, 0x40);
	if (ch == NULL)
		return 0;
	return ch - b2a;
}

static void
input_loop(void)
{
	bool rx_mode = true;
	bool rxfigs = false;
	int rxstate = -1;
	char ch;

	while (1) {
		if (!rx_mode) {	// TX Mode
			if (!do_tx())
				return;
			rx_mode = true;
			reset_rx();
			rxstate = -1;
		}
		else {
			if (check_input()) {
				rx_mode = false;
				continue;
			}

			rxstate = get_rtty_ch(rxstate);
			if (rxstate < 0)
				continue;
			if (rxstate > 0x20)
				printf_errno("got a==%d", rxstate);
			ch = b2a[rxstate+(0x20*rxfigs)];
			if (log_file != NULL)
				fwrite(&ch, 1, 1, log_file);
			switch(ch) {
				case 0:
					continue;
				case 0x07:	// BEL
					continue;
				case 0x0f:	// LTRS
					rxfigs = false;
					continue;
				case 0x0e:	// FIGS
					rxfigs = true;
					continue;
				case ' ':
					rxfigs = false;	// USOS
			}
			write_rx(ch);
		}
	}
}

static bool
do_tx(void)
{
	int ch;
	bool figs = false;

	for (;;) {
		ch = get_input();
		switch (ch) {
			case -1:
				printf_errno("getting character");
			case 3:
				return false;
			case RTTY_FKEY(1):
				do_macro(1, &figs);
				break;
			case RTTY_FKEY(2):
				do_macro(2, &figs);
				break;
			case RTTY_FKEY(3):
				do_macro(3, &figs);
				break;
			case RTTY_FKEY(4):
				do_macro(4, &figs);
				break;
			case RTTY_FKEY(5):
				do_macro(5, &figs);
				break;
			case RTTY_FKEY(6):
				do_macro(6, &figs);
				break;
			case RTTY_FKEY(7):
				do_macro(7, &figs);
				break;
			case RTTY_FKEY(8):
				do_macro(8, &figs);
				break;
			case RTTY_FKEY(9):
				do_macro(9, &figs);
				break;
			case RTTY_FKEY(10):
				do_macro(10, &figs);
				break;
			case '`':
				toggle_reverse(&reverse);
				break;
			case 0x7f:
			case 0x08:
				if (!get_rts())
					change_settings();
				break;
			default:
				send_char(ch, &figs);
				break;
		}
		if (!get_rts()) {
			return true;
		}
	}
}

static bool
do_macro(int fkey, bool *figs)
{
	size_t len;
	size_t i;
	int m;

	m = fkey - 1;
	if (settings.macros[m] == NULL)
		return true;
	len = strlen(settings.macros[m]);
	for (i = 0; i < len; i++)
		send_char(settings.macros[m][i], figs);
	return true;
}

static void
send_char(const char ch, bool *figs)
{
	const char fstr[] = "\x1f\x1b"; // LTRS, FIGS
	char bch;
	bool rts;
	int state;

	bch = asc2baudot(ch, *figs);
	rts = get_rts();
	if (ch == '\t' || (!rts && bch != 0)) {
		rts ^= 1;
		state = TIOCM_RTS;
		if (!rts) {
			write(tty, "\x04", 1);
			ioctl(tty, TIOCDRAIN);
			// Space still gets cut off... wait one char
			usleep(((1/((double)settings.baud_numerator / settings.baud_denominator))*7.5)*1000000);
		}
		if (ioctl(tty, rts ? TIOCMBIS : TIOCMBIC, &state) != 0)
			printf_errno("%s RTS bit", rts ? "setting" : "resetting");
		if (rts) {
			*figs = false;
			write(tty, "\x08\x02", 2);
		}
		mark_tx_extent(rts);
	}
	if (bch == 0)
		return;
	if (bch) {
		// Send FIGS/LTRS as needed
		if ((!!(bch & 0x20)) != *figs) {
			*figs = !!(bch & 0x20);
			if (write(tty, &fstr[*figs], 1) != 1)
				printf_errno("error sending FIGS/LTRS");
		}
		switch (b2a[(int)bch]) {
			case 0:
			case 0x0e:
			case 0x0f:
				if (log_file != NULL)
					fwrite(&b2a[(int)bch], 1, 1, log_file);
				break;
			case '\r':
				if (write(tty, "\x02", 1) != 1)
					printf_errno("error sending linefeed");
			default:
				write_tx(b2a[(int)bch]);
				if (log_file != NULL)
					fwrite(&b2a[(int)bch], 1, 1, log_file);
				break;
		}
		if (b2a[(int)bch] == ' ')
			*figs = false;	// USOS
		bch &= 0x1f;
		if (write(tty, &bch, 1) != 1)
			printf_errno("error sending char 0x%02x", bch);
	}
}

static bool
get_rts(void)
{
	int state;

	if (ioctl(tty, TIOCMGET, &state) == -1)
		printf_errno("getting RTS state");
	return !!(state & TIOCM_RTS);
}

static void
done(void)
{
	int state = 0;

	if (tty != -1) {
		ioctl(tty, TIOCMGET, &state);
		state &= ~(TIOCM_RTS | TIOCM_DTR);
		ioctl(tty, TIOCMSET, &state);
	}
}

int
strtoi(const char *nptr, char **endptr, int base)
{
	int ret;
	long l;

	l = strtol(nptr, endptr, base);
	ret = (int)l;
	if (errno == ERANGE && (l == LONG_MIN || l == LONG_MAX)) {
		switch (l) {
			case LONG_MIN:
				ret = INT_MIN;
				break;
			case LONG_MAX:
				ret = INT_MAX;
				break;
		}
	}
	if (l < INT_MIN)
		ret = INT_MIN;
	if (l > INT_MAX)
		ret = INT_MAX;

	return ret;
}

static void
usage(const char *cmd)
{
	printf("\n"
	       "Usage: %s [<arg>...]\n"
	       "\n"
	       "ARG Description                  Default\n"
	       "-t  TTY device name              /dev/ttyu9\n"
	       "-p  DSP device name              /dev/dsp8\n"
	       "-m  Mark audio frequency         2125.0\n"
	       "-s  Space audio frequency        2295.0\n"
	       "-n  Baudrate Numerator           1000\n"
	       "-d  Baudrate Denominator         22\n"
	       "-l  Logfile name                 bsdtty.log\n"
	       "-r  DSP rate                     48000\n"
	       "-q  Bandpass filter Q            10.0\n"
	       "-Q  Envelope lowpass filter Q    0.5\n"
	       "-1  F1 Macro                     <empty>\n"
	       "-2  F2 Macro                     \"CQ CQ CQ CQ CQ CQ DE W8BSD W8BSD W8BSD PSE K\"\n"
	       "-3  F3 Macro                     <empty>\n"
	       "-4  F4 Macro                     <empty>\n"
	       "-5  F5 Macro                     <empty>\n"
	       "-6  F6 Macro                     <empty>\n"
	       "-7  F7 Macro                     <empty>\n"
	       "-8  F8 Macro                     <empty>\n"
	       "-9  F9 Macro                     <empty>\n"
	       "-0  F10 Macro                    <empty>\n"
	       "\n", cmd);
	exit(EXIT_FAILURE);
}
