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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "bsdtty.h"
#include "fldigi_xmlrpc.h"
#include "fsk_demod.h"
#include "ui.h"

#ifdef WITH_OUTRIGGER
#include "api/api.h"
#include "iniparser/src/dictionary.h"
#endif

static bool do_tx(void);
static void done(void);
static bool get_rts(void);
static void handle_rx_char(char ch);
static void input_loop(void);
static const char *mode_name(enum rig_modes mode);
static void send_char(const char ch);
static void send_rtty_char(char ch);
static void setup_log(void);
static void setup_rig_control(void);
static void setup_tty(void);
static int sock_readln(int sock, char *buf, size_t bufsz);
static void usage(const char *cmd);
static void setup_defaults(void);

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

/* UART Stuff */
static int tty = -1;

/* RX in reverse mode */
bool reverse = false;

/* Log thing */
static FILE *log_file;

static char sync_buffer[9];
static int sb_chars;
static int sync_squelch = 1;
static int rigctld_socket = -1;
static bool rxfigs;
static bool txfigs;

char *their_callsign;
unsigned serial;

struct charset {
	const char *chars;
	const char *name;
};

static struct charset charsets[] = {
	{
		// From http://baudot.net/docs/smith--teletype-codes.pdf
		.name = "ITA2",
		.chars = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- '87"
		  "\r#4\x07" ",@:("
		  "5+)2$601"
		  "9?*\x0e" "./=\x0f"
	},
	{
		// From http://baudot.net/docs/smith--teletype-codes.pdf
		.name = "USTTY",
		.chars = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- \x07" "87"
		  "\r$4',!:("
		  "5\")2#601"
		  "9?&\x0e" "./;\x0f"
	},
	{
		// From ITU-T S.1 (official standard)
		/*
		 * WRU signal (who are you?) FIGS D is to operate answerback...
		 * It is therefore encoded as ENQ. (4.1)
		 * 
		 * FIGS F, G, and H are explicitly NOT DEFINED. 
		 * "arbitrary sign" such as a square to indicate an
		 * abnormal impression should occur. (4.2)
		 * 
		 * See U.11, U.20, U.22 and S.4 for NUL uses
		 */
		.name = "ITA2(S)",
		.chars = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n '87"
		  "\r\x05" "4\x07,\x00" ":("
		  "5+)2\x00" "601"
		  "9?\x00" "\x0e" "./=\x0f"
	  },
};

#ifdef WITH_OUTRIGGER
static dictionary *or_d;
static struct rig *rig;
#endif

