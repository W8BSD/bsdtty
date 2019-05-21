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

#define NOISE_CORRECT
//#define MATCHED_BUCKETS

#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/types.h>

#include <assert.h>
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#ifdef __FreeBSD__
#include <pthread_np.h>
#endif
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "baudot.h"
#include "bsdtty.h"
#include "fsk_demod.h"
#include "ui.h"

struct fir_filter {
	size_t		len;
	float		*buf;
	float		*coef;
};

struct bq_filter {
	double		coef[5];
	double		buf[4];
};

/* RX Stuff */
static double phase_rate;
static double phase = 0.0;
// Mark filter
static struct fir_filter *mfilt;
static struct bq_filter *mlpfilt;
// Space filter
static struct fir_filter *sfilt;
static struct bq_filter *slpfilt;
// Mark phase filter
static struct bq_filter *mapfilt;
// Space phase filter
static struct bq_filter *sapfilt;
#ifdef NOISE_CORRECT
// Mark/Space noise level
static float mnoise;
static float snoise;
static float mnsamp;
static float snsamp;
#endif
// Audio meter filter
static struct bq_filter *afilt;
// Hunt for Start
static atomic_bool hfs = ATOMIC_VAR_INIT(false);
static double *hfs_buf;
static size_t hfs_bufmax;
static size_t hfs_head;
static size_t hfs_tail;
static size_t hfs_start;
static size_t hfs_b0;
static size_t hfs_b1;
static size_t hfs_b2;
static size_t hfs_b3;
static size_t hfs_b4;
static size_t hfs_stop1;
static size_t hfs_stop2;

/* Audio variables */
static int dsp = -1;
static int dsp_channels = 1;
#ifdef MATCHED_BUCKETS
static struct fir_filter **waterfall_bp;
#else
static struct bq_filter **waterfall_bp;
#endif
static struct bq_filter **waterfall_lp;
size_t waterfall_width;
static pthread_mutex_t waterfall_mutex = PTHREAD_MUTEX_INITIALIZER;
#define WF_LOCK()	assert(pthread_mutex_lock(&waterfall_mutex) == 0)
#define WF_UNLOCK()	assert(pthread_mutex_unlock(&waterfall_mutex) == 0)

#if 0 // suppress warning
static int avail(int head, int tail, int max);
#endif
static double bq_filter(double value, struct bq_filter *filter);
static struct bq_filter * calc_apf_coef(double f0, double q);
static struct bq_filter * calc_bpf_coef(double f0, double q);
static struct bq_filter * calc_lpf_coef(double f0, double q);
static void create_filters(void);
static double current_value(void);
static void free_bq_filter(struct bq_filter *f);
static void free_fir_filter(struct fir_filter *f);
static bool get_bit(void);
static bool get_stop_bit(void);
static size_t next(int val, int max);
#if 0 // suppress warning
static int prev(int val, int max);
#endif
static int16_t read_audio(void);
static void setup_audio(void);
static struct fir_filter * create_matched_filter(double frequency);
static double fir_filter(int16_t value, struct fir_filter *f);
static void feed_waterfall(int16_t value);
static int read_rtty_ch(int state);
static void * rx_thread(void *arg);
static void rx_unlock(void *arg);

static char chbuf[256];
static size_t chh;
static size_t cht;
pthread_mutex_t chbuf_mutex = PTHREAD_MUTEX_INITIALIZER;
#define CH_LOCK()	assert(pthread_mutex_lock(&chbuf_mutex) == 0)
#define CH_UNLOCK()	assert(pthread_mutex_unlock(&chbuf_mutex) == 0)
static bool rxfigs;
pthread_mutex_t rx_lock = PTHREAD_MUTEX_INITIALIZER;

void
setup_rx(pthread_t *tid)
{
	int hfs_buflen;

	setup_audio();

	SETTING_RLOCK();
	phase_rate = 1/((double)settings.dsp_rate/((double)settings.baud_numerator / settings.baud_denominator));
	hfs_buflen = ((double)settings.dsp_rate/((double)settings.baud_numerator / settings.baud_denominator)) * 7.1 + 1;
	hfs_tail = 0;
	hfs_head = 0;
	hfs_start = ((1/phase_rate)*.5);
	hfs_b0 = ((1/phase_rate)*1.5);
	hfs_b1 = ((1/phase_rate)*2.5);
	hfs_b2 = ((1/phase_rate)*3.5);
	hfs_b3 = ((1/phase_rate)*4.5);
	hfs_b4 = ((1/phase_rate)*5.5);
	hfs_stop1 = ((1/phase_rate)*6.5);
	hfs_stop2 = hfs_bufmax;
	SETTING_UNLOCK();
	if (hfs_buf)
		free(hfs_buf);
	hfs_buf = malloc(hfs_buflen*sizeof(double));
	if (hfs_buf == NULL)
		printf_errno("allocating dsp buffer");
	hfs_bufmax = hfs_buflen - 1;
	create_filters();
	pthread_create(tid, NULL, rx_thread, NULL);
}

