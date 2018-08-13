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

//#define RX_OVERRUNS
#define MATCHED_FILTERS

#include <sys/soundcard.h>
#include <sys/types.h>

#include <curses.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsdtty.h"
#include "fsk_demod.h"
#include "ui.h"

struct fir_filter {
	size_t		len;
	double		*buf;
	double		*tbuf;
	double		*coef;
	size_t		head;
};

struct bq_filter {
	double		coef[5];
	double		buf[4];
};

/* RX Stuff */
#ifdef RX_OVERRUNS
static int last_ro = -1;
#endif
static double phase_rate;
static double phase = 0.0;
// Mark filter
#ifdef MATCHED_FILTERS
static struct fir_filter *mfilt;
#else
static struct bq_filter *mfilt;
#endif
static struct bq_filter *mlpfilt;
// Space filter
#ifdef MATCHED_FILTERS
static struct fir_filter *sfilt;
#else
static struct bq_filter *sfilt;
#endif
static struct bq_filter *slpfilt;
// Mark phase filter
static struct bq_filter *mapfilt;
// Space phase filter
static struct bq_filter *sapfilt;
// Audio meter filter
static struct bq_filter *afilt;
// Hunt for Start
static double *hfs_buf;
static size_t hfs_bufmax;
static int hfs_head;
static int hfs_tail;
static int hfs_start;
static int hfs_b0;
static int hfs_b1;
static int hfs_b2;
static int hfs_b3;
static int hfs_b4;
static int hfs_stop1;
static int hfs_stop2;

/* Audio variables */
static int dsp = -1;
static int dsp_afsk = -1;
static int dsp_channels = 1;
static int16_t *dsp_buf;
static int head;
static int tail;	// Empty when equal
static size_t dsp_bufmax;
static struct bq_filter **waterfall_bp;
static struct bq_filter **waterfall_lp;
size_t waterfall_width;

/* AFSK buffers */
/*
 * Each of these is half of a bit-time long
 * The ones to/from zero use a quarter sine envelope
 */
struct afsk_buf {
	size_t size;
	int16_t *buf;
};
static struct afsk_buf zero_to_mark;
static struct afsk_buf zero_to_space;
static struct afsk_buf mark_to_zero;
static struct afsk_buf space_to_zero;
static struct afsk_buf mark_to_mark;
static struct afsk_buf space_to_space;
enum afsk_bit last_afsk_bit = AFSK_UNKNOWN;

static int avail(int head, int tail, int max);
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
static int next(int val, int max);
#if 0 // suppress warning
static int prev(int val, int max);
#endif
static void read_audio(void);
static void send_afsk_buf(struct afsk_buf *buf);
static void setup_audio(void);
static struct fir_filter * create_matched_filter(double frequency);
static double fir_filter(int16_t value, struct fir_filter *f);
static void generate_afsk_samples(void);
static void adjust_wave(struct afsk_buf *buf, double start_phase);
static void generate_sine(double freq, struct afsk_buf *buf);
static void feed_waterfall(int16_t value);

void
setup_rx(void)
{
	int hfs_buflen;

	setup_audio();

	phase_rate = 1/((double)settings.dsp_rate/((double)settings.baud_numerator / settings.baud_denominator));
	hfs_buflen = ((double)settings.dsp_rate/((double)settings.baud_numerator / settings.baud_denominator)) * 7.1 + 1;
	if (hfs_buf)
		free(hfs_buf);
	hfs_buf = malloc(hfs_buflen*sizeof(double));
	if (hfs_buf == NULL)
		printf_errno("allocating dsp buffer");
	hfs_bufmax = hfs_buflen - 1;
	create_filters();
}

