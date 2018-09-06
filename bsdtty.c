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

#include <sys/ioctl.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "afsk_send.h"
#include "fsk_send.h"
#include "baudot.h"
#include "bsdtty.h"
#include "fldigi_xmlrpc.h"
#include "fsk_demod.h"
#include "rigctl.h"
#include "ui.h"

static bool do_tx(int *rxstate);
static void done(void);
static void handle_rx_char(char ch);
static void input_loop(void);
static void send_char(const char ch);
static void send_rtty_char(char ch);
static void set_rts(bool newval, bool force);
static void setup_log(void);
noreturn static void usage(const char *cmd);
static void setup_defaults(void);
static void fix_config(void);
struct send_fsk_api *send_fsk;

struct bt_settings settings = {
	.baud_numerator = 1000,
	.baud_denominator = 22,
	.dsp_rate = 8000,
	.bp_filter_q = 10,
	.lp_filter_q = 0.5,
	.mark_freq = 2125,
	.space_freq = 2295,
	.charset = 0,
	.afsk = false,
	.ctl_ptt = false,
	.freq_offset = 170,
	.rigctld_port = 4532,
	.xmlrpc_port = 7362
};

/* RX in reverse mode */
bool reverse = false;

/* Log thing */
static FILE *log_file;

static bool txfigs;
static bool send_start_crlf = true;
static bool send_end_space = true;
static pthread_t xmlrpc_thread;
static pthread_t rx_thread;
bool rts;
// The mutex is to allow downgrading.
pthread_mutex_t rts_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t rts_rwlock = PTHREAD_RWLOCK_INITIALIZER;

char *their_callsign;
unsigned serial;
pthread_rwlock_t settings_lock = PTHREAD_RWLOCK_INITIALIZER;
pthread_mutex_t bsdtty_lock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv)
{
	char *c;
	int ch;

	load_config();

	SETTING_WLOCK();
	while ((ch = getopt(argc, argv, "ac:C:d:f:hl:i:I:m:n:N:p:P:q:Q:r:s:t:T1:x:2:3:4:5:6:7:8:9:0:")) != -1) {
		while (optarg && isspace(*optarg))
			optarg++;
		switch (ch) {
			case '0':
				settings.macros[9] = strdup(optarg);
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
			case 'a':
				settings.afsk = true;
				break;
			case 'c':
				settings.charset = strtoi(optarg, NULL, 10);
				if (settings.charset < 0 || settings.charset >= charset_count)
					settings.charset = 0;
				break;
			case 'C':
				settings.callsign = strdup(optarg);
				break;
			case 'd':	// baud_denominator
				settings.baud_denominator = strtoi(optarg, NULL, 10);
				break;
			case 'f':
				settings.freq_offset = strtoi(optarg, NULL, 10);
				break;
			case 'F':
				settings.freq_offset = strtoi(optarg, NULL, 10);
				break;
			case 'h':
				SETTING_UNLOCK();
				usage(argv[0]);
			case 'i':
				settings.rigctld_host = strdup(optarg);
				break;
			case 'I':
				settings.rigctld_port = strtoi(optarg, NULL, 10);
				break;
			case 'l':	// log_name
				settings.log_name = strdup(optarg);
				break;
			case 'm':	// mark_freq
				settings.mark_freq = strtod(optarg, NULL);
				break;
			case 'n':	// baud_numerator
				settings.baud_numerator = strtoi(optarg, NULL, 10);
				break;
			case 'N':
				for (c = optarg; *c; c++) {
					switch (*c) {
						case 's':
							send_start_crlf = false;
							break;
						case 'e':
							send_end_space = false;
							break;
					}
				}
				break;
			case 'p':	// dsp_name
				settings.dsp_name = strdup(optarg);
				break;
			case 'P':
				settings.xmlrpc_port = strtoi(optarg, NULL, 10);
				break;
			case 'q':	// bp_filter_q
				settings.bp_filter_q = strtod(optarg, NULL);
				break;
			case 'Q':	// lp_filter_q1
				settings.lp_filter_q = strtod(optarg, NULL);
				break;
			case 'r':	// dsp_rate
				settings.dsp_rate = strtoi(optarg, NULL, 10);
				break;
			case 's':	// space_freq
				settings.space_freq = strtod(optarg, NULL);
				break;
			case 't':	// tty_name
				settings.tty_name = strdup(optarg);
				break;
			case 'T':
				settings.ctl_ptt = true;
				break;
			case 'x':
				settings.xmlrpc_host = strdup(optarg);
				break;
			default:
				SETTING_UNLOCK();
				usage(argv[0]);
		}
	}

	setup_defaults();
	fix_config();
	SETTING_UNLOCK();

	setlocale(LC_ALL, "");

	/*
	 * We want to set up the sockets before we setup curses since
	 * it can spin writing to stderr.
	 */
	setup_xmlrpc(&xmlrpc_thread);

	// Now set up curses
	setup_curses();

	// Do our cleanup handler here so the curses one is last.
	atexit(done);

	// Set up the RX stuff.
	setup_rx(&rx_thread);

	// And TX
	if (settings.afsk)
		send_fsk = &afsk_api;
	else
		send_fsk = &fsk_api;
	send_fsk->setup();

	// Set up the log file
	setup_log();

	display_charset(charset_name());
	BSDTTY_LOCK();
	update_serial(serial);
	BSDTTY_UNLOCK();

	setup_rig_control();

	// Finally, do the thing.
	input_loop();
	return EXIT_SUCCESS;
}