int main(int argc, char **argv)
{
	int ch;

	load_config();

#ifdef WITH_OUTRIGGER
	while ((ch = getopt(argc, argv, "ac:C:d:D:f:hl:m:n:p:P:q:Q:r:R:s:t:T1:x:2:3:4:5:6:7:8:9:0:")) != -1) {
#else
	while ((ch = getopt(argc, argv, "ac:C:d:f:hl:m:n:p:P:q:Q:r:s:t:T1:x:2:3:4:5:6:7:8:9:0:")) != -1) {
#endif
		while (optarg && isspace(*optarg))
			optarg++;
		switch (ch) {
			case 'x':
				settings.xmlrpc_host = strdup(optarg);
				break;
			case 'P':
				settings.xmlrpc_port = strtoi(optarg, NULL, 10);
				break;
			case 'h':
				usage(argv[0]);
			case 'F':
				settings.freq_offset = strtoi(optarg, NULL, 10);
				break;
			case 'R':
				settings.or_rig = strdup(optarg);
				break;
			case 'D':
				settings.or_dev = strdup(optarg);
				break;
			case 'a':
				settings.afsk = true;
				break;
			case 'C':
				settings.callsign = strdup(optarg);
				break;
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
			case 'T':
				settings.ctl_ptt = true;
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
			case 'c':
				settings.charset = strtoi(optarg, NULL, 10);
				if (settings.charset < 0 || settings.charset >= sizeof(charsets) / sizeof(charsets[0]))
					settings.charset = 0;
				break;
			default:
				usage(argv[0]);
		}
	}

	setup_defaults();
	fix_config();

	setup_tty();

	setlocale(LC_ALL, "");
	atexit(done);

	// Now set up curses
	setup_curses();

	// Set up the FSK stuff.
	setup_rx();

	// Set up the log file
	setup_log();

	display_charset(charsets[settings.charset].name);
	update_squelch(sync_squelch);
	update_serial(serial);

	setup_rig_control();
	setup_xmlrpc();

	// Finally, do the thing.
	input_loop();
	return EXIT_SUCCESS;
}

void
reinit(void)
{
	load_config();
	fix_config();

	setup_tty();

	// Set up the FSK stuff.
	setup_rx();

	// Set up the log file
	setup_log();

	setup_rig_control();
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
		settings.macros[1] = strdup("CQ CQ CQ CQ CQ CQ DE \\ \\ \\ PSE K\t");
	if (settings.macros[2] == NULL)
		settings.macros[2] = strdup("\\ ");
	if (settings.macros[3] == NULL)
		settings.macros[3] = strdup("` DE \\\t");
	if (settings.callsign == NULL)
		settings.callsign = strdup("W8BSD");
	if (settings.rigctld_host == NULL)
		settings.rigctld_host = strdup("localhost");
#ifdef WITH_OUTRIGGER
	if (settings.or_rig == NULL)
		settings.or_rig = strdup("TS-940S");
	if (settings.or_dev == NULL)
		settings.or_dev = strdup("/dev/ttyu2");
#endif
}

static void
setup_log(void)
{
	time_t now;

	if (log_file != NULL)
		fclose(log_file);
	if (log_file)
		fclose(log_file);
	log_file = fopen(settings.log_name, "a");
	if (log_file == NULL)
		printf_errno("opening log file");
	now = time(NULL);
	if (log_file != NULL)
		fprintf(log_file, "\n\n%s\n", ctime(&now));
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
#ifdef TIOCSFBAUD
	struct baud_fraction bf;
#endif

	// Set up the UART
	if (tty != -1)
		close(tty);
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
#ifdef TIOCSFBAUD
	bf.bf_numerator = settings.baud_numerator;
	bf.bf_denominator = settings.baud_denominator;
	ioctl(tty, TIOCSFBAUD, &bf);
	ioctl(tty, TIOCGFBAUD, &bf);
#endif
}

char
asc2baudot(int asc, bool figs)
{
	char *ch = NULL;

	asc = toupper(asc);
	if (figs)
		ch = memchr(charsets[settings.charset].chars + 0x20, asc, 0x20);
	if (ch == NULL)
		ch = memchr(charsets[settings.charset].chars, asc, 0x40);
	if (ch == NULL)
		return 0;
	return ch - charsets[settings.charset].chars;
}

char
baudot2asc(int baudot, bool figs)
{
	if (baudot < 0 || baudot > 0x1f)
		return 0;
	return charsets[settings.charset].chars[baudot + figs * 0x20];
}

static void
handle_rx_char(char ch)
{
	if (log_file != NULL)
		fwrite(&ch, 1, 1, log_file);
	switch(ch) {
		case 0:
			return;
		case 0x07:	// BEL
			return;
		case 0x0f:	// LTRS
			rxfigs = false;
			return;
		case 0x0e:	// FIGS
			rxfigs = true;
			return;
		case ' ':
			rxfigs = false;	// USOS
	}
	fldigi_add_rx(ch);
	write_rx(ch);
}

static void
input_loop(void)
{
	bool rx_mode = true;
	int rxstate = -1;
	int i;
	char ch;

	while (1) {
		handle_xmlrpc();
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
			if (rxstate < 0) {
				sb_chars = 0;
				rxfigs = false;
				continue;
			}
			if (rxstate > 0x20)
				printf_errno("got a==%d", rxstate);
			ch = baudot2asc(rxstate, rxfigs);
			if (sb_chars < sync_squelch) {
				sync_buffer[sb_chars++] = ch;
				if (sb_chars == sync_squelch) {
					for (i = 0; i < sync_squelch; i++)
						handle_rx_char(sync_buffer[i]);
					continue;
				}
				else
					continue;
			}
			handle_rx_char(ch);
		}
	}
}

