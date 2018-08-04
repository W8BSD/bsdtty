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

#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stddef.h>

enum serial_data_word_length {
	SERIAL_DWL_FIRST,
	SERIAL_DWL_5 = SERIAL_DWL_FIRST,
	SERIAL_DWL_6,
	SERIAL_DWL_7,
	SERIAL_DWL_8,
	SERIAL_DWL_LAST = SERIAL_DWL_8
};

enum serial_stop_bits {
	SERIAL_SB_FIRST,
	SERIAL_SB_1 = SERIAL_SB_FIRST,
	SERIAL_SB_1_5,
	SERIAL_SB_2,
	SERIAL_SB_LAST = SERIAL_SB_2
};

enum serial_parity {
	SERIAL_P_FIRST,
	SERIAL_P_NONE = SERIAL_P_FIRST,
	SERIAL_P_ODD,
	SERIAL_P_EVEN,
	SERIAL_P_HIGH,
	SERIAL_P_LOW,
	SERIAL_P_LAST = SERIAL_P_LOW
};

enum serial_break_enable {
	SERIAL_BREAK_FIRST,
	SERIAL_BREAK_DISABLED = SERIAL_BREAK_FIRST,
	SERIAL_BREAK_ENABLED,
	SERIAL_BREAK_LAST = SERIAL_BREAK_ENABLED
};

enum serial_handle_type {
	SERIAL_H_FIRST,
	SERIAL_H_UNSPECIFIED = SERIAL_H_FIRST,
	SERIAL_H_TERMIOS,
	SERIAL_H_WIN32,
	SERIAL_H_LAST = SERIAL_H_WIN32
};

enum serial_flow {
	SERIAL_F_FIRST,
	SERIAL_F_NONE = SERIAL_F_FIRST,
	SERIAL_F_CTS,
	SERIAL_F_LAST = SERIAL_F_CTS
};

struct io_serial_handle {
	enum serial_data_word_length	word;
	enum serial_stop_bits			stop;
	enum serial_parity				parity;
	enum serial_break_enable		brk;
	enum serial_handle_type			type;
	enum serial_flow				flow;
	unsigned						speed;
	void							*handle;
};

struct io_serial_handle *serial_open(enum serial_handle_type htype, const char *path,
		unsigned speed, enum serial_data_word_length wlen, enum serial_stop_bits sbits,
		enum serial_parity parity, enum serial_flow flow, enum serial_break_enable brk);
int serial_close(struct io_serial_handle *hdl);
int serial_cts(struct io_serial_handle *hdl, bool *cts);
int serial_dsr(struct io_serial_handle *hdl, bool *dsr);
int serial_cd(struct io_serial_handle *hdl, bool *cd);
int serial_dtr(struct io_serial_handle *hdl, bool dtr);
int serial_rts(struct io_serial_handle *hdl, bool rts);
int serial_wait_write(struct io_serial_handle *hdl, unsigned timeout);
int serial_wait_read(struct io_serial_handle *hdl, unsigned timeout);
int serial_write(struct io_serial_handle *hdl, const void *buf, size_t nbytes, unsigned timeout);
int serial_read(struct io_serial_handle *hdl, void *buf, size_t nbytes, unsigned timeout);
int serial_pending(struct io_serial_handle *hdl);
int serial_drain(struct io_serial_handle *hdl);

#endif
