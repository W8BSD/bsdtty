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
#include <curses.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "bsdtty.h"
#include "ui.h"

/* UI Stuff */
static WINDOW *status;
static WINDOW *status_title;
static WINDOW *rx;
static WINDOW *rx_title;
static WINDOW *tx;
static WINDOW *tx_title;
static int tx_width;
static int tx_height;

static void setup_windows(void);
static void w_printf(WINDOW *win, const char *format, ...);
static void do_endwin(void);

void
setup_curses(void)
{
	initscr();
	atexit(do_endwin);
	raw();		// cbreak() leaves SIGINT working
	noecho();
	nonl();
	keypad(stdscr, TRUE);

	setup_windows();
}

void
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
		nsamp = settings.dsp_rate/((((double)settings.baud_numerator / settings.baud_denominator))*7.5);
	if (buf == NULL) {
		buf = malloc(sizeof(*buf) * nsamp * 2 + 1);
		if (buf == NULL)
			printf_errno("alocating %d bytes", sizeof(*buf) * nsamp * 2 + 1);
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

void
mark_tx_extent(bool start)
{
	w_printf(tx, "\n------- %s of transmission -------\n", start ? "Start" : "End");
}

int
get_input(void)
{
	int ret;

	ret = wgetch(tx);
	if (ret == ERR)		// Map ERR to -1
		return -1;
	if (ret == KEY_BREAK)	// Map KEY_BREAK to ^C
		return 3;
	return ret;
}

void
write_tx(char ch)
{
	if (ch == '\r')
		waddch(tx, '\n');
	waddch(tx, ch);
	wrefresh(tx);
}

void
write_rx(char ch)
{
	waddch(rx, ch);
	wrefresh(rx);
}

bool
check_input(void)
{
	int ch;

	ch = wgetch(rx);
	if (ch != ERR) {
		ungetch(ch);
		return true;
	}
	return false;
}

void
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
do_endwin(void)
{
	endwin();
}
