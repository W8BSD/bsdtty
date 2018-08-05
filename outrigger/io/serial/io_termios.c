/* Copyright (c) 2014 OpenHam
 * Developers:
 * Stephen Hurd (K6BSD/VE5BSD) <shurd@FreeBSD.org>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice, developer list, and this permission notice shall
 * be included in all copies or substantial portions of the Software. If you meet
 * us some day, and you think this stuff is worth it, you can buy us a beer in
 * return
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#ifdef WITH_TERMIOS

#include <datetime.h>

#include "serial.h"
#include "io_termios.h"

#define SUPPORTED_SPEED(x) \
	if (speed <= (x)) \
		return B##x

speed_t rate_to_macro(unsigned long speed)
{
	// Standard values
	SUPPORTED_SPEED(0);
	SUPPORTED_SPEED(50);
	SUPPORTED_SPEED(75);
	SUPPORTED_SPEED(110);
	SUPPORTED_SPEED(134);
	SUPPORTED_SPEED(150);
	SUPPORTED_SPEED(200);
	SUPPORTED_SPEED(300);
	SUPPORTED_SPEED(600);
	SUPPORTED_SPEED(1200);
	SUPPORTED_SPEED(1800);
	SUPPORTED_SPEED(2400);
	SUPPORTED_SPEED(4800);
	SUPPORTED_SPEED(9600);
	SUPPORTED_SPEED(19200);
	SUPPORTED_SPEED(38400);

	// Non-POSIX
#ifdef B57600
	SUPPORTED_SPEED(57600);
#endif
#ifdef B115200
	SUPPORTED_SPEED(115200);
#endif
#ifdef B230400
	SUPPORTED_SPEED(230400);
#endif
#ifdef B460800
	SUPPORTED_SPEED(460800);
#endif
#ifdef B500000
	SUPPORTED_SPEED(500000);
#endif
#ifdef B576000
	SUPPORTED_SPEED(576000);
#endif
#ifdef B921600
	SUPPORTED_SPEED(921600);
#endif
#ifdef B1000000
	SUPPORTED_SPEED(1000000);
#endif
#ifdef B1152000
	SUPPORTED_SPEED(1152000);
#endif
#ifdef B1500000
	SUPPORTED_SPEED(1500000);
#endif
#ifdef B2000000
	SUPPORTED_SPEED(2000000);
#endif
#ifdef B2500000
	SUPPORTED_SPEED(2500000);
#endif
#ifdef B3000000
	SUPPORTED_SPEED(3000000);
#endif
#ifdef B3500000
	SUPPORTED_SPEED(3500000);
#endif
#ifdef B4000000
	SUPPORTED_SPEED(4000000);
#endif
	return B0;
}
#undef SUPPORTED_SPEED
#define SUPPORTED_SPEED(x) \
	if (speed == B##x) \
		return x;

unsigned long macro_to_rate(speed_t speed)
{
	// Standard values
	SUPPORTED_SPEED(0);
	SUPPORTED_SPEED(50);
	SUPPORTED_SPEED(75);
	SUPPORTED_SPEED(110);
	SUPPORTED_SPEED(134);
	SUPPORTED_SPEED(150);
	SUPPORTED_SPEED(200);
	SUPPORTED_SPEED(300);
	SUPPORTED_SPEED(600);
	SUPPORTED_SPEED(1200);
	SUPPORTED_SPEED(1800);
	SUPPORTED_SPEED(2400);
	SUPPORTED_SPEED(4800);
	SUPPORTED_SPEED(9600);
	SUPPORTED_SPEED(19200);
	SUPPORTED_SPEED(38400);

	// Non-POSIX
#ifdef B57600
	SUPPORTED_SPEED(57600);
#endif
#ifdef B115200
	SUPPORTED_SPEED(115200);
#endif
#ifdef B230400
	SUPPORTED_SPEED(230400);
#endif
#ifdef B460800
	SUPPORTED_SPEED(460800);
#endif
#ifdef B500000
	SUPPORTED_SPEED(500000);
#endif
#ifdef B576000
	SUPPORTED_SPEED(576000);
#endif
#ifdef B921600
	SUPPORTED_SPEED(921600);
#endif
#ifdef B1000000
	SUPPORTED_SPEED(1000000);
#endif
#ifdef B1152000
	SUPPORTED_SPEED(1152000);
#endif
#ifdef B1500000
	SUPPORTED_SPEED(1500000);
#endif
#ifdef B2000000
	SUPPORTED_SPEED(2000000);
#endif
#ifdef B2500000
	SUPPORTED_SPEED(2500000);
#endif
#ifdef B3000000
	SUPPORTED_SPEED(3000000);
#endif
#ifdef B3500000
	SUPPORTED_SPEED(3500000);
#endif
#ifdef B4000000
	SUPPORTED_SPEED(4000000);
#endif
	return 0;
}
#undef SUPPORTED_SPEED

struct io_serial_handle *serial_termios_open(const char *path,
		unsigned speed, enum serial_data_word_length wlen, enum serial_stop_bits sbits,
		enum serial_parity parity, enum serial_flow flow, enum serial_break_enable brk)
{
	struct termios				tio;
	struct serial_termios_impl	*hdl;
	struct io_serial_handle 	*ret;

	if (path == NULL)
		return NULL;

	ret = (struct io_serial_handle *)malloc(sizeof(struct io_serial_handle));

	if (ret == NULL)
		return ret;
	ret->handle = malloc(sizeof(struct serial_termios_impl));
	if (ret->handle == NULL)
		goto fail;
	hdl = ret->handle;

	/* Verify args */
	ret->type = SERIAL_H_TERMIOS;
	if (wlen < SERIAL_DWL_FIRST || wlen > SERIAL_DWL_LAST)
		goto fail;
	ret->word = wlen;
	if (sbits < SERIAL_SB_FIRST || sbits > SERIAL_SB_LAST)
		goto fail;
	ret->stop = sbits;
	if (parity < SERIAL_P_FIRST || parity > SERIAL_P_LAST)
		goto fail;
	ret->parity = parity;
	if (flow < SERIAL_F_FIRST || flow > SERIAL_F_LAST)
		goto fail;
	ret->flow = flow;
	if (brk < SERIAL_BREAK_FIRST || brk > SERIAL_BREAK_LAST)
		goto fail;
	ret->brk = brk;

	/* Open port */
	hdl->fd = open(path, O_RDWR|O_NONBLOCK);
	if (hdl->fd == -1)
		goto fail;

	/* Set up the tty */
	if (tcgetattr(hdl->fd, &tio) != 0)
		goto fail;
	cfmakeraw(&tio);
	if (cfsetospeed(&tio, rate_to_macro(speed)) != 0)
		goto fail;
	if (cfsetispeed(&tio, rate_to_macro(speed)) != 0)
		goto fail;
	tio.c_iflag = IGNBRK|IGNPAR;
	tio.c_oflag = 0;
	tio.c_cflag = CREAD|CLOCAL;
	switch (wlen) {
		case SERIAL_DWL_5:
			tio.c_cflag |= CS5;
			break;
		case SERIAL_DWL_6:
			tio.c_cflag |= CS6;
			break;
		case SERIAL_DWL_7:
			tio.c_cflag |= CS7;
			break;
		case SERIAL_DWL_8:
			tio.c_cflag |= CS8;
			break;
		default:
			goto fail;
	}
	switch (sbits) {
		case SERIAL_SB_1:
			break;
		case SERIAL_SB_2:
		case SERIAL_SB_1_5:
			tio.c_cflag |= CSTOPB;
			break;
		default:
			goto fail;
	}
	switch (parity) {
		case SERIAL_P_NONE:
			break;
		case SERIAL_P_ODD:
			tio.c_cflag |= PARENB|PARODD;
			break;
		case SERIAL_P_EVEN:
			tio.c_cflag |= PARENB;
			break;
		case SERIAL_P_HIGH:
		case SERIAL_P_LOW:
		default:
			goto fail;
	}
	switch (flow) {
		case SERIAL_F_CTS:
			tio.c_cflag |= CCTS_OFLOW|CRTS_IFLOW;
			break;
		case SERIAL_F_NONE:
			break;
		default:
			goto fail;
	}
	switch (brk) {
		case SERIAL_BREAK_DISABLED:
			tio.c_iflag |= IGNBRK;
			break;
		case SERIAL_BREAK_ENABLED:
			break;
		default:
			goto fail;
	}
	if (tcsetattr(hdl->fd, TCSANOW, &tio) != 0)
		goto fail;

	/* Verify settings */
	if (tcgetattr(hdl->fd, &tio) != 0)
		goto fail;
	if (macro_to_rate(cfgetospeed(&tio)) != speed)
		goto fail;
	if (macro_to_rate(cfgetispeed(&tio)) != speed)
		goto fail;
	switch (tio.c_cflag & CSIZE) {
		case CS5:
			if (wlen != SERIAL_DWL_5)
				goto fail;
			break;
		case CS6:
			if (wlen != SERIAL_DWL_6)
				goto fail;
			break;
		case CS7:
			if (wlen != SERIAL_DWL_7)
				goto fail;
			break;
		case CS8:
			if (wlen != SERIAL_DWL_8)
				goto fail;
			break;
		default:
			goto fail;
	}
	switch (tio.c_cflag & CSTOPB) {
		case 0:
			if (sbits != SERIAL_SB_1)
				goto fail;
			break;
		case CSTOPB:
			if (sbits != SERIAL_SB_2 && sbits != SERIAL_SB_1_5)
				goto fail;
			break;
		default:
			goto fail;
	}
	switch (tio.c_cflag & (PARENB|PARODD)) {
		case 0:
		case PARODD:
			if (parity != SERIAL_P_NONE)
				goto fail;
			break;
		case PARENB:
			if (parity != SERIAL_P_EVEN)
				goto fail;
			break;
		case (PARENB|PARODD):
			if (parity != SERIAL_P_ODD)
				goto fail;
			break;
		default:
			goto fail;
	}
	switch (tio.c_cflag & (CCTS_OFLOW|CRTS_IFLOW)) {
		case 0:
			if (flow != SERIAL_F_NONE)
				goto fail;
			break;
		default:
			if (flow != SERIAL_F_CTS)
				goto fail;
			break;
	}
	switch (tio.c_iflag & (IGNBRK|BRKINT)) {
		case IGNBRK:
		case (IGNBRK|BRKINT):
			if (brk != SERIAL_BREAK_DISABLED)
				goto fail;
			break;
		case 0:
			if (brk != SERIAL_BREAK_ENABLED)
				goto fail;
			break;
		default:
			goto fail;
	}
	if (flow == SERIAL_F_CTS)
		serial_termios_rts(ret, true);
	return ret;