int
get_rtty_ch(int state)
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
			if (current_value() < 0.0)
				break;
			phase += phase_rate;
			if (phase >= 1.6)
				return -1;
		}
	}
	else if (state == -1) {
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
		hfs_tail = 0;
		for (hfs_head = 0; hfs_head <= hfs_bufmax; hfs_head++)
			hfs_buf[hfs_head] = current_value();
		hfs_head = hfs_bufmax;
		hfs_start = ((1/phase_rate)*.5);
		hfs_b0 = ((1/phase_rate)*1.5);
		hfs_b1 = ((1/phase_rate)*2.5);
		hfs_b2 = ((1/phase_rate)*3.5);
		hfs_b3 = ((1/phase_rate)*4.5);
		hfs_b4 = ((1/phase_rate)*5.5);
		hfs_stop1 = ((1/phase_rate)*6.5);
		hfs_stop2 = hfs_bufmax;
		return -2;
	}
	else {
		/*
		 * Now we're in HfS mode
		 * First, check the existing buffer.
		 */
		if (hfs_buf[hfs_tail] >= 0.0 && hfs_buf[next(hfs_tail, hfs_bufmax)] < 0.0) {
			/* If there's a valid character in there, return it. */
			if (hfs_buf[hfs_start] < 0.0 &&
			    hfs_buf[hfs_stop1] >= 0.0 &&
			    hfs_buf[hfs_stop2] >= 0.0) {
				return (hfs_buf[hfs_b0] > 0.0) |
					((hfs_buf[hfs_b1] > 0.0) << 1) |
					((hfs_buf[hfs_b2] > 0.0) << 2) |
					((hfs_buf[hfs_b3] > 0.0) << 3) |
					((hfs_buf[hfs_b4] > 0.0) << 4);
			}
		}

		/* Now update it and stay in HfS. */
		hfs_buf[hfs_head] = current_value();
		hfs_head = next(hfs_head, hfs_bufmax);
		hfs_tail = next(hfs_tail, hfs_bufmax);
		hfs_start = next(hfs_start, hfs_bufmax);
		hfs_b0 = next(hfs_b0, hfs_bufmax);
		hfs_b1 = next(hfs_b1, hfs_bufmax);
		hfs_b2 = next(hfs_b2, hfs_bufmax);
		hfs_b3 = next(hfs_b3, hfs_bufmax);
		hfs_b4 = next(hfs_b4, hfs_bufmax);
		hfs_stop1 = next(hfs_stop1, hfs_bufmax);
		hfs_stop2 = next(hfs_stop2, hfs_bufmax);
		return -2;
	}

	/*
	 * Now we get the start bit... this is how we synchronize,
	 * so reset the phase here.
	 */
	phase = phase_rate;
	b = get_bit();
	if (b)
		return -1;

	/* Now read the five data bits */
	for (i = 0; i < 5; i++) {
		b = get_bit();
		ret |= b << i;
	}

	/* 
	 * Now, get a stop bit, which we expect to be at least
	 * 1.42 bits long.
	 */
	if (!get_stop_bit())
		return -1;

	return ret;
}

void
reset_rx(void)
{
	head = tail = 0;
#ifdef RX_OVERRUNS
	last_ro = -1;
#endif
}

static void
setup_audio(void)
{
	int i;
	int dsp_buflen;

	if (dsp != -1)
		close(dsp);
	dsp = open(settings.dsp_name, O_RDONLY);
	if (dsp == -1)
		printf_errno("unable to open sound device");
	i = AFMT_S16_NE;
	if (ioctl(dsp, SNDCTL_DSP_SETFMT, &i) == -1)
		printf_errno("setting format");
	if (i != AFMT_S16_NE)
		printf_errno("16-bit native endian audio not supported");
	if (ioctl(dsp, SNDCTL_DSP_CHANNELS, &dsp_channels) == -1)
		printf_errno("setting mono");
	if (ioctl(dsp, SNDCTL_DSP_SPEED, &settings.dsp_rate) == -1)
		printf_errno("setting sample rate");
	dsp_buflen = (int)((double)settings.dsp_rate / ((double)settings.baud_numerator / settings.baud_denominator)) + 1;
	if (dsp_buf)
		free(dsp_buf);
	dsp_buf = malloc(sizeof(dsp_buf[0]) * dsp_buflen);
	if (dsp_buf == NULL)
		printf_errno("allocating dsp buffer");
	dsp_bufmax = dsp_buflen - 1;

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
	i = dsp_channels;
	if (ioctl(dsp_afsk, SNDCTL_DSP_CHANNELS, &i) == -1)
		printf_errno("setting afsk channels");
	if (i != dsp_channels)
		printf_errno("afsk channel mismatch");
	i = settings.dsp_rate;
	if (ioctl(dsp_afsk, SNDCTL_DSP_SPEED, &i) == -1)
		printf_errno("setting sample rate");
	if (i != settings.dsp_rate)
		printf_errno("afsk sample rate mismatch");
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
		if (phase > 0.5 && nsamp == 0) {
			tot = cv;
			nsamp++;
		}
		/* Sampling is over, look for jitter */
		if (phase > 0.97) {
			if ((cv < 0.0) != (tot <= 0)) {
				// Value change... assume this is the end of the bit.
				// Set start phase for next bit.
				phase = 1 - phase;
				return tot > 0;
			}
		}
	}
	/* We over-read this bit... adjust next bit phase */
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
		if (phase > 0.5 && nsamp == 0) {
			ret = cv >= 0.0;
			nsamp++;
		}
		else if (phase > 1 && nsamp == 1) {
			if (cv < 0.0)
				ret = false;
			nsamp++;
		}
		if (phase > 1.39 && ret && cv < 0.0)
			return ret;
	}
	return ret;
}