static bool
do_tx(void)
{
	int ch;

	for (;;) {
		ch = get_input();
		switch (ch) {
			case -1:
				if (settings.afsk)
					send_rtty_char(0x1f);
				else
					printf_errno("getting character");
				break;
			case 3:
				return false;
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
			case RTTY_KEY_LEFT:
				sync_squelch--;
				if (sync_squelch < 1)
					sync_squelch = 1;
				update_squelch(sync_squelch);
				break;
			case RTTY_KEY_RIGHT:
				sync_squelch++;
				if (sync_squelch > 9)
					sync_squelch = 9;
				update_squelch(sync_squelch);
				break;
			case RTTY_KEY_UP:
				serial++;
				update_serial(serial);
				break;
			case RTTY_KEY_DOWN:
				if (serial)
					serial--;
				update_serial(serial);
				break;
			case RTTY_KEY_REFRESH:
				display_charset(charsets[settings.charset].name);
				update_squelch(sync_squelch);
				show_reverse(reverse);
				update_captured_call(their_callsign);
				update_serial(serial);
				break;
			case '`':
				toggle_reverse(&reverse);
				break;
			case '[':
				settings.charset--;
				if (settings.charset < 0)
					settings.charset = sizeof(charsets) / sizeof(charsets[0]) - 1;
				display_charset(charsets[settings.charset].name);
				break;
			case ']':
				// TODO: CR is expanded to CRLF...
#if 0
				settings.charset++;
				if (settings.charset == sizeof(charsets) / sizeof(charsets[0]))
					settings.charset = 0;
				display_charset(charsets[settings.charset].name);
#endif
				break;
			case '\\':
				reset_tuning_aid();
				break;
			case 23:
				if (!get_rts())
					toggle_tuning_aid();
				break;
			case 0x7f:
			case 0x08:
				if (!get_rts())
					change_settings();
				break;
			default:
				send_char(ch);
				break;
		}
		if (!get_rts()) {
			return true;
		}
	}
}

void
send_string(const char *str)
{
	if (str == NULL)
		return;

	for (; *str; str++)
		send_char(*str);
}

bool
do_macro(int fkey)
{
	size_t len;
	size_t i;
	int m;
	char buf[11];

	m = fkey - 1;
	if (settings.macros[m] == NULL)
		return true;
	if (strncasecmp(settings.macros[m], "CQ CQ", 5) == 0)
		clear_rx_window();
	len = strlen(settings.macros[m]);
	for (i = 0; i < len; i++) {
		switch (settings.macros[m][i]) {
			case '\\':
				send_string(settings.callsign);
				break;
			case '`':
				send_string(their_callsign);
				break;
			case '[':
				send_char('\r');
				break;
			case ']':
				send_char('\n');
				break;
			case '^':
				serial++;
				/* Fall-through */
			case '%':
				sprintf(buf, "%d", serial);
				send_string(buf);
				update_serial(serial);
				break;
			default:
				send_char(settings.macros[m][i]);
				break;
		}
	}
	if (len > 2)
		if (strcasecmp(settings.macros[m] + len - 3, " CQ") == 0)
			clear_rx_window();
	return true;
}

