#include <sys/soundcard.h>
#include <sys/stat.h>

#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *);
static void printf_errno(const char *format, ...);
static void input_loop(void);
static void done(void);
static void setup_tty(void);
static void setup_curses(void);
static void w_printf(WINDOW *win, const char *format, ...);
static void setup_windows(void);
static char asc2baudot(int asc, bool figs);
static int get_rtty_ch(int state);
static bool get_bit(void);
static double current_value(void);
static void setup_audio(void);
static void create_filters(void);
static void calc_bpf_coef(double f0, double q, double *filt);
static void calc_lpf_coef(double f0, double q, double *filt);
static void calc_apf_coef(double f0, double q, double *filt);
static double bq_filter(double value, double *filt, double *buf);
static bool get_stop_bit(void);
static int avail(int head, int tail, int max);
static int prev(int val, int max);
static int next(int val, int max);
static void update_tuning_aid(double mark, double space);
static void setup_log(void);

/* UART Stuff */
static int tty = -1;
static char *tty_name = NULL;

/* Audio variables */
static char *dsp_name = "/dev/dsp8";
static int dsp = -1;
static int dsp_rate = 48000;
static int dsp_channels = 1;
static uint16_t *dsp_buf;
static int head=0, tail=0;	// Empty when equal
static size_t dsp_bufmax;

/* RX Stuff */
static double bp_filter_q = 10;
static double lp_filter_q1 = 1;
static double space_freq = 2295;
static double mark_freq = 2125;
static int baud_numerator = 1000;
static int baud_denominator = 22;
static int last_ro = -1;
static double phase_rate;
static double phase = 0;
// Mark filter
static double mbpfilt[5];
static double mlpfilt[5];
static double mbpbuf[4];
static double mlpbuf[4];
// Space filter
static double sbpfilt[5];
static double slpfilt[5];
static double sbpbuf[4];
static double slpbuf[4];
// Mark phase filter
static double mapfilt[5];
static double mapbuf[4];
// Space phase filter
static double sapfilt[5];
static double sapbuf[4];
// Hunt for Space
static double *hfs_buf = NULL;
static size_t hfs_bufmax;
static int hfs_head;
static int hfs_tail;
static int hfs_start;
static int hfs_b0;
static int hfs_b1;
static int hfs_b2;
static int hfs_b3;
static int hfs_b4;
static int hfs_stop1;
static int hfs_stop2;

/* UI Stuff */
static WINDOW *status;
static WINDOW *status_title;
static WINDOW *rx;
static WINDOW *rx_title;
static WINDOW *tx;
static WINDOW *tx_title;
static int tx_width;
static int tx_height;

/* Log thing */
static FILE *log_file;
static char *log_name = "bsdtty.log";

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
	// TODO: Parse a command-line...
	if (argc != 2)
		usage(argv[0]);

	tty_name = argv[1];

	setup_tty();

	setlocale(LC_ALL, "");
	atexit(done);

	// Now set up curses
	setup_curses();

	// Set up the FSK stuff.
	create_filters();

	// Set up the audio stuff.
	setup_audio();

	// Set up the log file
	setup_log();

	// Finally, do the thing.
	input_loop();
	return EXIT_SUCCESS;
}

static void
setup_log(void)
{
	time_t now;

	if (log_file != NULL)
		fclose(log_file);
	log_file = fopen(log_name, "wb");
	if (log_file == NULL)
		printf_errno("opening log file");
	now = time(NULL);
	fprintf(log_file, "\r\n\r\n%s\r\n", ctime(&now));
}

static void
setup_curses(void)
{
	initscr();
	cbreak();
	noecho();
	nonl();
	keypad(stdscr, TRUE);

	setup_windows();
}

