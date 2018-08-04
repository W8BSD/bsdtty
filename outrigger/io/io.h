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

#ifndef IO_H
#define IO_H

#include <stdbool.h>
#include <stddef.h>

#include <iniparser.h>
#include <threads.h>
#include <semaphores.h>
#include <mutexes.h>

enum io_handle_type {
	IO_H_FIRST,
	IO_H_UNKNOWN = IO_H_FIRST,
	IO_H_SERIAL,
	IO_H_LAST = IO_H_SERIAL
};

struct io_response {
	size_t		len;
	char		msg[];
};

typedef struct io_response *(*io_read_callback)(void *);
typedef void (*io_async_callback)(void *, struct io_response *);

struct io_handle {
	enum io_handle_type	type;
	void				*cbdata;
	io_read_callback	read_cb;
	io_async_callback	async_cb;
	union {
		struct io_serial_handle	*serial;
	} handle;
	bool				terminate;			// Terminate the read thread
	semaphore_t			response_semaphore;	// Posted when a message is received and sync_pending is true
	semaphore_t			ack_semaphore;		// Posted when the thing which waited on response_semaphore
													// has used the response (ie: read next)
	mutex_t				sync_lock;			// Held by something waiting for a specific response
	mutex_t				lock;				// Held when reading/writing shared data
	thread_t			read_thread;		// The read thread
	bool				sync_pending;		// True if there is a thread waiting on response_semaphore
	struct io_response	*response;			// Set before response_semaphore is posted.
	size_t				response_len;
};

struct io_handle *io_start(enum io_handle_type htype, void *handle, io_read_callback rcb, io_async_callback acb, void *cbdata);
struct io_handle *io_start_from_dictionary(dictionary *d, const char *section, enum io_handle_type htype, io_read_callback rcb, io_async_callback acb, void *cbdata);
int io_end(struct io_handle *hdl);
struct io_response *io_get_response(struct io_handle *hdl, const char *match, size_t matchlen, size_t matchpos);
int io_wait_write(struct io_handle *hdl, unsigned timeout);
int io_write(struct io_handle *hdl, const void *buf, size_t nbytes, unsigned timeout);
int io_wait_read(struct io_handle *hdl, unsigned timeout);
int io_read(struct io_handle *hdl, void *buf, size_t nbytes, unsigned timeout);
int io_pending(struct io_handle *hdl);

#endif