static int
read_rtty_ch(int state)
{
	bool b;
	int i;
	int ret = 0;

	/*
	 * Synchronize with the beginning of the start bit.
	 */
	if (state >= 0) {
		/*
		 * We got a stop bit last time, assume we're
		 * synchronized, and wait for up to 1.1 bit times for
		 * space to start.
		 * 
		 * If it doesn't start, go to "hunt for start" mode.
		 */
		for (phase = 0;;) {
			if (current_value() < 0.0 || hfs_head == hfs_tail)
				break;
			phase += phase_rate;
			if (phase >= 1.6) {
				state = -1;
				rxfigs = false;
				atomic_store(&hfs, true);
				break;
			}
		}
	}
	if (hfs_head == hfs_tail)
		state = -1;
	if (state < 0) {
		/*
		 * Start of "Hunt for Start" mode... this one is fun
		 * since it looks for a whole character rather than
		 * parsing as it goes.  This works by having a charlen
		 * buffer of cv, and any time we cross from mark to
		 * space, we look back and see if we have a stop bit
		 * at the end along with a start bit at the start.
		 * If we do, we return THAT character, and assume
		 * synchronization.
		 */

		/*
		 * Now we're in HfS mode
		 * First, check the existing buffer.
		 */
#ifdef NOISE_CORRECT
		mnoise = snoise = 0.0;	// No noise if no signal...
#endif
		for (;;) {
			// Fill up the ring buffer...
			while (next(hfs_head, hfs_bufmax) != hfs_tail)
				current_value();
			if (hfs_buf[hfs_tail] >= 0.0 && hfs_buf[next(hfs_tail, hfs_bufmax)] < 0.0) {
				/* If there's a valid character in there, return it. */
				if (hfs_buf[hfs_start] < 0.0 &&
				    hfs_buf[hfs_stop1] >= 0.0 &&
				    hfs_buf[hfs_stop2] >= 0.0) {
					phase = phase_rate;
					/*
					 * With NOISE_CORRECT, it would be nice to have initial
					 * noise levels here for the second character.
					 */
					hfs_tail = hfs_head;
					atomic_store(&hfs, false);
					return (hfs_buf[hfs_b0] > 0.0) |
						((hfs_buf[hfs_b1] > 0.0) << 1) |
						((hfs_buf[hfs_b2] > 0.0) << 2) |
						((hfs_buf[hfs_b3] > 0.0) << 3) |
						((hfs_buf[hfs_b4] > 0.0) << 4);
				}
			}
			current_value();
		}
	}

	/*
	 * Now we get the start bit... this is how we synchronize,
	 * so reset the phase here.
	 */
	phase = phase_rate;
	b = get_bit();
	if (hfs_head == hfs_tail)
		return -1;
	if (b)
		return -1;

	/* Now read the five data bits */
	for (i = 0; i < 5; i++) {
		b = get_bit();
		if (hfs_head == hfs_tail)
			return -1;
		ret |= b << i;
	}

	/* 
	 * Now, get a stop bit, which we expect to be at least
	 * 1.42 bits long.
	 */
	if (!get_stop_bit())
		return -1;
	if (hfs_head == hfs_tail)
		return -1;
	hfs_tail = hfs_head;

	return ret;
}

static void
setup_audio(void)
{
	int i;

	if (dsp != -1)
		close(dsp);
	SETTING_WLOCK();
	dsp = open(settings.dsp_name, O_RDONLY);
	if (dsp == -1)
		printf_errno("unable to open sound device %s", settings.dsp_name);
	i = AFMT_S16_NE;
	if (ioctl(dsp, SNDCTL_DSP_SETFMT, &i) == -1)
		printf_errno("setting format");
	if (i != AFMT_S16_NE)
		printf_errno("16-bit native endian audio not supported");
	if (ioctl(dsp, SNDCTL_DSP_CHANNELS, &dsp_channels) == -1)
		printf_errno("setting mono");
	if (ioctl(dsp, SNDCTL_DSP_SPEED, &settings.dsp_rate) == -1)
		printf_errno("setting sample rate");
	SETTING_UNLOCK();
}