static void
setup_windows(void)
{
	struct winsize ws;
	int datrows;
	int i;
	int y, x;

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
		printf_errno("getting window size");
	if (ws.ws_row < 6)
		printf_errno("not enough rows");
	if (ws.ws_col < 10)
		printf_errno("not enough columns");
	datrows = ((ws.ws_row - 4) / 2);
	if ((status_title = newwin(1, ws.ws_col, 0, 0)) == NULL)
		printf_errno("creating status_title window");
	if ((status = newwin(1, ws.ws_col, 1, 0)) == NULL)
		printf_errno("creating status window");
	if ((rx_title = newwin(1, ws.ws_col, 2, 0)) == NULL)
		printf_errno("creating rx_title window");
	if ((rx = newwin(ws.ws_row - 4 - datrows, ws.ws_col, 3, 0)) == NULL)
		printf_errno("creating rx window");
	if ((tx_title = newwin(1, ws.ws_col, ws.ws_row - datrows - 1, 0)) == NULL)
		printf_errno("creating tx_title window");
	if ((tx = newwin(datrows, ws.ws_col, ws.ws_row - datrows, 0)) == NULL)
		printf_errno("creating tx window");
	tx_width = ws.ws_col;
	tx_height = datrows;
	scrollok(status_title, FALSE);
	scrollok(status, FALSE);
	scrollok(rx_title, FALSE);
	idlok(rx, TRUE);
	wsetscrreg(rx, 0, ws.ws_row - 4 - datrows - 1);
	scrollok(rx, TRUE);
	scrollok(tx_title, FALSE);
	idlok(tx, TRUE);
	wsetscrreg(tx, 0, datrows - 1);
	scrollok(tx, TRUE);
	wclear(status_title);
	wclear(status);
	wclear(rx_title);
	wclear(rx);
	wclear(tx_title);
	wclear(tx);
	wmove(status_title, 0, 0);
	wmove(status, 0, 0);
	wmove(rx_title, 0, 0);
	wmove(rx, 0, 0);
	wmove(tx_title, 0, 0);
	wmove(tx, 0, 0);
	for (i = 0; i < 3; i++) {
		waddch(status_title, ACS_HLINE);
		waddch(rx_title, ACS_HLINE);
		waddch(tx_title, ACS_HLINE);
	}
	waddstr(status_title, " Status ");
	getyx(status_title, y, x);
	for (i = x; i < ws.ws_col; i++)
		waddch(status_title, ACS_HLINE);
	waddstr(rx_title, " RX ");
	getyx(rx_title, y, x);
	for (i = x; i < ws.ws_col; i++)
		waddch(rx_title, ACS_HLINE);
	waddstr(tx_title, " TX ");
	getyx(tx_title, y, x);
	for (i = x; i < ws.ws_col; i++)
		waddch(tx_title, ACS_HLINE);
	wrefresh(status_title);
	wrefresh(status);
	wrefresh(rx_title);
	wrefresh(rx);
	wrefresh(tx_title);
	wrefresh(tx);
	wtimeout(tx, -1);
	wtimeout(rx, 0);
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
	tty = open(tty_name, O_RDWR|O_DIRECT|O_NONBLOCK);
	if (tty == -1)
		printf_errno("unable to open %s");

	/*
	 * In case stty wasn't used on the init device, turn off DTR and
	 * CTS hopefully before anyone notices
	 */
	if (ioctl(tty, TIOCMBIC, &state) != 0)
		printf_errno("unable clear RTS/DTR");

	if (tcgetattr(tty, &t) == -1)
		printf_errno("unable to read term caps");

	cfmakeraw(&t);

	/* May as well set to 45 for devices that don't support FBAUD */
	if (cfsetspeed(&t, baud_numerator/baud_denominator) == -1)
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
	bf.bf_numerator = baud_numerator;
	bf.bf_denominator = baud_denominator;
	ioctl(tty, TIOCSFBAUD, &bf);
	ioctl(tty, TIOCGFBAUD, &bf);
}

static void
w_printf(WINDOW *win, const char *format, ...)
{
	char *msg = NULL;
	va_list ap;

	va_start(ap, format);
	if (vasprintf(&msg, format, ap) < 0)
		msg = NULL;
	va_end(ap);
	waddstr(win, msg);
	wrefresh(win);
}

static void
printf_errno(const char *format, ...)
{
	int eno = errno;
	char *msg = NULL;
	va_list ap;

	endwin();
	va_start(ap, format);
	if (vasprintf(&msg, format, ap) < 0)
		msg = NULL;
	va_end(ap);
	printf("%s (%s)\n", strerror(eno), msg ? msg : "");
	exit(EXIT_FAILURE);
}