void
reinit(void)
{
	load_config();
	fix_config();

	// Set up the FSK stuff.
	send_fsk->end_fsk();
	pthread_cancel(rx_thread);
	pthread_join(rx_thread, NULL);
	setup_rx(&rx_thread);
	SETTING_RLOCK();
	if (settings.afsk)
		send_fsk = &afsk_api;
	else
		send_fsk = &fsk_api;
	SETTING_UNLOCK();
	send_fsk->setup();

	// Set up the log file
	setup_log();

	setup_rig_control();
}

/*
 * setting write lock must be held.
 */
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
		settings.macros[1] = strdup("CQ CQ CQ CQ CQ CQ DE \\ \\ \\ PSE K\t");
	if (settings.macros[2] == NULL)
		settings.macros[2] = strdup("\\ ");
	if (settings.macros[3] == NULL)
		settings.macros[3] = strdup("` DE \\\t");
	if (settings.callsign == NULL)
		settings.callsign = strdup("W8BSD");
	if (settings.rigctld_host == NULL)
		settings.rigctld_host = strdup("localhost");
}

static void
setup_log(void)
{
	time_t now;

	if (log_file)
		fclose(log_file);
	SETTING_RLOCK();
	log_file = fopen(settings.log_name, "a");
	SETTING_UNLOCK();
	if (log_file == NULL)
		printf_errno("opening log file");
	now = time(NULL);
	if (log_file != NULL)
		fprintf(log_file, "\n\n%s\n", ctime(&now));
}

static void
handle_rx_char(char ch)
{
	switch(ch) {
		case 0:
			return;
		case 0x07:	// BEL
			return;
		case 0x0f:	// LTRS
			return;
		case 0x0e:	// FIGS
			return;
	}
	if (log_file != NULL)
		fwrite(&ch, 1, 1, log_file);
	fldigi_add_rx(ch);
}

static void
input_loop(void)
{
	int rxstate = -1;

	while (1) {
		RTS_RLOCK();
		if (rts) {	// TX Mode
			RTS_UNLOCK();
			if (!do_tx(&rxstate))
				return;
			rxstate = -1;
		}
		else {
			RTS_UNLOCK();
			if (check_input()) {
				if (!do_tx(&rxstate))
					return;
				continue;
			}

			for (rxstate = get_rtty_ch(); is_fsk_char(rxstate); rxstate = get_rtty_ch())
				handle_rx_char(rxstate);
		}
	}
}