static void
send_char(const char ch)
{
	const char fstr[] = "\x1f\x1b"; // LTRS, FIGS
	char bch;
	bool rts;
	int state;
	char ach;
	time_t now;
	static uint64_t freq = 0;
	static char mode[16];

	bch = asc2baudot(ch, txfigs);
	rts = get_rts();
	if (ch == '\t' || (!rts && bch != 0)) {
		rts ^= 1;
		state = TIOCM_RTS;
		if (!rts) {
			send_rtty_char(4);
			if (settings.afsk)
				end_afsk_tx();
			else {
				ioctl(tty, TIOCDRAIN);
				// Space still gets cut off... wait one char
				usleep(((1/((double)settings.baud_numerator / settings.baud_denominator))*7.5)*1000000);
			}
		}
		if (rts && rig) {
			freq = get_rig_freq() + settings.freq_offset;
			get_rig_mode(mode, sizeof(mode));
		}
		set_rig_ptt(rts);
		if (rts) {
			/* 
			 * Start with a byte length of mark to help sync...
			 * This also covers the RX -> TX switching time
			 * due to the relay.
			 */
			if (settings.afsk) {
				send_afsk_bit(AFSK_STOP);
				send_afsk_bit(AFSK_STOP);
				send_afsk_bit(AFSK_STOP);
				send_afsk_bit(AFSK_STOP);
				send_afsk_bit(AFSK_STOP);
			}
			else {
				/* Hold it in mark for 1 byte time. */
				usleep(((1/((double)settings.baud_numerator / settings.baud_denominator))*7.5)*1000000);
			}
			txfigs = false;
			/*
			 * Per ITU-T S.1, the FIRST symbol should be a
			 * shift... since it's most likely to be lost,
			 * a LTRS is the safest, since a FIGS will get
			 * repeated after the CRLF.
			 */
			send_rtty_char(0x1f);
			send_rtty_char(8);
			send_rtty_char(2);
		}
		now = time(NULL);
		if (log_file != NULL) {
			if (freq == 0)
				fprintf(log_file, "\n------- %s of transmission (%.24s) -------\n", rts ? "Start" : "End", ctime(&now));
			else
				fprintf(log_file, "\n------- %s of transmission (%.24s) on %s (%s) -------\n", rts ? "Start" : "End", ctime(&now), format_freq(freq), mode);
			fflush(log_file);
		}
		mark_tx_extent(rts);
	}
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
				if (log_file != NULL)
					fwrite(&ach, 1, 1, log_file);
				break;
			case 0x0e:
				txfigs = false;
				if (log_file != NULL)
					fwrite(&ach, 1, 1, log_file);
				break;
			case 0x0f:
				txfigs = true;
				if (log_file != NULL)
					fwrite(&ach, 1, 1, log_file);
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

static bool
get_rts(void)
{
	int state;

	if (settings.ctl_ptt)
		return get_rig_ptt();
	if (ioctl(tty, TIOCMGET, &state) == -1)
		printf_errno("getting RTS state");
	return !!(state & TIOCM_RTS);
}

static void
done(void)
{
	int state = 0;

	if (settings.ctl_ptt) {
		set_rig_ptt(false);
#ifdef WITH_OUTRIGGER
		if (rig)
			close_rig(rig);
#endif
		if (rigctld_socket != -1)
			close(rigctld_socket);
	}

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
#ifdef WITH_OUTRIGGER
	       "-R  Outrigger rig name           \"TS-940S\"\n"
	       "-D  Outrigger port device name   \"/dev/ttyu2\"\n"
#endif
	       "-f  VFO frequency offset         170\n"
	       "-x  XML-RPC host name            \"localhost\"\n"
	       "-P  XML-RPC port                 7362\n"
	       "\n", cmd);
	exit(EXIT_FAILURE);
}

static void
send_rtty_char(char ch)
{
	if (settings.afsk) {
		send_afsk_char(ch);
	}
	else {
		if (write(tty, &ch, 1) != 1)
			printf_errno("error sending FIGS/LTRS");
	}
}

void
captured_callsign(const char *str)
{
	if (their_callsign)
		free(their_callsign);
	if (str == NULL || str[0] == 0)
		return;
	their_callsign = strdup(str);
}

static void
setup_rig_control(void)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV
	};
	struct addrinfo *ai;
	struct addrinfo *aip;
	char port[6];
	int opt;