/*
 * This gets a single bit value.
 */
static bool
get_bit(void)
{
	int nsamp;
	double cv;
	double tot;

	for (nsamp = 0; phase < 1.03; phase += phase_rate) {
		/* We only sample in the middle of the phase */
		cv = current_value();
		if (next(hfs_head, hfs_bufmax) != hfs_tail)
			return false;
		if (phase > 0.5 && nsamp == 0) {
			tot = cv;
#ifdef NOISE_CORRECT
			mnoise = mnsamp;
			snoise = snsamp;
#endif
			nsamp++;
		}
		/* Sampling is over, look for jitter */
		if (phase > 0.97 && nsamp == 1) {
			if ((cv < 0.0) != (tot <= 0)) {
				// Value change... assume this is the end of the bit.
				// Set start phase for next bit.
				phase = 1 - phase;
				return tot > 0;
			}
		}
	}
	/* We over-read this bit... adjust next bit phase */
	assert(nsamp);
	phase = -(1.0 - phase);
	return tot > 0;
}

/*
 * This gets a stop bit.
 */
static bool
get_stop_bit(void)
{
	int nsamp;
	double cv;
	bool ret = 0;

	for (nsamp = 0; phase < 1.42; phase += phase_rate) {
		cv = current_value();
		if (hfs_head == hfs_tail)
			return false;
		if (phase > 0.5 && nsamp == 0) {
			ret = cv >= 0.0;
			nsamp++;
		}
#ifdef NOISE_CORRECT
		else if (phase > 0.75 && nsamp == 1) {
			mnoise = mnsamp;
			nsamp++;
		}
		else if (phase > 1 && nsamp == 2) {
#else
		else if (phase > 1 && nsamp == 1) {
#endif
			if (cv < 0.0)
				ret = false;
			nsamp++;
		}
		if (phase > 1.39 && ret && cv < 0.0)
			return ret;
	}
	return ret;
}

static int16_t
read_audio(void)
{
	int ret;
	int16_t tmpbuf[16];	// Max 16 channels.  Heh.

	/* Read into circular buffer */

	for (;;) {
		ret = read(dsp, tmpbuf, sizeof(*tmpbuf) * dsp_channels);
		if (ret == -1) {
			if (errno != EINTR)
				printf_errno("reading audio input");
		}
		else
			break;
	}

	return tmpbuf[0];
}

/*
 * The current demodulated value.  Essentially the difference between
 * the mark and space envelopes.
 */
static double
current_value(void)
{
	int16_t sample;
	double mv, emv, sv, esv, cv, a;
	float mns, sns;

	if (pthread_mutex_trylock(&rx_lock) != 0) {
		RX_LOCK();
		hfs_tail = hfs_head;
	}
	sample = read_audio();

	mv = fir_filter(sample, mfilt);
	sv = fir_filter(sample, sfilt);
	emv = bq_filter(mv*mv, mlpfilt);
	esv = bq_filter(sv*sv, slpfilt);
#ifdef NOISE_CORRECT
	mns = emv;
	sns = esv;
	emv -= mnoise;
	esv -= snoise;
#endif
	feed_waterfall(sample);
	update_tuning_aid(mv, sv);
	a = bq_filter((double)sample * sample, afilt);
	audio_meter((int16_t)sqrt(a));
	RX_UNLOCK();

	/*
	 * TODO: A variable decision threshold may help out... essentially,
	 * instead of taking zero as the crossing point, take the
	 * envelope of both the mark and space signals as the extents,
	 * and set the zero point at the average.  The only question is
	 * how fast to have the envelopes respond to change since we're
	 * only guaranteed a mark and a space for each character... and
	 * extended mark for idle is entirely possible.
	 */
#ifdef NOISE_CORRECT
	if (emv > esv)
		mnsamp = mns;
	else
		snsamp = sns;
#endif
	cv = emv - esv;

	/* Return the current value */
	hfs_buf[hfs_head] = cv;
	hfs_head = next(hfs_head, hfs_bufmax);
	if (hfs_head == hfs_tail)
		hfs_tail = next(hfs_tail, hfs_bufmax);
	hfs_start = next(hfs_start, hfs_bufmax);
	hfs_b0 = next(hfs_b0, hfs_bufmax);
	hfs_b1 = next(hfs_b1, hfs_bufmax);
	hfs_b2 = next(hfs_b2, hfs_bufmax);
	hfs_b3 = next(hfs_b3, hfs_bufmax);
	hfs_b4 = next(hfs_b4, hfs_bufmax);
	hfs_stop1 = next(hfs_stop1, hfs_bufmax);
	hfs_stop2 = next(hfs_stop2, hfs_bufmax);
	return cv;
}

// https://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
static void
create_filters(void)
{
	free_fir_filter(mfilt);
	free_fir_filter(sfilt);
	SETTING_RLOCK();
	mfilt = create_matched_filter(settings.mark_freq);
	sfilt = create_matched_filter(settings.space_freq);

	/*
	 * TODO: Do we need to get the envelopes separately, or just
	 * take the envelope of the differences?
	 */
	free_bq_filter(mlpfilt);
	free_bq_filter(slpfilt);
	mlpfilt = calc_lpf_coef(((double)settings.baud_numerator / settings.baud_denominator)*1.1, settings.lp_filter_q);
	slpfilt = calc_lpf_coef(((double)settings.baud_numerator / settings.baud_denominator)*1.1, settings.lp_filter_q);

	/*
	 * These are here to fix the phasing for the crossed bananas
	 * display.  The centre frequencies aren't calculated, they were
	 * selected via trial and error, so likely don't work for other
	 * mark/space frequencies.
	 * 
	 * TODO: Figure out how to calculate phase in biquad IIR filters.
	 */
	free_bq_filter(mapfilt);
	free_bq_filter(sapfilt);
	mapfilt = calc_apf_coef(settings.mark_freq / 1.75, 1);
	sapfilt = calc_apf_coef(settings.space_freq * 1.75, 1);
	SETTING_UNLOCK();

	/* For the audio level meter */
	free_bq_filter(afilt);
	afilt = calc_lpf_coef(10, 0.5);
}

static struct bq_filter *
calc_lpf_coef(double f0, double q)
{
	struct bq_filter *ret;
	double w0, cw0, sw0, a[5], b[5], alpha;

	ret = malloc(sizeof(*ret));
	if (ret == NULL)
		printf_errno("allocating bpf");

	SETTING_RLOCK();
	w0 = 2.0 * M_PI * (f0 / settings.dsp_rate);
	SETTING_UNLOCK();
	cw0 = cos(w0);
	sw0 = sin(w0);

	alpha = sw0 / (2.0 * q);
	b[0] = (1.0-cw0)/2.0;
	b[1] = 1.0 - cw0;
	b[2] = (1.0-cw0)/2.0;
	a[0] = 1.0 + alpha;
	a[1] = -2.0 * cw0;
	a[2] = 1.0 - alpha;
	ret->coef[0] = b[0]/a[0];
	ret->coef[1] = b[1]/a[0];
	ret->coef[2] = b[2]/a[0];
	ret->coef[3] = a[1]/a[0];
	ret->coef[4] = a[2]/a[0];
	ret->buf[0] = 0;
	ret->buf[1] = 0;
	ret->buf[2] = 0;
	ret->buf[3] = 0;

	return ret;
}

static struct bq_filter *
calc_bpf_coef(double f0, double q)
{
	struct bq_filter *ret;
	double w0, cw0, sw0, a[5], b[5], alpha;

	ret = malloc(sizeof(*ret));
	if (ret == NULL)
		printf_errno("allocating bpf");

	SETTING_RLOCK();
	w0 = 2.0 * M_PI * (f0 / settings.dsp_rate);
	SETTING_UNLOCK();
	cw0 = cos(w0);
	sw0 = sin(w0);
	alpha = sw0 / (2.0 * q);

	//b[0] = q * alpha;
	b[0] = alpha;
	b[1] = 0.0;
	b[2] = -(b[0]);
	a[0] = 1.0 + alpha;
	a[1] = -2.0 * cw0;
	a[2] = 1.0 - alpha;
	ret->coef[0] = b[0]/a[0];
	ret->coef[1] = b[1]/a[0];
	ret->coef[2] = b[2]/a[0];
	ret->coef[3] = a[1]/a[0];
	ret->coef[4] = a[2]/a[0];
	ret->buf[0] = 0;
	ret->buf[1] = 0;
	ret->buf[2] = 0;
	ret->buf[3] = 0;

	return ret;
}

static struct bq_filter *
calc_apf_coef(double f0, double q)
{
	struct bq_filter *ret;
	double w0, cw0, sw0, a[5], b[5], alpha;

	ret = malloc(sizeof(*ret));
	if (ret == NULL)
		printf_errno("allocating bpf");

	SETTING_RLOCK();
	w0 = 2.0 * M_PI * (f0 / settings.dsp_rate);
	SETTING_UNLOCK();
	cw0 = cos(w0);
	sw0 = sin(w0);
	alpha = sw0 / (2.0 * q);

	//b[0] = q * alpha;
	b[0] = 1 - alpha;
	b[1] = -2 * cw0;
	b[2] = 1 + alpha;
	a[0] = 1.0 + alpha;
	a[1] = -2.0 * cw0;
	a[2] = 1.0 - alpha;
	ret->coef[0] = b[0]/a[0];
	ret->coef[1] = b[1]/a[0];
	ret->coef[2] = b[2]/a[0];
	ret->coef[3] = a[1]/a[0];
	ret->coef[4] = a[2]/a[0];
	ret->buf[0] = 0;
	ret->buf[1] = 0;
	ret->buf[2] = 0;
	ret->buf[3] = 0;

	return ret;
}

static double
bq_filter(double value, struct bq_filter *f)
{
	double y;

	y = (f->coef[0] * value) +
	    (f->coef[1] * f->buf[0]) + (f->coef[2] * f->buf[1]) -
	    (f->coef[3] * f->buf[2]) - (f->coef[4] * f->buf[3]);
	f->buf[1] = f->buf[0];
	f->buf[0] = value;
	f->buf[3] = f->buf[2];
	f->buf[2] = y;
	return y;
}

static double
fir_filter(int16_t value, struct fir_filter *f)
{
	size_t i;
	float res = 0;

	memmove(f->buf, &f->buf[1], sizeof(f->buf[0]) * (f->len - 1));
	f->buf[f->len - 1] = (float)value;

#pragma clang loop vectorize(enable)
	for (i = 0; i < f->len; i++)
		res += f->buf[i] * f->coef[i];

	return res / f->len;
}

static struct fir_filter *
create_matched_filter(double frequency)
{
	size_t i;
	struct fir_filter *ret;
	double wavelen;

	ret = malloc(sizeof(*ret));
	if (ret == NULL)
		printf_errno("allocating FIR filter");
	/*
	 * For the given sample rate, calculate the number of
	 * samples in a complete wave
	 */
	SETTING_RLOCK();
	ret->len = settings.dsp_rate / ((double)settings.baud_numerator / settings.baud_denominator) / 2;
	wavelen = settings.dsp_rate / frequency;
	SETTING_UNLOCK();

	ret->buf = calloc(sizeof(*ret->buf), ret->len);
	if (ret->buf == NULL)
		printf_errno("allocating FIR buffer");
	ret->coef = malloc(sizeof(*ret->coef) * ret->len);
	if (ret->coef == NULL)
		printf_errno("allocating FIR coef");

	/*
	 * Now create a sine wave with that many samples in coef
	 */
	for (i = 0; i < ret->len; i++)
		ret->coef[ret->len - i - 1] = sin((double)i / wavelen * (2.0 * M_PI));

	return ret;
}

#if 0 // suppress warning
static int
avail(int head, int tail, int max)
{
	if (head == tail)
		return max;
	if (head < tail)
		return tail - head;
	return (max - head) + tail;
}
#endif

#if 0 // suppress warning
static int
prev(int val, int max)
{
	if (--val < 0)
		val = max;
	return val;
}
#endif

static size_t
next(int val, int max)
{
	if (++val > max)
		val = 0;
	return val;
}

void
toggle_reverse(bool *rev)
{
	void *tmp;

	/*
	 * RX stuff, swap filters
	 */
	*rev = !(*rev);
	tmp = mfilt;
	mfilt = sfilt;
	sfilt = tmp;

	tmp = mlpfilt;
	mlpfilt = slpfilt;
	slpfilt = tmp;

	tmp = mapfilt;
	mapfilt = sapfilt;
	sapfilt = tmp;

	show_reverse(*rev);
}

static void
free_bq_filter(struct bq_filter *f)
{
	if (f)
		free(f);
}

static void
free_fir_filter(struct fir_filter *f)
{
	if (f) {
		if (f->buf)
			free(f->buf);
		if (f->coef)
			free(f->coef);
		free(f);
	}
}

void
setup_spectrum_filters(size_t buckets)
{
	size_t i;
	double freq_step = 4000.0 / (buckets + 1);
	double freq;
	double q;

	WF_LOCK();
	if (waterfall_bp) {
		for (i = 0; i < waterfall_width; i++) {
			if (waterfall_bp[i])
#ifdef MATCHED_BUCKETS
				free_fir_filter(waterfall_bp[i]);
#else
				free_bq_filter(waterfall_bp[i]);
#endif
		}
		free(waterfall_bp);
		waterfall_bp = NULL;
	}
	if (waterfall_lp) {
		for (i = 0; i < waterfall_width; i++) {
			if (waterfall_lp[i])
				free_bq_filter(waterfall_lp[i]);
		}
		free(waterfall_lp);
		waterfall_lp = NULL;
	}
	waterfall_width = 0;
	if (buckets == 0) {
		WF_UNLOCK();
		return;
	}
	waterfall_bp = calloc(sizeof(*waterfall_bp), buckets);
	if (waterfall_bp == NULL) {
		waterfall_width = 0;
		WF_UNLOCK();
		return;
	}
	waterfall_lp = calloc(sizeof(*waterfall_lp), buckets);
	if (waterfall_lp == NULL) {
		free(waterfall_bp);
		waterfall_width = 0;
		WF_UNLOCK();
		return;
	}
	waterfall_width = buckets;
	for (i = 0; i < buckets; i++) {
		freq = (freq_step / 2) + freq_step * i;
		q = freq / freq_step;
#ifdef MATCHED_BUCKETS
		waterfall_bp[i] = create_matched_filter(freq);
#else
		waterfall_bp[i] = calc_bpf_coef(freq, q);
#endif
		if (waterfall_bp[i] == NULL) {
			WF_UNLOCK();
			setup_spectrum_filters(0);
			return;
		}
		waterfall_lp[i] = calc_bpf_coef(1, 0.5);
	}
	WF_UNLOCK();
	return;
}

static void
feed_waterfall(int16_t value)
{
	size_t i;
	double v;

	if (tuning_style != TUNE_ASCIIFALL)
		return;
	WF_LOCK();
	for (i = 0; i < waterfall_width; i++) {
#ifdef MATCHED_BUCKETS
		v = fir_filter(value, waterfall_bp[i]);
#else
		v = bq_filter(value, waterfall_bp[i]);
#endif
		bq_filter(v * v, waterfall_lp[i]);
	}
	WF_UNLOCK();
}

double
get_waterfall(size_t bucket)
{
	double ret = 0;

	WF_LOCK();
	if (bucket < waterfall_width)
		ret = waterfall_lp[bucket]->buf[0];
	WF_UNLOCK();
	return ret;
}

static void
rx_unlock(void *arg)
{
	pthread_mutex_trylock(&rx_lock);
	RX_UNLOCK();
}

static void *
rx_thread(void *arg)
{
	int ret = -1;
	char ch;
	sigset_t blk;
	(void)arg;

	memset(&blk, 0xff, sizeof(blk));
	assert(pthread_sigmask(SIG_BLOCK, &blk, NULL) == 0);

#ifdef __linux__
	pthread_setname_np(pthread_self(), "RX");
#else
	pthread_set_name_np(pthread_self(), "RX");
#endif

	pthread_cleanup_push(rx_unlock, NULL);

	for (;;) {
		ret = read_rtty_ch(ret);
		if (is_fsk_char(ret)) {
			ch = baudot2asc(ret, rxfigs);
			switch (ch) {
				case 0x0e:
					rxfigs = true;
					break;
				case 0x0f:
				case ' ':	// USOS
					rxfigs = false;
					break;
			}
			write_rx(ch);
			CH_LOCK();
			chbuf[chh] = ch;
			chh = next(chh, sizeof(chbuf) - 1);
			if (chh == cht) {
				cht = next(cht, sizeof(chbuf) - 1);
				printf_errno("ring buffer full!");
			}
			CH_UNLOCK();
		}
		pthread_testcancel();
	}

	pthread_cleanup_pop(true);

	return NULL;
}

int
get_rtty_ch(void)
{
	int ret;

	CH_LOCK();
	if (chh != cht) {
		ret = chbuf[cht];
		cht = next(cht, sizeof(chbuf) - 1);
	}
	else {
		if (atomic_load(&hfs))
			ret = FSK_DEMOD_HFS;
		else
			ret = FSK_DEMOD_SYNC;
	}
	CH_UNLOCK();

	return ret;
}

void
end_fsk_thread(void)
{
	// There's no FSK thread to end.
}