static bool
do_tx(int *rxstate)
{
	int ch;

	ch = get_input();
	switch (ch) {
		case -1:
			break;
		case 3:
			return false;
		case 4:
			RTS_WLOCK();
			if (rts) {
				set_rts(false, true);
				RTS_UNLOCK();
			}
			else
				RTS_UNLOCK();
			break;
		case RTTY_FKEY(1):
			do_macro(1);
			break;
		case RTTY_FKEY(2):
			do_macro(2);
			break;
		case RTTY_FKEY(3):
			do_macro(3);
			break;
		case RTTY_FKEY(4):
			do_macro(4);
			break;
		case RTTY_FKEY(5):
			do_macro(5);
			break;
		case RTTY_FKEY(6):
			do_macro(6);
			break;
		case RTTY_FKEY(7):
			do_macro(7);
			break;
		case RTTY_FKEY(8):
			do_macro(8);
			break;
		case RTTY_FKEY(9):
			do_macro(9);
			break;
		case RTTY_FKEY(10):
			do_macro(10);
			break;
		case RTTY_KEY_UP:
			BSDTTY_LOCK();
			serial++;
			update_serial(serial);
			BSDTTY_UNLOCK();
			break;
		case RTTY_KEY_DOWN:
			BSDTTY_LOCK();
			if (serial)
				serial--;
			update_serial(serial);
			BSDTTY_UNLOCK();
			break;
		case RTTY_KEY_REFRESH:
			display_charset(charset_name());
			BSDTTY_LOCK();
			show_reverse(reverse);
			update_captured_call(their_callsign);
			update_serial(serial);
			BSDTTY_UNLOCK();
			break;
		case '`':
			BSDTTY_LOCK();
			toggle_reverse(&reverse);
			send_fsk->toggle_reverse();
			BSDTTY_UNLOCK();
			*rxstate = -1;
			break;
		case '[':
			SETTING_WLOCK();
			settings.charset--;
			if (settings.charset < 0)
				settings.charset = charset_count - 1;
			SETTING_UNLOCK();
			display_charset(charset_name());
			break;
		case ']':
			SETTING_WLOCK();
			settings.charset++;
			if (settings.charset == charset_count)
				settings.charset = 0;
			SETTING_UNLOCK();
			display_charset(charset_name());
			break;
		case '\\':
			reset_tuning_aid();
			break;
		case 23:
			RTS_RLOCK();
			if (!rts) {
				RTS_UNLOCK();
				toggle_tuning_aid();
			}
			else
				RTS_UNLOCK();
			break;
		case 0x7f:
		case 0x08:
			RTS_RLOCK();
			if (!rts) {
				RTS_UNLOCK();
				change_settings();
				*rxstate = -1;
			}
			else
				RTS_UNLOCK();
			break;
		default:
			send_char(ch);
			break;
	}
	return true;
}

void
send_string(char *str)
{
	if (str == NULL)
		return;

	for (; *str; str++)
		send_char(*str);
	free(str);
}

bool
do_macro(int fkey)
{
	size_t len;
	size_t i;
	int m;
	char *str;
	bool clear = false;
	bool upd_ser = false;

	m = fkey - 1;
	SETTING_RLOCK();
	if (settings.macros[m] == NULL) {
		SETTING_UNLOCK();
		return true;
	}
	if (strncasecmp(settings.macros[m], "CQ CQ", 5) == 0)
		clear = true;
	len = strlen(settings.macros[m]);
	for (i = 0; i < len; i++) {
		switch (settings.macros[m][i]) {
			case '\\':
				send_string(strdup(settings.callsign));
				break;
			case '`':
				BSDTTY_LOCK();
				str = strdup(their_callsign);
				BSDTTY_UNLOCK();
				send_string(str);
				break;
			case '[':
				send_char('\r');
				break;
			case ']':
				// TODO: CR is expanded to CRLF...
				send_char('\n');
				break;
			case '^':
				BSDTTY_LOCK();
				serial++;
				BSDTTY_UNLOCK();
				upd_ser = true;
				/* Fall-through */
			case '%':
				BSDTTY_LOCK();
				asprintf(&str, "%d", serial);
				BSDTTY_UNLOCK();
				send_string(str);
				break;
			default:
				send_char(settings.macros[m][i]);
				break;
		}
	}
	if (len > 2)
		if (strcasecmp(settings.macros[m] + len - 3, " CQ") == 0)
			clear = true;
	SETTING_UNLOCK();
	if (clear)
		clear_rx_window();
	if (upd_ser) {
		BSDTTY_LOCK();
		update_serial(serial);
		BSDTTY_UNLOCK();
	}
	return true;
}

/*
 * Must be called with RTS_WLOCK().
 */
