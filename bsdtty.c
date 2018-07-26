#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

static struct termios orig_t;
static bool israw = false;
static int tty = -1;

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
	struct termios t;
	char *err;

	// TODO: Parse a command-line...
	if (argc != 2)
		usage(argv[0]);

	tty_name = argv[1];

	setup_tty();

	atexit(done);

	// Now set up the input tty
	if (isatty(STDIN_FILENO)) {
		if (tcgetattr(STDIN_FILENO, &t) == -1)
			printf_errno("unable to read stdin term caps");
		memcpy(&orig_t, &t, sizeof(t));
		cfmakeraw(&t);
		if (tcsetattr(STDIN_FILENO, TCSADRAIN, &t) == -1)
			printf_errno("unable to set stdin attributes");
		israw = true;
	}

	input_loop();
	return 0;
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
	printf("Set baudrate at %f\n", ((double)bf.bf_numerator/bf.bf_denominator));
}

static void
printf_errno(const char *format, ...)
{
	int eno = errno;
	char *msg = NULL;
	char *emsg = NULL;
	va_list ap;

	va_start(ap, format);
	if (vasprintf(&msg, format, ap) < 0)
		msg = NULL;
	va_end(ap);
	printf("%s (%s)%s", strerror(eno), msg ? msg : "", israw ? "\r\n" : "\n");
	exit(EXIT_FAILURE);
}

static void
usage(const char *cmd)
{
	printf("Usage: %s /path/to/tty\n", cmd);
	exit(EXIT_FAILURE);
}

static char
asc2baudot(char asc, bool figs)
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
	fd_set rs;
	bool figs = false;
	char ch;
	char bch;
	const char fstr[] = "\x1f\x1b"; // LTRS, FIGS
	int state;
	struct timeval tv;
	bool rts = false;
	bool dtr = false;

	while (1) {
		FD_ZERO(&rs);
		FD_SET(STDIN_FILENO, &rs);
		switch (select(STDIN_FILENO + 1, &rs, NULL, NULL, NULL)) {
			case 1:
				if (read(STDIN_FILENO, &ch, 1) != 1)
					exit(EXIT_SUCCESS);
				if (ch == 3)
					exit(EXIT_SUCCESS);
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
					if (rts && tty == -1)
						setup_tty();
					if (ioctl(tty, rts ? TIOCMBIS : TIOCMBIC, &state) != 0) {
						printf_errno("setting RTS bit");
						continue;
					}
					if (rts) {
						figs = false;
						write(tty, "\x1f\x1f\x1f\x08\x02", 5);
						printf("\r\n------- Start of transmission -------\r\n");
					}
					else
						printf("\r\n-------- End of transmission --------\r\n");
				}
				if (bch == 0)
					bch = 0x1f;	// Deedle...
				// Send FIGS/LTRS as needed
				if (bch) {
					if ((!!(bch & 0x20)) != figs) {
						figs = !!(bch & 0x20);
						if (write(tty, &fstr[figs], 1) != 1)
							printf_errno("error sending FIGS/LTRS");
					}
					putchar(b2a[bch]);
					if (b2a[bch] == ' ')
						figs = false;	// USOS
					bch &= 0x1f;
					if (write(tty, &bch, 1) != 1)
						printf_errno("error sending char 0x%02x", bch);
					if (b2a[bch] == '\r') {
						putchar('\n');
						if (write(tty, "\x02", 1) != 1)
							printf_errno("error sending linefeed");
					}
					fflush(stdout);
				}
				break;
			default:
				exit(EXIT_SUCCESS);
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
	tcsetattr(STDIN_FILENO, TCSADRAIN, &orig_t);
	exit(EXIT_SUCCESS);
}