static void
read_audio(void)
{
	int ret;
	int max;
	int16_t tmpbuf[128];
	int16_t *tb = tmpbuf;
	int i, j;
	audio_errinfo errinfo;

	/* Read into circular buffer */

	if (avail(head, tail, dsp_bufmax) > (sizeof(tmpbuf) / sizeof(*tb) / dsp_channels)) {
		ret = read(dsp, tmpbuf, sizeof(tmpbuf));
		if (ret == -1)
			printf_errno("reading audio input");
		if (head >= tail) {
			max = sizeof(tmpbuf) / sizeof(tmpbuf[0]) / dsp_channels;
			if (max > ret / sizeof(*tb) / dsp_channels)
				max = ret / sizeof(*tb) / dsp_channels;
			i = (dsp_bufmax - head + 1) >= max ? max : (dsp_bufmax - head + 1);
			if (dsp_channels == 1) {
				memcpy(dsp_buf + head, tb, i * sizeof(dsp_buf[0]));
				tb += i;
			}
			else {
				for (j = 0; j < i; j++) {
					memcpy(dsp_buf + head + j, tb, sizeof(*tb));
					tb += dsp_channels;
				}
			}
			ret -= i * sizeof(*tb) * dsp_channels;
			head += i;
			if (tail == head)
				printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
			if (head > dsp_bufmax)
				head -= dsp_bufmax;
		}
		if (head < tail) {
			if (dsp_channels == 1) {
				memcpy(dsp_buf + head, tb, ret);
				head += ret / sizeof(*tb);
				if (tail == head)
					printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
			}
			else {
				for (j = 0; j < ret; ret += sizeof(*tb)) {
					memcpy(dsp_buf + head, tb, sizeof(*tb));
					tb += dsp_channels;
					head++;
					if (tail == head)
						printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
				}
			}
		}
		ret = ioctl(dsp, SNDCTL_DSP_GETERROR, &errinfo);
		if (ret == -1)
			printf_errno("reading audio errors");
#ifdef RX_OVERRUNS
		// TODO: Figure out how to get this back in.
		if (last_ro != -1 && errinfo.rec_overruns)
			printf_errno("rec_overrun (%d)", errinfo.rec_overruns);
		last_ro = 0;
#endif
	}

	if (tail == head)
		printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
}

/*
 * The current demodulated value.  Essentially the difference between
 * the mark and space envelopes.
 */
static double
current_value(void)
{
	double mv, emv, sv, esv, cv, a;

	read_audio();

#ifdef MATCHED_FILTERS
	mv = fir_filter(dsp_buf[tail], mfilt);
	sv = fir_filter(dsp_buf[tail], sfilt);
#else
	mv = bq_filter(dsp_buf[tail], mbpfilt);
	sv = bq_filter(dsp_buf[tail], sbpfilt);
#endif
	emv = bq_filter(mv*mv, mlpfilt);
	esv = bq_filter(sv*sv, slpfilt);
	feed_waterfall(dsp_buf[tail]);
	update_tuning_aid(mv, sv);
	a = bq_filter((double)dsp_buf[tail] * dsp_buf[tail], afilt);
	audio_meter((int16_t)sqrt(a));
	tail++;
	if (tail > dsp_bufmax)
		tail = 0;

	cv = emv - esv;

	/* Return the current value */
	return cv;
}

// https://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
static void
create_filters(void)
{
#ifndef MATCHED_FILTERS
	free_bq_filter(mfilt);
	free_bq_filter(sfilt);
	mfilt = calc_bpf_coef(settings.mark_freq, settings.bp_filter_q);
	sfilt = calc_bpf_coef(settings.space_freq, settings.bp_filter_q);
#else
	free_fir_filter(mfilt);
	free_fir_filter(sfilt);
	mfilt = create_matched_filter(settings.mark_freq);
	sfilt = create_matched_filter(settings.space_freq);
#endif

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

	w0 = 2.0 * M_PI * (f0 / settings.dsp_rate);
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

	w0 = 2.0 * M_PI * (f0 / settings.dsp_rate);
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

	w0 = 2.0 * M_PI * (f0 / settings.dsp_rate);
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
	double res = 0;

	f->buf[f->head] = value;
	res = f->buf[f->head] * f->coef[0];
	f->head = next(f->head, f->len - 1);

	memcpy(f->tbuf, &f->buf[f->head], (f->len - f->head) * sizeof(f->tbuf[0]));
	memcpy(f->tbuf + (f->len - f->head), f->buf, f->head * sizeof(f->tbuf[0]));
	for (i = 0; i < f->len; i++)
		f->tbuf[i] *= f->coef[i];

	for (i = 0; i < f->len; i++)
		res += f->tbuf[i];

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
	ret->head = 0;
	/*
	 * For the given sample rate, calculate the number of
	 * samples in a complete wave
	 */
	ret->len = settings.dsp_rate / ((double)settings.baud_numerator / settings.baud_denominator) / 2;
	wavelen = settings.dsp_rate / frequency;

	ret->buf = calloc(sizeof(*ret->buf) * ret->len, 1);
	if (ret == NULL)
		printf_errno("allocating FIR buffer");
	ret->tbuf = malloc(sizeof(*ret->buf) * ret->len);
	if (ret == NULL)
		printf_errno("allocating FIR temp buffer");
	ret->coef = malloc(sizeof(*ret->coef) * ret->len);
	if (ret == NULL)
		printf_errno("allocating FIR coef");

	/*
	 * Now create a sine wave with that many samples in coef
	 */
	for (i = 0; i < ret->len; i++)
		ret->coef[ret->len - i - 1] = sin((double)i / wavelen * (2.0 * M_PI));

	return ret;
}

