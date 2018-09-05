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
#include <sys/soundcard.h>
#include <sys/types.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#ifdef __FreeBSD__
#include <pthread_np.h>
#endif
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "afsk_send.h"
#include "bsdtty.h"
#include "ui.h"

enum afsk_bit {
	AFSK_SPACE,
	AFSK_MARK,
	AFSK_STOP,
	AFSK_UNKNOWN
};

/* AFSK buffers */
/*
 * Each of these is half of a bit-time long
 * The ones to/from zero use a quarter sine envelope
 */
struct afsk_buf {
	size_t size;
	int16_t *buf;
};

/* AFSK queue */
struct afsk_queue {
	struct afsk_buf *buf;
	struct afsk_queue *next;
};
static struct afsk_queue *qhead;
static struct afsk_queue *qtail;
static pthread_mutex_t afsk_queue_lock;
#define AQ_LOCK()	assert(pthread_mutex_lock(&afsk_queue_lock) == 0)
#define AQ_UNLOCK()	assert(pthread_mutex_unlock(&afsk_queue_lock) == 0)

static struct afsk_buf zero_to_mark;
static struct afsk_buf zero_to_space;
static struct afsk_buf mark_to_zero;
static struct afsk_buf space_to_zero;
static struct afsk_buf mark_to_mark;
static struct afsk_buf space_to_space;
enum afsk_bit last_afsk_bit = AFSK_UNKNOWN;
static int dsp_afsk = -1;
static int dsp_afsk_channels = 1;
static int afsk_dsp_rate = 48000;
static pthread_mutex_t afsk_mutex = PTHREAD_MUTEX_INITIALIZER;
#define AFSK_LOCK() pthread_mutex_lock(&afsk_mutex);
#define AFSK_UNLOCK() pthread_mutex_unlock(&afsk_mutex);
static sem_t qsem;
static bool qsem_initialized;
static pthread_t afsk_threadid;

static void adjust_wave(struct afsk_buf *buf, double start_phase);
static void generate_afsk_samples(void);
static void generate_sine(double freq, struct afsk_buf *buf);
static void send_afsk_buf(struct afsk_buf *buf);
static void send_afsk_bit(enum afsk_bit bit);
static void * afsk_thread(void *arg);
static void flush_queue(void);

/*
 * Requires the setting lock to be held.
 */
static void
generate_sine(double freq, struct afsk_buf *buf)
{
	size_t i;
	double wavelen = afsk_dsp_rate / freq;
	size_t nsamp;

	nsamp = afsk_dsp_rate / ((double)settings.baud_numerator / settings.baud_denominator * 2) + 2;
	if (buf->buf)
		free(buf->buf);
	buf->buf = calloc(sizeof(buf->buf[0]) * nsamp, dsp_afsk_channels);
	if (buf->buf == NULL)
		printf_errno("allocating AFSK buffer");

	for (i = 0; i < nsamp; i++)
		buf->buf[i*dsp_afsk_channels] = sin((double)i / wavelen * (2.0 * M_PI)) * (INT16_MAX);

	for (i = nsamp - 4; i < nsamp; i++) {
		if ((buf->buf[i * dsp_afsk_channels] >= 0) && (buf->buf[(i-1) * dsp_afsk_channels] <= 0))
			break;
	}
	if (i == nsamp) {
		for (--i; i > 0; i--) {
			if ((buf->buf[i * dsp_afsk_channels] >= 0) && (buf->buf[(i-1) * dsp_afsk_channels] <= 0))
				break;
		}
	}

	buf->size = i;
}

static void
adjust_wave(struct afsk_buf *buf, double start_phase)
{
	size_t i;
	double phase_step = (M_PI) / buf->size;

	for (i = 0; i < buf->size; i++) {
		buf->buf[i * dsp_afsk_channels] *= (cos(start_phase) + 1) / 2;
		start_phase += phase_step;
	}
}

/*
 * Requires the settings lock to be held
 */
static void
generate_afsk_samples(void)
{
	generate_sine(settings.mark_freq, &zero_to_mark);
	generate_sine(settings.mark_freq, &mark_to_zero);
	generate_sine(settings.mark_freq, &mark_to_mark);
	generate_sine(settings.space_freq, &zero_to_space);
	generate_sine(settings.space_freq, &space_to_zero);
	generate_sine(settings.space_freq, &space_to_space);

	adjust_wave(&zero_to_mark, M_PI);
	adjust_wave(&mark_to_zero, 0.0);
	adjust_wave(&zero_to_space, M_PI);
	adjust_wave(&space_to_zero, 0.0);
}