fail:
	if (ret->handle != NULL)
		free(ret->handle);
	free(ret);
	return NULL;
}

int serial_termios_close(struct io_serial_handle *hdl)
{
	int ret;
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;

	ret = close(thdl->fd);
	free(thdl);
}

int serial_termios_cts(struct io_serial_handle *hdl, bool *val)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	int state;

	if (val == NULL)
		return EINVAL;

	if (ioctl(thdl->fd, TIOCMGET, &state) == -1)
		return errno;
	*val = (state & TIOCM_CTS)?true:false;
	return 0;
}

int serial_termios_dsr(struct io_serial_handle *hdl, bool *val)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	int state;

	if (val == NULL)
		return EINVAL;

	if (ioctl(thdl->fd, TIOCMGET, &state) == -1)
		return errno;
	*val = (state & TIOCM_DSR)?true:false;
	return 0;
}

int serial_termios_cd(struct io_serial_handle *hdl, bool *val)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	int state;

	if (val == NULL)
		return EINVAL;

	if (ioctl(thdl->fd, TIOCMGET, &state) == -1)
		return errno;
	*val = (state & TIOCM_CD)?true:false;
	return 0;
}

int serial_termios_dtr(struct io_serial_handle *hdl, bool val)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	unsigned long				action;
	int							state = TIOCM_DTR;

	action = val?TIOCMBIS:TIOCMBIC;
	if (ioctl(thdl->fd, action, &state) == -1)
		return errno;
	return 0;
}