static void
usage(const char *cmd)
{
	printf("Usage: %s /path/to/tty\n", cmd);
	exit(EXIT_FAILURE);
}

static char
asc2baudot(int asc, bool figs)
{
	char *ch = NULL;

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
	bool figs = false;
	int ch;
	char bch;
	const char fstr[] = "\x1f\x1b"; // LTRS, FIGS
	int state;
	bool rts = false;
	bool rx_mode = true;
	bool rxfigs = false;
	int rxstate = -1;

	while (1) {
		if (!rx_mode) {	// TX Mode
			ch = wgetch(tx);
			switch (ch) {
				case ERR:
					printf_errno("getting character");
				case KEY_BREAK:
				case 3:
					return;
				default:
					bch = asc2baudot(toupper(ch), figs);
					if (ch == '\t' || (!rts && bch != 0)) {
						rts ^= 1;
						state = TIOCM_RTS;
						if (!rts) {
							write(tty, "\x04", 1);
							ioctl(tty, TIOCDRAIN);
							// Space still gets cut off... wait one char
							usleep(((1/((double)baud_numerator / baud_denominator))*7.5)*1000000);
						}
						if (ioctl(tty, rts ? TIOCMBIS : TIOCMBIC, &state) != 0) {
							printf_errno("%s RTS bit", rts ? "setting" : "resetting");
							continue;
						}
						if (rts) {
							figs = false;
							write(tty, "\x1f\x1f\x1f\x08\x02", 5);
							waddstr(tx, "\n------- Start of transmission -------\n");
						}
						else
							waddstr(tx, "\n-------- End of transmission --------\n");
					}
					if (bch == 0)
						bch = 0x1f;	// Deedle...
					if (bch) {
						// Send FIGS/LTRS as needed
						if ((!!(bch & 0x20)) != figs) {
							figs = !!(bch & 0x20);
							if (write(tty, &fstr[figs], 1) != 1)
								printf_errno("error sending FIGS/LTRS");
						}
						switch (b2a[(int)bch]) {
							case 0:
							case 0x0e:
							case 0x0f:
								fwrite(&b2a[(int)bch], 1, 1, log_file);
								break;
							case '\r':
								waddch(tx, '\n');
								if (write(tty, "\x02", 1) != 1)
									printf_errno("error sending linefeed");
							default:
								waddch(tx, b2a[(int)bch]);
								fwrite(&b2a[(int)bch], 1, 1, log_file);
						}
						if (b2a[(int)bch] == ' ')
							figs = false;	// USOS
						bch &= 0x1f;
						if (write(tty, &bch, 1) != 1)
							printf_errno("error sending char 0x%02x", bch);
						// Expand CR to CRLF
						wrefresh(tx);
					}
					break;
			}
			if (!rts) {
				rx_mode = true;
				last_ro = -1;
				head = tail = 0;
				rxstate = -1;
			}
		}
		else {
			ch = wgetch(rx);
			if (ch != ERR) {
				ungetch(ch);
				rx_mode = false;
				continue;
			}

			rxstate = get_rtty_ch(rxstate);
			if (rxstate < 0)
				continue;
			if (rxstate > 0x20)
				printf_errno("got a==%d", rxstate);
			ch = b2a[rxstate+(0x20*rxfigs)];
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
					rxfigs = false;
			}
			waddch(rx, ch);
			wrefresh(rx);
		}
	}
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
	endwin();
	exit(EXIT_SUCCESS);
}