static void
send_afsk_bit(enum afsk_bit bit)
{
	switch(bit) {
		case AFSK_MARK:
			switch(last_afsk_bit) {
				case AFSK_UNKNOWN:
					printf_errno("mark after unknown");
				case AFSK_SPACE:
					send_afsk_buf(&space_to_zero);
					send_afsk_buf(&zero_to_mark);
					break;
				case AFSK_MARK:
					send_afsk_buf(&mark_to_mark);
					send_afsk_buf(&mark_to_mark);
					break;
				case AFSK_STOP:
					printf_errno("mark after stop");
			}
			break;
		case AFSK_SPACE:
			switch(last_afsk_bit) {
				case AFSK_UNKNOWN:
					send_afsk_buf(&zero_to_space);
					break;
				case AFSK_SPACE:
					send_afsk_buf(&space_to_space);
					send_afsk_buf(&space_to_space);
					break;
				case AFSK_STOP:
				case AFSK_MARK:
					send_afsk_buf(&mark_to_zero);
					send_afsk_buf(&zero_to_space);
					break;
			}
			break;
		case AFSK_STOP:
			switch(last_afsk_bit) {
				case AFSK_UNKNOWN:
					send_afsk_buf(&zero_to_mark);
					send_afsk_buf(&mark_to_mark);
					send_afsk_buf(&mark_to_mark);
					break;
				case AFSK_SPACE:
					send_afsk_buf(&space_to_zero);
					send_afsk_buf(&zero_to_mark);
					send_afsk_buf(&mark_to_mark);
					break;
				case AFSK_MARK:
					send_afsk_buf(&mark_to_mark);
					send_afsk_buf(&mark_to_mark);
					send_afsk_buf(&mark_to_mark);
					break;
				case AFSK_STOP:
					send_afsk_buf(&mark_to_mark);
					send_afsk_buf(&mark_to_mark);
					send_afsk_buf(&mark_to_mark);
			}
			break;
		case AFSK_UNKNOWN:
			printf_errno("sending unknown bit");
			break;
	}
	last_afsk_bit = bit;
}

static void
write_afsk_buf(struct afsk_buf *buf)
{
	size_t sent = 0;
	int ret;

	while (sent < buf->size) {
		ret = write(dsp_afsk, buf->buf + sent, (buf->size - sent) * sizeof(buf->buf[0]));
		if (ret == -1)
			printf_errno("writing AFSK buffer");
		ret /= sizeof(buf->buf[0]);
		sent += ret;
	}
}

static void
send_afsk_buf(struct afsk_buf *buf)
{
	struct afsk_queue *new;

	new = malloc(sizeof(*new));
	if (new == NULL)
		printf_errno("allocating afsk queue entry");
	new->buf = buf;
	new->next = NULL;
	AQ_LOCK();
	if (qtail != NULL)
		qtail->next = new;
	qtail = new;
	if (qhead == NULL)
		qhead = new;
	AQ_UNLOCK();
	sem_post(&qsem);
}

static void
send_afsk_char(char ch)
{
	int i;

	AFSK_LOCK();
	send_afsk_bit(AFSK_SPACE);
	for (i = 0; i < 5; i++) {
		send_afsk_bit(ch & 1 ? AFSK_MARK : AFSK_SPACE);
		ch >>= 1;
	}
	send_afsk_bit(AFSK_STOP);
	AFSK_UNLOCK();
}

static void
end_afsk_tx(void)
{
	AFSK_LOCK();
	switch(last_afsk_bit) {
		case AFSK_UNKNOWN:
			printf_errno("ending after unknown bit");
			break;
		case AFSK_SPACE:
			printf_errno("ending after space");
			break;
		case AFSK_STOP:
		case AFSK_MARK:
			send_afsk_buf(&mark_to_zero);
			break;
	}
	if (ioctl(dsp_afsk, SNDCTL_DSP_SYNC, NULL) == -1)
		printf_errno("syncing AFSK playback");
	AFSK_UNLOCK();
}