static void
set_rts(bool newval, bool force)
{
	time_t now;
	static uint64_t freq = 0;
	static char mode[16];

	rts = newval;
	if (!rts) {
		if (force)
			send_fsk->flush();
		if (send_end_space && !force)
			send_rtty_char(4);
		send_fsk->end_tx();
		RX_UNLOCK();
	}
	if (rts) {
		get_rig_freq_mode(&freq, mode, sizeof(mode));
		if (freq) {
			SETTING_RLOCK();
			freq += settings.freq_offset;
			SETTING_UNLOCK();
		}
	}
	now = time(NULL);
	if (log_file != NULL) {
		if (freq == 0)
			fprintf(log_file, "\n------- %s of transmission (%.24s) -------\n", rts ? "Start" : "End", ctime(&now));
		else
			fprintf(log_file, "\n------- %s of transmission (%.24s) on %s (%s) -------\n", rts ? "Start" : "End", ctime(&now), format_freq(freq), mode);
		fflush(log_file);
	}
	if (rts)
		RX_LOCK();
	mark_tx_extent(rts);
	set_rig_ptt(rts);
	if (rts) {
		// Stop the RX thread...
		/* 
		 * Start with a byte length of mark to help sync...
		 * This also covers the RX -> TX switching time
		 * due to the relay.
		 */
		if (!force)
			send_fsk->send_preamble();
		txfigs = false;
		/*
		 * Per ITU-T S.1, the FIRST symbol should be a
		 * shift... since it's most likely to be lost,
		 * a LTRS is the safest, since a FIGS will get
		 * repeated after the CRLF.
		 */
		if (!force) {
			send_rtty_char(0x1f);
			if (send_start_crlf) {
				send_rtty_char(8);
				send_rtty_char(2);
			}
		}
	}
}

static void
send_char(const char ch)
{
	const char fstr[] = "\x1f\x1b"; // LTRS, FIGS
	char bch;
	char ach;

	bch = asc2baudot(ch, txfigs);

	RTS_WLOCK();
	if (ch == '\t' || (!rts && bch != 0)) {
		rts = !rts;
		set_rts(rts, false);
	}
	RTS_UNLOCK();
	if (bch == 0)
		return;
	if (bch) {
		// Send FIGS/LTRS as needed
		if ((!!(bch & 0x20)) != txfigs) {
			txfigs = !!(bch & 0x20);
			send_rtty_char(fstr[txfigs]);
		}
		/* We do this to ensure it's valid baudot */
		ach = baudot2asc(bch & 0x1f, bch & 0x20);
		switch (ach) {
			case 0:
				break;
			case 0x0e:
				txfigs = false;
				break;
			case 0x0f:
				txfigs = true;
				break;
			case '\r':
				write_tx('\r');
				if (log_file != NULL)
					fwrite("\r\n", 2, 1, log_file);
				break;
			default:
				write_tx(ach);
				if (log_file != NULL)
					fwrite(&ach, 1, 1, log_file);
				break;
		}
		if (ach == ' ')
			txfigs = false;	// USOS
		bch &= 0x1f;
		send_rtty_char(bch);
		if (bch == 0x08)
			send_rtty_char(2);
	}
}

static void
done(void)
{
	/*
	 * We need to clear the RX lock if we set it, but we don't
	 * want assertions here, so don't use the macros.
	 */
	pthread_mutex_lock(&rts_lock);
	pthread_rwlock_wrlock(&rts_rwlock);
	pthread_mutex_unlock(&rts_lock);
	if (rts) {
		rts = false;
		set_rig_ptt(false);
		pthread_rwlock_unlock(&rts_rwlock);
		pthread_mutex_unlock(&rx_lock);
	}
	else
		pthread_rwlock_unlock(&rts_rwlock);
	if (send_fsk)
		send_fsk->end_fsk();
	pthread_cancel(xmlrpc_thread);
	pthread_join(xmlrpc_thread, NULL);
	pthread_cancel(rx_thread);
	pthread_join(rx_thread, NULL);
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
	if (l < INT_MIN) {
		ret = INT_MIN;
		errno = ERANGE;
	}
	if (l > INT_MAX) {
		ret = INT_MAX;
		errno = ERANGE;
	}

	return ret;
}

unsigned int
strtoui(const char *nptr, char **endptr, int base)
{
	unsigned int ret;
	unsigned long l;

	l = strtoul(nptr, endptr, base);
	ret = (unsigned int)l;
	if (errno == ERANGE && l == ULONG_MAX)
		ret = UINT_MAX;
	if (l > UINT_MAX) {
		ret = UINT_MAX;
		errno = ERANGE;
	}

	return ret;
}