static int
get_rtty_ch(int state)
{
	bool b;
	int i;
	int ret = 0;

	/*
	 * Synchronize with the beginning of the start bit.
	 */
	if (state >= 0) {
		/*
		 * We got a stop bit last time, assume we're
		 * synchronized, and wait for up to 1.1 bit times for
		 * space to start.
		 * 
		 * If it doesn't start, go to "hunt for start" mode.
		 */
		for (phase = 0;;) {
			if (current_value() < 0.0)
				break;
			phase += phase_rate;
			if (phase >= 1.6)
				return -1;
		}
	}
	else if (state == -1) {
		/*
		 * Start of "Hunt for Start" mode... this one is fun
		 * since it looks for a whole character rather than
		 * parsing as it goes.  This works by having a charlen
		 * buffer of cv, and any time we cross from mark to
		 * space, we look back and see if we have a stop bit
		 * at the end along with a start bit at the start.
		 * If we do, we return THAT character, and assume
		 * synchronization.
		 */
		hfs_tail = 0;
		for (hfs_head = 0; hfs_head <= hfs_bufmax; hfs_head++)
			hfs_buf[hfs_head] = current_value();
		hfs_head = hfs_bufmax;
		hfs_start = hfs_bufmax * 0.133333333333333333;
		hfs_b0 = hfs_bufmax * 0.266666666666666666;
		hfs_b1 = hfs_bufmax * 0.4;
		hfs_b2 = hfs_bufmax * 0.533333333333333333;
		hfs_b3 = hfs_bufmax * 0.666666666666666666;
		hfs_b4 = hfs_bufmax * 0.8;
		hfs_stop1 = hfs_bufmax * 0.866666666666666666;
		hfs_stop2 = hfs_bufmax * 0.933333333333333333;
		return -2;
	}
	else {
		/*
		 * Now we're in HfS mode
		 * First, check the existing buffer.
		 */
		if (hfs_buf[hfs_tail] < 0.0 && hfs_buf[prev(hfs_tail, hfs_bufmax)] >= 0.0) {
			/* If there's a valid character in there, return it. */
			if (hfs_buf[hfs_start] < 0.0 &&
			    hfs_buf[hfs_stop1] >= 0.0 &&
			    hfs_buf[hfs_stop2] >= 0.0) {
				return (hfs_buf[hfs_b0] > 0.0) |
					((hfs_buf[hfs_b1] > 0.0) << 1) |
					((hfs_buf[hfs_b2] > 0.0) << 2) |
					((hfs_buf[hfs_b3] > 0.0) << 3) |
					((hfs_buf[hfs_b4] > 0.0) << 4);
			}
		}

		/* Now update it and stay in HfS. */
		hfs_buf[hfs_head] = current_value();
		hfs_head = next(hfs_head, hfs_bufmax);
		hfs_tail = next(hfs_tail, hfs_bufmax);
		hfs_start = next(hfs_start, hfs_bufmax);
		hfs_b0 = next(hfs_b0, hfs_bufmax);
		hfs_b1 = next(hfs_b1, hfs_bufmax);
		hfs_b2 = next(hfs_b2, hfs_bufmax);
		hfs_b3 = next(hfs_b3, hfs_bufmax);
		hfs_b4 = next(hfs_b4, hfs_bufmax);
		hfs_stop1 = next(hfs_stop1, hfs_bufmax);
		hfs_stop2 = next(hfs_stop2, hfs_bufmax);
		return -2;
	}

	/*
	 * Now we get the start bit... this is how we synchronize,
	 * so reset the phase here.
	 */
	phase = 0;
	b = get_bit();
	if (b)
		return -1;

	/* Now read the five data bits */
	for (i = 0; i < 5; i++) {
		b = get_bit();
		ret |= b << i;
	}

	/* 
	 * Now, get a stop bit, which we expect to be at least
	 * 1.42 bits long.
	 */
	if (!get_stop_bit())
		return -1;

	return ret;
}

/*
 * This gets a single bit value.
 */
static bool
get_bit(void)
{
	int nsamp;
	double cv;
	double tot;

	for (nsamp = 0; phase < 1.03; phase += phase_rate) {
		/* We only sample in the middle of the phase */
		cv = current_value();
		if (phase > 0.5 && nsamp == 0) {
			tot = cv;
			nsamp++;
		}
		/* Sampling is over, look for jitter */
		if (phase > 0.97) {
			if ((cv < 0.0) != (tot <= 0)) {
				// Value change... assume this is the end of the bit.
				// Set start phase for next bit.
				phase = 1 - phase;
				return tot > 0;
			}
		}
	}
	/* We over-read this bit... adjust next bit phase */
	phase = -(1.0 - phase);
	return tot > 0;
}

