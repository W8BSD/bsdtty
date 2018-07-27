#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
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

static int tty = -1;
WINDOW *status;
WINDOW *status_title;
WINDOW *rx;
WINDOW *rx_title;
WINDOW *tx;
WINDOW *tx_title;

char *tty_name = NULL;

const char b2a[] = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- \x07" "87"
		  "\r$4',!:("
		  "5\")2#601"
		  "9?&\x0e" "./;\x0f";

const char us2a[] = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- '" "87"
		  "\r$4\x07,!:("
		  "5\")2#601"
		  "9?&\x0e" "./;\x0f";

int main(int argc, char **argv)
{
	char *err;

	// TODO: Parse a command-line...
	if (argc != 2)
		usage(argv[0]);

	tty_name = argv[1];

	setup_tty();

	setlocale(LC_ALL, "");
	atexit(done);

	// Now set up curses
	setup_curses();

	// Finally, do the thing.
	input_loop();
	return EXIT_SUCCESS;
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
	if (cfsetspeed(&t, 45) == -1)
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
	bf.bf_numerator = 1000;
	bf.bf_denominator = 22;
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
	char *emsg = NULL;
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

#define HALF_CHAR_TIME	82500
#define CHAR_TIME	165000

static void
input_loop(void)
{
	fd_set rs;
	bool figs = false;
	int ch;
	char bch;
	const char fstr[] = "\x1f\x1b"; // LTRS, FIGS
	int state;
	struct timeval tv;
	bool rts = false;
	bool dtr = false;
	int outq;

	tv.tv_sec = 0;
	tv.tv_usec = HALF_CHAR_TIME;
	while (1) {
		wrefresh(tx);
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
						usleep(165000);
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
					switch (b2a[bch]) {
						case 0:
						case 0x0e:
						case 0x0f:
							break;
						default:
							waddch(tx, b2a[bch]);
					}
					if (b2a[bch] == ' ')
						figs = false;	// USOS
					bch &= 0x1f;
					if (write(tty, &bch, 1) != 1)
						printf_errno("error sending char 0x%02x", bch);
					// Expand CR to CRLF
					if (b2a[bch] == '\r') {
						putchar('\n');
						if (write(tty, "\x02", 1) != 1)
							printf_errno("error sending linefeed");
					}
				}
				break;
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