int serial_termios_rts(struct io_serial_handle *hdl, bool val)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	unsigned long				action;
	int							state = TIOCM_RTS;

	action = val?TIOCMBIS:TIOCMBIC;
	if (ioctl(thdl->fd, action, &state) == -1)
		return errno;
	return 0;
}

static int serial_termios_broken_cts(struct io_serial_handle *hdl, unsigned timeout)
{
	bool			cts=false;

	while(timeout--) {
		if (serial_termios_cts(hdl, &cts) != 0)
			return -1;
		if (cts)
			return 1;
		ms_sleep(1);
	}
	return 0;
}

static int serial_termios_select(struct io_serial_handle *hdl, bool wr, unsigned timeout)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	fd_set			fds;
	struct timeval	tv = {};
	bool			cts;

	FD_ZERO(&fds);
	FD_SET(thdl->fd, &fds);
	tv.tv_sec = timeout/1000;
	tv.tv_usec = (timeout % 1000)*1000;
	switch (select(thdl->fd+1, wr?NULL:&fds, wr?&fds:NULL, NULL, &tv)) {
		case 0:
			return 1;
		case -1:
			return -1;
		default:
			if (FD_ISSET(thdl->fd, &fds)) {
				/* Check for broken CTS flow control */
				if (wr && hdl->flow == SERIAL_F_CTS) {
					if (serial_termios_cts(hdl, &cts)==0) {
						if (!cts)
							return serial_termios_broken_cts(hdl, timeout);
					}
				}
				return 1;
			}
			return 0;
	}
}