/*
 * This gets a stop bit.
 */
static bool
get_stop_bit(void)
{
	int i;
	int need = ((double)dsp_rate / ((double)baud_numerator / baud_denominator)) * 0.9;
	int rst = 0;
	double cv;

	for (i = 0; i < need; i++) {
		if ((cv = current_value()) < 0.0) {
			i = 0;
			rst++;
			if (rst > need*2)
				return false;
			continue;
		}
	}
	return true;
}

static int
avail(int head, int tail, int max)
{
	if (head == tail)
		return max;
	if (head < tail)
		return tail - head;
	return (max - head) + tail;
}

static int
prev(int val, int max)
{
	if (--val < 0)
		val = max;
	return val;
}

static int
next(int val, int max)
{
	if (++val > max)
		val = 0;
	return val;
}

/*
 * The current demodulated value.  Essentially the difference between
 * the mark and space envelopes.
 */
static double
current_value(void)
{
	int ret;
	int max;
	uint16_t tmpbuf[1];
	uint16_t *tb = tmpbuf;
	double mv, emv, sv, esv, cv;
	int i, j;
	audio_errinfo errinfo;

	/* Read into circular buffer */

	if (avail(head, tail, dsp_bufmax) > (sizeof(tmpbuf) / sizeof(*tb) / dsp_channels)) {
		ret = read(dsp, tmpbuf, sizeof(tmpbuf));
		if (ret == -1)
			printf_errno("reading audio input");
		if (head >= tail) {
			max = sizeof(tmpbuf) / sizeof(tmpbuf[0]) / dsp_channels;
			if (max > ret / sizeof(*tb) / dsp_channels)
				max = ret / sizeof(*tb) / dsp_channels;
			i = (dsp_bufmax - head + 1) >= max ? max : (dsp_bufmax - head + 1);
			if (dsp_channels == 1) {
				memcpy(dsp_buf + head, tb, i * sizeof(dsp_buf[0]));
				tb += i;
			}
			else {
				for (j = 0; j < i; j++) {
					memcpy(dsp_buf + head + j, tb, sizeof(*tb));
					tb += dsp_channels;
				}
			}
			ret -= i * sizeof(*tb) * dsp_channels;
			head += i;
			if (tail == head)
				printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
			if (head > dsp_bufmax)
				head -= dsp_bufmax;
		}
		if (head < tail) {
			if (dsp_channels == 1) {
				memcpy(dsp_buf + head, tb, ret);
				head += ret / sizeof(*tb);
				if (tail == head)
					printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
			}
			else {
				for (j = 0; j < ret; ret += sizeof(*tb)) {
					memcpy(dsp_buf + head, tb, sizeof(*tb));
					tb += dsp_channels;
					head++;
					if (tail == head)
						printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
				}
			}
		}
		ret = ioctl(dsp, SNDCTL_DSP_GETERROR, &errinfo);
		if (ret == -1)
			printf_errno("reading audio errors");
#if 0
		if (last_ro != -1 && errinfo.rec_overruns)
			printf_errno("rec_overrun (%d)", errinfo.rec_overruns);
		last_ro = 0;
#endif
	}

	if (tail == head)
		printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
	mv = bq_filter(dsp_buf[tail], mbpfilt, mbpbuf);
	emv = bq_filter(mv*mv, mlpfilt, mlpbuf);
	sv = bq_filter(dsp_buf[tail], sbpfilt, sbpbuf);
	esv = bq_filter(sv*sv, slpfilt, slpbuf);
	update_tuning_aid(bq_filter(mv, mapfilt, mapbuf), bq_filter(sv, sapfilt, sapbuf));
	tail++;
	if (tail > dsp_bufmax)
		tail = 0;

	cv = emv - esv;

	/* Return the current value */
	return cv;
}