#ifdef WITH_OUTRIGGER
	if (or_d) {
		dictionary_del(or_d);
		or_d = NULL;
	}
	if (rig) {
		close_rig(rig);
		rig = NULL;
	}
	if (settings.or_rig && settings.or_rig[0] &&
	    settings.or_dev && settings.or_dev[0]) {
		if (or_d)
			dictionary_del(or_d);
		or_d = dictionary_new(0);

		dictionary_set(or_d, "rig:rig", settings.or_rig);
		dictionary_set(or_d, "rig:port", settings.or_dev);

		if (rig)
			close_rig(rig);
		rig = init_rig(or_d, "rig");
		if (settings.ctl_ptt && rig == NULL)
			printf_errno("unable to control rig");
		if (rig != NULL)
			return;
	}
#endif
	if (rigctld_socket != -1) {
		close(rigctld_socket);
		rigctld_socket = -1;
	}
	if (settings.rigctld_host && settings.rigctld_host[0] &&
	    settings.rigctld_port) {
		sprintf(port, "%hu", settings.rigctld_port);
		if (getaddrinfo(settings.rigctld_host, port, &hints, &ai) != 0)
			return;
		for (aip = ai; aip != NULL; aip = aip->ai_next) {
			rigctld_socket = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol);
			if (rigctld_socket == -1)
				continue;
			if (connect(rigctld_socket, aip->ai_addr, aip->ai_addrlen) == 0) {
				opt = 1;
				setsockopt(rigctld_socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
				break;
			}
			close(rigctld_socket);
			rigctld_socket = -1;
		}
		if (settings.ctl_ptt && rigctld_socket == -1)
			printf_errno("unable to connect to rigctld");
		freeaddrinfo(ai);
	}
}

void
fix_config(void)
{
	if (settings.dsp_name == NULL)
		settings.dsp_name = strdup("/dev/dsp");
	if (settings.mark_freq < 1)
		settings.mark_freq = 2125;
	if (settings.mark_freq > settings.dsp_rate / 2)
		settings.mark_freq = settings.dsp_rate / 2 - 170;
	if (settings.space_freq < 1)
		settings.space_freq = 2295;
	if (settings.space_freq > settings.dsp_rate / 2)
		settings.space_freq = settings.dsp_rate / 2;
	if (settings.dsp_rate < 8000)
		settings.dsp_rate = 8000;
	if (settings.baud_denominator < 1)
		settings.baud_denominator = 1;
	if (settings.baud_numerator < 1)
		settings.baud_numerator = 1;
	if (settings.charset < 0)
		settings.charset = 0;
	if (settings.charset > sizeof(charsets) / sizeof(charsets[0]))
		settings.charset = 0;
	if ((settings.or_rig == NULL || *settings.or_rig == 0 || settings.or_dev == NULL || *settings.or_dev == 0) &&
	    (settings.rigctld_host == NULL || settings.rigctld_host[0] == 0 || settings.rigctld_port == 0))
		settings.ctl_ptt = false;
}

static const char *
mode_name(enum rig_modes mode)
{
	switch(mode) {
		case MODE_UNKNOWN:
			return "";
		case MODE_CW:
			return "CW";
		case MODE_CWN:
			return "CWN";
		case MODE_CWR:
			return "CWR";
		case MODE_CWRN:
			return "CWRN";
		case MODE_AM:
			return "AM";
		case MODE_LSB:
			return "LSB";
		case MODE_USB:
			return "USB";
		case MODE_FM:
			return "FM";
		case MODE_FMN:
			return "FMN";
		case MODE_FSK:
			return "FSK";
	}
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

static int
sock_readln(int sock, char *buf, size_t bufsz)
{
	fd_set rd;
	int i;
	int ret;
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 500000
	};

	for (i = 0; i < bufsz - 1; i++) {
		FD_ZERO(&rd);
		FD_SET(sock, &rd);
		switch(select(sock+1, &rd, NULL, NULL, &tv)) {
			case -1:
				if (errno == EINTR)
					continue;
				return -1;
			case 0:
				return -1;
			case 1:
				ret = recv(sock, buf + i, 1, MSG_WAITALL);
				if (ret == -1)
					return -1;
				if (buf[i] == '\n')
					goto done;
				break;
		}
	}
done:
	buf[i] = 0;
	while (i > 0 && buf[i-1] == '\n')
		buf[--i] = 0;
	return i;
}