static void
setup_afsk()
{
	int i;

	AFSK_LOCK();
	SETTING_RLOCK();
	generate_afsk_samples();
	if (dsp_afsk != -1)
		close(dsp_afsk);
	dsp_afsk = open(settings.dsp_name, O_WRONLY);
	if (dsp_afsk == -1)
		printf_errno("unable to open AFSK sound device");
	i = AFMT_S16_NE;
	if (ioctl(dsp_afsk, SNDCTL_DSP_SETFMT, &i) == -1)
		printf_errno("setting AFSK format");
	if (i != AFMT_S16_NE)
		printf_errno("16-bit native endian audio not supported");
	if (ioctl(dsp_afsk, SNDCTL_DSP_CHANNELS, &dsp_afsk_channels) == -1)
		printf_errno("setting afsk channels");
	if (ioctl(dsp_afsk, SNDCTL_DSP_SPEED, &afsk_dsp_rate) == -1)
		printf_errno("setting sample rate");
	SETTING_UNLOCK();
	if (qsem_initialized) {
		sem_destroy(&qsem);
		qsem_initialized = false;
	}
	if (sem_init(&qsem, 0, 0) == -1)
		printf_errno("initializing semaphore");
	qsem_initialized = true;
	if (pthread_create(&afsk_threadid, NULL, afsk_thread, NULL) != 0)
		printf_errno("Creating AFSK thread");
	AFSK_UNLOCK();
}

static void
afsk_toggle_reverse(void)
{
	int16_t *tbuf;

	AFSK_LOCK();
	/*
	 * TX Stuff, swap samples
	 */
	tbuf = zero_to_mark.buf;
	zero_to_mark.buf = zero_to_space.buf;
	zero_to_space.buf = tbuf;

	tbuf = mark_to_zero.buf;
	mark_to_zero.buf = space_to_zero.buf;
	space_to_zero.buf = tbuf;

	tbuf = mark_to_mark.buf;
	mark_to_mark.buf = space_to_space.buf;
	space_to_space.buf = tbuf;
	AFSK_UNLOCK();
}

static void
send_afsk_preamble(void)
{
	AFSK_LOCK();
	send_afsk_bit(AFSK_STOP);
	send_afsk_bit(AFSK_STOP);
	send_afsk_bit(AFSK_STOP);
	send_afsk_bit(AFSK_STOP);
	send_afsk_bit(AFSK_STOP);
	AFSK_UNLOCK();
}

static void
diddle_afsk(void)
{
	send_afsk_char(0x1f);
}

static void *
afsk_thread(void *arg)
{
	struct afsk_queue *ent;
	sigset_t blk;
	(void)arg;

	memset(&blk, 0xff, sizeof(blk));
	assert(pthread_sigmask(SIG_BLOCK, &blk, NULL) == 0);

#ifdef __linux__
	pthread_setname_np(pthread_self(), "AFSK");
#else
	pthread_set_name_np(pthread_self(), "AFSK");
#endif

	for (;;) {
		sem_wait(&qsem);
		AQ_LOCK();
		if (qhead == NULL) {
			AQ_UNLOCK();
			continue;
		}
		ent = qhead;
		qhead = qhead->next;
		if (qhead == NULL)
			qtail = NULL;
		AQ_UNLOCK();
		write_afsk_buf(ent->buf);
		free(ent);
	}
}

static void
flush_queue(void)
{
	struct afsk_queue *ent;

	AQ_LOCK();
	for (ent = qhead; ent != NULL; ent = qhead) {
		qhead = ent->next;
		free(ent);
		AQ_UNLOCK();
		assert(sem_trywait(&qsem) == 0);
		AQ_LOCK();
	}
	qhead = qtail = NULL;
	AQ_UNLOCK();
}

static void
end_afsk_thread(void)
{
	pthread_cancel(afsk_threadid);
	pthread_join(afsk_threadid, NULL);
	flush_queue();
}

struct send_fsk_api afsk_api = {
	.toggle_reverse = afsk_toggle_reverse,
	.end_tx = end_afsk_tx,
	.send_preamble = send_afsk_preamble,
	.send_char = send_afsk_char,
	.setup = setup_afsk,
	.diddle = diddle_afsk,
	.end_fsk = end_afsk_thread,
	.flush = flush_queue
};