static void
update_tuning_aid(double mark, double space)
{
	static double *buf = NULL;
	static int wsamp = 0;
	static int nsamp = -1;
	static double maxm = 0;
	static double maxs = 0;
	int mmult, smult;
	int madd, sadd;
	int phaseadj = 0;
	int y, x;
	chtype ch;
	int och;

	if (nsamp == -1)
		nsamp = dsp_rate/((((double)baud_numerator / baud_denominator))*7.5);
	if (buf == NULL) {
		buf = malloc(sizeof(*buf) * nsamp * 2);
	}
	if (phaseadj < 0) {
		if (wsamp >= phaseadj)
			buf[(wsamp - phaseadj)*2] = mark;
		buf[wsamp*2+1] = space;
	}
	else {
		buf[wsamp*2] = mark;
		if (wsamp >= phaseadj)
			buf[(wsamp - phaseadj)*2+1] = space;
	}
	if (abs(mark) > maxm)
		maxm = abs(mark);
	if (abs(space) > maxs)
		maxs = abs(space);
	wsamp++;

	mmult = maxm / (tx_width / 2 - 2);
	smult = maxs / (tx_height / 2 - 2);
	madd = tx_width / 2;
	sadd = tx_height / 2;
	if (wsamp - phaseadj == nsamp) {
		werase(tx);
		for (wsamp = 0; wsamp < nsamp; wsamp++) {
			y = buf[wsamp*2+1] / smult + sadd;
			x = buf[wsamp*2] / mmult + madd;
			ch = mvwinch(tx, y, x);
			switch (ch & A_CHARTEXT) {
				case ' ':
					och = '.';
					break;
				case '.':
					och = '+';
					break;
				case ',':
					och = '-';
					break;
				case '-':
					och = '+';
					break;
				case '+':
					och = '#';
					break;
				case '#':
					och = '#';
					break;
				case ERR & A_CHARTEXT:
					printf_errno("no char %02x (%d) at %d %d %d %lf %lf", ch, ch, y, x, tx_width, maxm, buf[wsamp*2]);
				default:
					printf_errno("no char %02x (%d) at %d %d", ch, ch, y, x);
			}
			mvwaddch(tx, buf[wsamp*2+1] / smult + sadd, buf[wsamp*2] / mmult + madd, och);
		}
		wmove(tx, 0, 0);
		wrefresh(tx);
		wsamp = 0;
	}
}

static void
setup_audio(void)
{
	int i;
	int dsp_buflen;
	int hfs_buflen;

	dsp = open(dsp_name, O_RDONLY);
	if (dsp == -1)
		printf_errno("unable to open sound device");
	i = AFMT_S16_NE;
	if (ioctl(dsp, SNDCTL_DSP_SETFMT, &i) == -1)
		printf_errno("setting format");
	if (i != AFMT_S16_NE)
		printf_errno("16-bit native endian audio not supported");
	if (ioctl(dsp, SNDCTL_DSP_CHANNELS, &dsp_channels) == -1)
		printf_errno("setting mono");
	if (ioctl(dsp, SNDCTL_DSP_SPEED, &dsp_rate) == -1)
		printf_errno("setting sample rate");
	phase_rate = 1/((double)dsp_rate/((double)baud_numerator / baud_denominator));
	dsp_buflen = (int)((double)dsp_rate / ((double)baud_numerator / baud_denominator)) + 1;
	dsp_buf = malloc(sizeof(dsp_buf[0]) * dsp_buflen);
	if (dsp_buf == NULL)
		printf_errno("allocating dsp buffer");
	dsp_bufmax = dsp_buflen - 1;
	hfs_buflen = ((double)dsp_rate/((double)baud_numerator / baud_denominator)) * 7.5 + 1;
	hfs_buf = malloc(hfs_buflen*sizeof(double));
	if (hfs_buf == NULL)
		printf_errno("allocating dsp buffer");
	hfs_bufmax = hfs_buflen - 1;
}