noreturn static void
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
	       "-r  DSP rate                     16000\n"
	       "-q  Bandpass filter Q            10.0\n"
	       "-Q  Envelope lowpass filter Q    0.5\n"
	       "-1  F1 Macro                     <empty>\n"
	       "-2  F2 Macro                     \"CQ CQ CQ CQ CQ CQ DE W8BSD W8BSD W8BSD PSE K\"\n"
	       "-3  F3 Macro                     \"\\ \"\n"
	       "-4  F4 Macro                     \"` DE \\\"\n"
	       "-5  F5 Macro                     <empty>\n"
	       "-6  F6 Macro                     <empty>\n"
	       "-7  F7 Macro                     <empty>\n"
	       "-8  F8 Macro                     <empty>\n"
	       "-9  F9 Macro                     <empty>\n"
	       "-0  F10 Macro                    <empty>\n"
	       "-c  Charset to use               0\n"
	       "-a  Use AFSK (no argument)\n"
	       "-C  Callsign                     \"W8BSD\"\n"
	       "-T  Use rig control PTT (no argument)\n"
	       "-f  VFO frequency offset         170\n"
	       "-x  XML-RPC host name            \"localhost\"\n"
	       "-P  XML-RPC port                 7362\n"
	       "-N[s][e]\n"
	       "    Turns off automatically added text.  If 's' is included,\n"
	       "    the starting CRLF is not sent.  If 'e' is included, the\n"
	       "    ending space is not sent.\n"
	       "-i  rigctld host name            \"localhost\"\n"
	       "-I  rigctld port                 4532\n"
	       "\n", cmd);
	exit(EXIT_FAILURE);
}

static void
send_rtty_char(char ch)
{
	send_fsk->send_char(ch);
}

void
captured_callsign(const char *str)
{
	BSDTTY_LOCK();
	if (their_callsign) {
		free(their_callsign);
		their_callsign = NULL;
	}
	if (str != NULL && str[0] != 0)
		their_callsign = strdup(str);
	BSDTTY_UNLOCK();
}

/*
 * Settings write lock must be held
 */
static void
fix_config(void)
{
	if (settings.dsp_name == NULL)
		settings.dsp_name = strdup("/dev/dsp");
	if (settings.mark_freq < 1)
		settings.mark_freq = 2125;
	if (settings.mark_freq > (double)settings.dsp_rate / 2)
		settings.mark_freq = (double)settings.dsp_rate / 2 - 170;
	if (settings.space_freq < 1)
		settings.space_freq = 2295;
	if (settings.space_freq > (double)settings.dsp_rate / 2)
		settings.space_freq = (double)settings.dsp_rate / 2;
	if (settings.dsp_rate < 8000)
		settings.dsp_rate = 8000;
	if (settings.baud_denominator < 1)
		settings.baud_denominator = 1;
	if (settings.baud_numerator < 1)
		settings.baud_numerator = 1;
	if (settings.charset < 0)
		settings.charset = 0;
	if (settings.charset >= charset_count)
		settings.charset = 0;
	if (settings.rigctld_host == NULL || settings.rigctld_host[0] == 0 || settings.rigctld_port == 0)
		settings.ctl_ptt = false;
}

const char *
format_freq(uint64_t freq)
{
	static char fstr[32];
	const char *prefix = " kMGTPEZY";
	int pc = 0;
	int pos;
	char *ch;

	fstr[0] = 0;
	if (freq) {
		sprintf(fstr, "%" PRIu64, freq);
		for (pos = strlen(fstr) - 3; pos > 0; pos -= 3) {
			memmove(&fstr[pos]+1, &fstr[pos], strlen(&fstr[pos])+1);
			fstr[pos] = '.';
		}
	}
	while (strlen(fstr) > 11) {
		ch = strrchr(fstr, '.');
		if (ch == NULL)
			printf_errno("unable to find dot in freq \"%s\"", fstr);
		*ch = 0;
		pc++;
	}

	ch = strrchr(fstr, 0);
	if (ch == NULL)
		printf_errno("unable to find end of string");
	*(ch++) = prefix[pc];
	*(ch++) = 'H';
	*(ch++) = 'z';
	*ch = 0;

	return fstr;
}