static int
avail(int head, int tail, int max)
{
	if (head == tail)
		return max;
	if (head < tail)
		return tail - head;
	return (max - head) + tail;
}

#if 0 // suppress warning
static int
prev(int val, int max)
{
	if (--val < 0)
		val = max;
	return val;
}
#endif

static int
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
	int16_t *tbuf;

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

	show_reverse(*rev);
}

static void
generate_sine(double freq, struct afsk_buf *buf)
{
	size_t i;
	double wavelen = settings.dsp_rate / freq;
	size_t nsamp = settings.dsp_rate / ((double)settings.baud_numerator / settings.baud_denominator * 2) + 2;

	if (buf->buf)
		free(buf->buf);
	buf->buf = calloc(sizeof(buf->buf[0]) * nsamp, dsp_channels);
	if (buf->buf == NULL)
		printf_errno("allocating AFSK buffer");

	for (i = 0; i < nsamp; i++)
		buf->buf[i*dsp_channels] = sin((double)i / wavelen * (2.0 * M_PI)) * (INT16_MAX >> 1);

	for (i = nsamp - 4; i < nsamp; i++) {
		if ((buf->buf[i * dsp_channels] >= 0) && (buf->buf[(i-1) * dsp_channels] <= 0))
			break;
	}
	if (i == nsamp) {
		for (--i; i > 0; i--) {
			if ((buf->buf[i * dsp_channels] >= 0) && (buf->buf[(i-1) * dsp_channels] <= 0))
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
		buf->buf[i * dsp_channels] *= (cos(start_phase) + 1) / 2;
		start_phase += phase_step;
	}
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
		if (f->tbuf)
			free(f->tbuf);
		if (f->coef)
			free(f->coef);
		free(f);
	}
}

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

void
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
	}
	last_afsk_bit = bit;
}

static void
send_afsk_buf(struct afsk_buf *buf)
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

void
send_afsk_char(char ch)
{
	int i;

	send_afsk_bit(AFSK_SPACE);
	for (i = 0; i < 5; i++) {
		send_afsk_bit(ch & 1 ? AFSK_MARK : AFSK_SPACE);
		ch >>= 1;
	}
	send_afsk_bit(AFSK_STOP);
}

void
end_afsk_tx(void)
{
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
}

void
setup_spectrum_filters(int buckets)
{
	int i;
	double freq_step = 4000 / (buckets + 1);
	double freq;
	double q;

	if (waterfall_bp) {
		for (i = 0; i < waterfall_width; i++) {
			if (waterfall_bp[i])
				free_bq_filter(waterfall_bp[i]);
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
	if (buckets == 0)
		return;
	waterfall_bp = calloc(sizeof(*waterfall_bp), buckets);
	if (waterfall_bp == NULL) {
		waterfall_width = 0;
		return;
	}
	waterfall_lp = calloc(sizeof(*waterfall_lp), buckets);
	if (waterfall_lp == NULL) {
		free(waterfall_bp);
		waterfall_width = 0;
		return;
	}
	waterfall_width = buckets;
	for (i = 0; i < buckets; i++) {
		freq = (freq_step / 2) + freq_step * i;
		q = freq / freq_step;
		waterfall_bp[i] = calc_bpf_coef(freq, q);
		if (waterfall_bp[i] == NULL) {
			setup_spectrum_filters(0);
			return;
		}
		waterfall_lp[i] = calc_bpf_coef(1, 0.5);
	}
	return;
}

static void
feed_waterfall(int16_t value)
{
	int i;
	double v;

	if (tuning_style != TUNE_ASCIIFALL)
		return;
	for (i = 0; i < waterfall_width; i++) {
		v = bq_filter(value, waterfall_bp[i]);
		bq_filter(v * v, waterfall_lp[i]);
	}
}

double
get_waterfall(int bucket)
{
	if (bucket < waterfall_width)
		return waterfall_lp[bucket]->buf[0];
	if (bucket > 0)
		return waterfall_lp[bucket - 1]->buf[0];
	return 0;
}