// https://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
static void
create_filters(void)
{
	/* TODO: Look into "Matched Filters"... all the rage */
	calc_bpf_coef(mark_freq, bp_filter_q, mbpfilt);
	mbpbuf[0] = mbpbuf[1] = mbpbuf[2] = mbpbuf[3] = 0.0;
	calc_bpf_coef(space_freq, bp_filter_q, sbpfilt);
	sbpbuf[0] = sbpbuf[1] = sbpbuf[2] = sbpbuf[3] = 0.0;

	/*
	 * TODO: Do we need to get the envelopes separately, or just
	 * take the envelope of the differences?
	 */
	calc_lpf_coef(((double)baud_numerator / baud_denominator)*1.1, lp_filter_q1, mlpfilt);
	mlpbuf[0] = mlpbuf[1] = mlpbuf[2] = mlpbuf[3] = 0.0;
	calc_lpf_coef(((double)baud_numerator / baud_denominator)*1.1, lp_filter_q1, slpfilt);
	slpbuf[0] = slpbuf[1] = slpbuf[2] = slpbuf[3] = 0.0;

	/*
	 * These are here to fix the phasing for the crossed bananas
	 * display.  The centre frequencies aren't calculated, they were
	 * selected via trial and error, so likely don't work for other
	 * mark/space frequencies.
	 * 
	 * TODO: Figure out how to calculate phase in biquad IIR filters.
	 */
	calc_apf_coef(mark_freq / 1.75, 1, mapfilt);
	mapbuf[0] = mapbuf[1] = mapbuf[2] = mapbuf[3] = 0.0;
	calc_apf_coef(space_freq * 1.75, 1, sapfilt);
	sapbuf[0] = sapbuf[1] = sapbuf[2] = sapbuf[3] = 0.0;
}

static void
calc_lpf_coef(double f0, double q, double *filt)
{
	double w0, cw0, sw0, a[5], b[5], alpha;

	w0 = 2.0 * M_PI * (f0 / dsp_rate);
	cw0 = cos(w0);
	sw0 = sin(w0);

	alpha = sw0 / (2.0 * q);
	b[0] = (1.0-cw0)/2.0;
	b[1] = 1.0 - cw0;
	b[2] = (1.0-cw0)/2.0;
	a[0] = 1.0 + alpha;
	a[1] = -2.0 * cw0;
	a[2] = 1.0 - alpha;
	filt[0] = b[0]/a[0];
	filt[1] = b[1]/a[0];
	filt[2] = b[2]/a[0];
	filt[3] = a[1]/a[0];
	filt[4] = a[2]/a[0];
}

static void
calc_bpf_coef(double f0, double q, double *filt)
{
	double w0, cw0, sw0, a[5], b[5], alpha;

	w0 = 2.0 * M_PI * (f0 / dsp_rate);
	cw0 = cos(w0);
	sw0 = sin(w0);
	alpha = sw0 / (2.0 * q);

	//b[0] = q * alpha;
	b[0] = alpha;
	b[1] = 0.0;
	b[2] = -(b[0]);
	a[0] = 1.0 + alpha;
	a[1] = -2.0 * cw0;
	a[2] = 1.0 - alpha;
	filt[0] = b[0]/a[0];
	filt[1] = b[1]/a[0];
	filt[2] = b[2]/a[0];
	filt[3] = a[1]/a[0];
	filt[4] = a[2]/a[0];
}

static void
calc_apf_coef(double f0, double q, double *filt)
{
	double w0, cw0, sw0, a[5], b[5], alpha;

	w0 = 2.0 * M_PI * (f0 / dsp_rate);
	cw0 = cos(w0);
	sw0 = sin(w0);
	alpha = sw0 / (2.0 * q);

	//b[0] = q * alpha;
	b[0] = 1 - alpha;
	b[1] = -2 * cw0;
	b[2] = 1 + alpha;
	a[0] = 1.0 + alpha;
	a[1] = -2.0 * cw0;
	a[2] = 1.0 - alpha;
	filt[0] = b[0]/a[0];
	filt[1] = b[1]/a[0];
	filt[2] = b[2]/a[0];
	filt[3] = a[1]/a[0];
	filt[4] = a[2]/a[0];
}

static double
bq_filter(double value, double *filt, double *buf)
{
	double y;

	y = (filt[0] * value) + (filt[1] * buf[0]) + (filt[2] * buf[1]) - (filt[3] * buf[2]) - (filt[4] * buf[3]);
	buf[1] = buf[0];
	buf[0] = value;
	buf[3] = buf[2];
	buf[2] = y;
	return y;
}
