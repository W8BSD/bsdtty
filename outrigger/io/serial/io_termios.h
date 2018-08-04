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

#ifndef TERMIOS_H
#define TERMIOS_H

#ifdef WITH_TERMIOS
#include "serial.h"

struct serial_termios_impl {
	int		fd;
};

struct io_serial_handle *serial_termios_open(const char *path,
		unsigned speed, enum serial_data_word_length wlen, enum serial_stop_bits sbits,
		enum serial_parity parity, enum serial_flow flow, enum serial_break_enable brk);
int serial_termios_close(struct io_serial_handle *hdl);
int serial_termios_cts(struct io_serial_handle *hdl, bool *cts);
int serial_termios_dsr(struct io_serial_handle *hdl, bool *dsr);
int serial_termios_cd(struct io_serial_handle *hdl, bool *cd);
int serial_termios_dtr(struct io_serial_handle *hdl, bool dtr);
int serial_termios_rts(struct io_serial_handle *hdl, bool rts);
int serial_termios_wait_write(struct io_serial_handle *hdl, unsigned timeout);
int serial_termios_wait_read(struct io_serial_handle *hdl, unsigned timeout);
int serial_termios_write(struct io_serial_handle *hdl, const void *buf, size_t nbytes, unsigned timeout);
int serial_termios_read(struct io_serial_handle *hdl, void *buf, size_t nbytes, unsigned timeout);
int serial_termios_pending(struct io_serial_handle *hdl);
int serial_termios_drain(struct io_serial_handle *hdl);

#endif

#endif
