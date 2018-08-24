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
#include <sys/types.h>

#include <fcntl.h>
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>

#include "bsdtty.h"
#include "ui.h"

static int fsk_tty = -1;
static pthread_mutex_t fsk_mutex = PTHREAD_MUTEX_INITIALIZER;
#define FSK_LOCK() pthread_mutex_lock(&fsk_mutex);
#define FSK_UNLOCK() pthread_mutex_unlock(&fsk_mutex);

static void end_fsk_thread(void);
static void fsk_toggle_reverse(void);
static void end_fsk_tx(void);
static void send_fsk_preamble(void);
static void send_fsk_char(char ch);
static void setup_fsk(void);
static void diddle_fsk(void);

static void
fsk_toggle_reverse(void)
{
	// FSK can't be reversed.
}

static void
end_fsk_tx(void)
{
	useconds_t sl;

	FSK_LOCK();
	ioctl(fsk_tty, TIOCDRAIN);
	// Space still gets cut off... wait one char
	SETTING_RLOCK();
	sl = ((1/((double)settings.baud_numerator / settings.baud_denominator))*7.5)*1000000;
	SETTING_UNLOCK();
	usleep(sl);
	FSK_UNLOCK();
}

static void
send_fsk_preamble(void)
{
	useconds_t sl;
	
	/* Hold it in mark for 1 byte time. */
	FSK_LOCK();
	SETTING_RLOCK();
	sl = ((1/((double)settings.baud_numerator / settings.baud_denominator))*7.5)*1000000;
	SETTING_UNLOCK();
	usleep(sl);
	FSK_UNLOCK();
}

static void
send_fsk_char(char ch)
{
	FSK_LOCK();
	if (write(fsk_tty, &ch, 1) != 1)
		printf_errno("error sending FIGS/LTRS");
	FSK_UNLOCK();
}

static void
setup_fsk(void)
{
	struct termios t;
	int state = TIOCM_DTR | TIOCM_RTS;
#ifdef TIOCSFBAUD
	struct baud_fraction bf;
#endif

	FSK_LOCK();
	// Set up the UART
	if (fsk_tty != -1)
		close(fsk_tty);
	SETTING_RLOCK();
	fsk_tty = open(settings.tty_name, O_RDWR|O_DIRECT|O_NONBLOCK);
	if (fsk_tty == -1)
		printf_errno("unable to open %s");

	/*
	 * In case stty wasn't used on the init device, turn off DTR and
	 * CTS hopefully before anyone notices
	 */
	if (ioctl(fsk_tty, TIOCMBIC, &state) != 0)
		printf_errno("unable clear RTS/DTR on '%s'", settings.tty_name);

	if (tcgetattr(fsk_tty, &t) == -1)
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

	if (tcsetattr(fsk_tty, TCSADRAIN, &t) == -1)
		printf_errno("unable to set attributes");

	if (tcgetattr(fsk_tty, &t) == -1)
		printf_errno("unable to read term caps");

	if (ioctl(fsk_tty, TIOCMBIC, &state) != 0)
		printf_errno("unable clear RTS/DTR");
#ifdef TIOCSFBAUD
	bf.bf_numerator = settings.baud_numerator;
	bf.bf_denominator = settings.baud_denominator;
	ioctl(fsk_tty, TIOCSFBAUD, &bf);
	ioctl(fsk_tty, TIOCGFBAUD, &bf);
#endif
	SETTING_UNLOCK();
	FSK_UNLOCK();
}

static void
diddle_fsk(void)
{
	// fsk can't diddle.
}

static void
end_fsk_thread(void)
{
	// There is no FSK thread to end.
}

static void
flush_fsk(void)
{
	int wh = FWRITE;

	ioctl(fsk_tty, TIOCFLUSH, &wh);
}

struct send_fsk_api fsk_api = {
	.toggle_reverse = fsk_toggle_reverse,
	.end_tx = end_fsk_tx,
	.send_preamble = send_fsk_preamble,
	.send_char = send_fsk_char,
	.setup = setup_fsk,
	.diddle = diddle_fsk,
	.end_fsk = end_fsk_thread,
	.flush = flush_fsk
};