int serial_termios_wait_write(struct io_serial_handle *hdl, unsigned timeout)
{
	return serial_termios_select(hdl, true, timeout);
}

int serial_termios_wait_read(struct io_serial_handle *hdl, unsigned timeout)
{
	return serial_termios_select(hdl, false, timeout);
}

int serial_termios_write(struct io_serial_handle *hdl, const void *buf, size_t nbytes, unsigned timeout)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	size_t	written;
	ssize_t	tw;

	for(written=0; written < nbytes; written += tw) {
		if (serial_termios_wait_write(hdl, timeout) != 1)
			return -1;
		tw = write(thdl->fd, buf+written, nbytes-written);
		if (tw == -1)
			return -1;
	}
	return written;
}

int serial_termios_read(struct io_serial_handle *hdl, void *buf, size_t nbytes, unsigned timeout)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	size_t	rd;
	ssize_t	tr;

	for(rd=0; rd < nbytes; rd += tr) {
		if (serial_termios_wait_read(hdl, timeout) != 1)
			return -1;
		tr = read(thdl->fd, buf+rd, nbytes-rd);
		if (tr == -1)
			return -1;
	}
	return rd;
}

int serial_termios_pending(struct io_serial_handle *hdl)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;
	int avail = 0;
	
	if (ioctl(thdl->fd, FIONREAD, &avail) == -1) {
		fd_set			rfd;
		struct timeval	tv = {};

		FD_ZERO(&rfd);
		FD_SET(thdl->fd, &rfd);
		switch (select(thdl->fd+1, &rfd, NULL, NULL, &tv)) {
			case 0:
				break;
			case -1:
				return -1;
			default:
				if (FD_ISSET(thdl->fd, &rfd))
					avail = 1;
				break;
		}
	}
	return avail;
}

int serial_termios_drain(struct io_serial_handle *hdl)
{
	struct serial_termios_impl	*thdl = (struct serial_termios_impl *)hdl->handle;

	return ioctl(thdl->fd, TIOCDRAIN);
}

#endif