uint64_t
get_rig_freq(void)
{
	uint64_t ret;
	char buf[1024];

	if (rig)
		return get_frequency(rig, VFO_UNKNOWN);
	if (rigctld_socket != -1) {
		if (send(rigctld_socket, "f\n", 2, 0) != 2)
			goto next;
		if (sock_readln(rigctld_socket, buf, sizeof(buf)) <= 0) {
			close(rigctld_socket);
			rigctld_socket = -1;
			if (settings.ctl_ptt)
				printf_errno("lost connection getting rig frequency");
			goto next;
		}
		if (sscanf(buf, "%" SCNu64, &ret) == 1)
			return ret;
	}
next:

	return 0;
}

const char *
get_rig_mode(char *buf, size_t sz)
{
	char tbuf[1024];

	if (rig) {
		snprintf(buf, sz, "%s", mode_name(get_mode(rig)));
		return buf;
	}

	if (rigctld_socket != -1) {
		if (send(rigctld_socket, "m\n", 2, 0) != 2)
			goto next;
		if (sock_readln(rigctld_socket, buf, sz) <= 0) {
			close(rigctld_socket);
			rigctld_socket = -1;
			if (settings.ctl_ptt)
				printf_errno("lost connection getting rig mode");
			goto next;
		}
		if (sock_readln(rigctld_socket, tbuf, sz) <= 0) {
			close(rigctld_socket);
			rigctld_socket = -1;
			if (settings.ctl_ptt)
				printf_errno("lost connection getting rig bandwidth");
			goto next;
		}
		return buf;
	}
next:
	if (sz >= 1)
		buf[0] = 0;
	return NULL;
}

bool
get_rig_ptt(void)
{
	char buf[1024];
	int state;

	if (settings.ctl_ptt) {
		if (rig)
			return get_ptt(rig);

		if (rigctld_socket != -1) {
			if (send(rigctld_socket, "t\n", 2, 0) != 2)
				goto next;
			if (sock_readln(rigctld_socket, buf, sizeof(buf)) <= 0) {
				close(rigctld_socket);
				rigctld_socket = -1;
				if (settings.ctl_ptt)
					printf_errno("lost connection getting rig PTT");
				goto next;
			}
			if (buf[0] == '1')
				return true;
			return false;
		}
	}
next:

	if (ioctl(tty, TIOCMGET, &state) == -1)
		printf_errno("getting RTS state");
	return !!(state & TIOCM_RTS);
}

bool
set_rig_ptt(bool val)
{
	char buf[1024];
	int state = TIOCM_RTS;
	int ret = true;
	int i;

	if (settings.ctl_ptt) {
		if (rig)
			ret = set_ptt(rig, val);
		else {
			if (rigctld_socket != -1) {
				sprintf(buf, "T %d\n", val);
				if (send(rigctld_socket, buf, strlen(buf), 0) != strlen(buf))
					goto next;
				if (sock_readln(rigctld_socket, buf, sizeof(buf)) <= 0) {
					close(rigctld_socket);
					rigctld_socket = -1;
					if (settings.ctl_ptt)
						printf_errno("lost connection setting rig PTT");
					goto next;
				}
				if (strcmp(buf, "RPRT 0") == 0)
					ret = true;
				else
					ret = false;
			}
		}
		if (ret) {
			for (i = 0; i < 200; i++) {
				if (get_rig_ptt() == val)
					break;
				usleep(10000);
			}
		}
		return ret;
	}
next:

	if (ioctl(tty, val ? TIOCMBIS : TIOCMBIC, &state) != 0)
		printf_errno("%s RTS bit", val ? "setting" : "resetting");
	return false;
}
