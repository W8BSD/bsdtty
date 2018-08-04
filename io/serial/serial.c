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

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#include "serial.h"
#ifdef WITH_TERMIOS
#include "io_termios.h"
#endif

struct io_serial_handle *serial_open(enum serial_handle_type htype, const char *path,
		unsigned speed, enum serial_data_word_length wlen, enum serial_stop_bits sbits,
		enum serial_parity parity, enum serial_flow flow, enum serial_break_enable brk)
{
	struct io_serial_handle *ret = NULL;
	switch(htype) {
		case SERIAL_H_UNSPECIFIED:
			/* H_UNSPECIFIED keeps falling through until it works */
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			ret = serial_termios_open(path, speed, wlen, sbits, parity, flow, brk);
			if(ret != NULL || htype == SERIAL_H_TERMIOS)
				return ret;
			/* Fall-through */
#endif
		default:
			return ret;
	}
}

int serial_close(struct io_serial_handle *hdl)
{
	if (hdl == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_close(hdl);
#endif
		default:
			return EINVAL;
	}
}

int serial_cts(struct io_serial_handle *hdl, bool *val)
{
	if (hdl == NULL || val == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_cts(hdl, val);
#endif
		default:
			return EINVAL;
	}
}

int serial_dsr(struct io_serial_handle *hdl, bool *val)
{
	if (hdl == NULL || val == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_dsr(hdl, val);
#endif
		default:
			return EINVAL;
	}
}

int serial_cd(struct io_serial_handle *hdl, bool *val)
{
	if (hdl == NULL || val == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_cd(hdl, val);
#endif
		default:
			return EINVAL;
	}
}

int serial_dtr(struct io_serial_handle *hdl, bool val)
{
	if (hdl == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_dtr(hdl, val);
#endif
		default:
			return EINVAL;
	}
}

int serial_rts(struct io_serial_handle *hdl, bool val)
{
	if (hdl == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_rts(hdl, val);
#endif
		default:
			return EINVAL;
	}
}

int serial_wait_write(struct io_serial_handle *hdl, unsigned timeout)
{
	if (hdl == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_wait_write(hdl, timeout);
#endif
		default:
			return EINVAL;
	}
}

int serial_wait_read(struct io_serial_handle *hdl, unsigned timeout)
{
	if (hdl == NULL)
		return EINVAL;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_wait_read(hdl, timeout);
#endif
		default:
			return EINVAL;
	}
}

int serial_write(struct io_serial_handle *hdl, const void *buf, size_t nbytes, unsigned timeout)
{
	if (hdl == NULL || buf == NULL || nbytes == 0)
		return -1;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_write(hdl, buf, nbytes, timeout);
#endif
		default:
			return -1;
	}
}

int serial_read(struct io_serial_handle *hdl, void *buf, size_t nbytes, unsigned timeout)
{
	if (hdl == NULL || buf == NULL || nbytes == 0)
		return -1;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_read(hdl, buf, nbytes, timeout);
#endif
		default:
			return -1;
	}
}

int serial_pending(struct io_serial_handle *hdl)
{
	if (hdl == NULL)
		return -1;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_pending(hdl);
#endif
		default:
			return -1;
	}
}

int serial_drain(struct io_serial_handle *hdl)
{
	if (hdl == NULL)
		return -1;

	switch(hdl->type) {
#ifdef WITH_TERMIOS
		case SERIAL_H_TERMIOS:
			return serial_termios_drain(hdl);
#endif
		default:
			return -1;
	}
}
