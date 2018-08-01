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
#include "ui.h"

/* RX Stuff */
#ifdef RX_OVERRUNS
static int last_ro = -1;
#endif
static double phase_rate;
static double phase = 0;
// Mark filter
static double mbpfilt[5];
static double mlpfilt[5];
static double mbpbuf[4];
static double mlpbuf[4];
// Space filter
static double sbpfilt[5];
static double slpfilt[5];
static double sbpbuf[4];
static double slpbuf[4];
// Mark phase filter
static double mapfilt[5];
static double mapbuf[4];
// Space phase filter
static double sapfilt[5];
static double sapbuf[4];
// Hunt for Space
static double *hfs_buf = NULL;
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
static int dsp_channels = 1;
static uint16_t *dsp_buf;
static int head=0, tail=0;	// Empty when equal
static size_t dsp_bufmax;

static int avail(int head, int tail, int max);
static double bq_filter(double value, double *filt, double *buf);
static void calc_apf_coef(double f0, double q, double *filt);
static void calc_bpf_coef(double f0, double q, double *filt);
static void calc_lpf_coef(double f0, double q, double *filt);
static void create_filters(void);
static double current_value(void);
static bool get_bit(void);
static bool get_stop_bit(void);
static int next(int val, int max);
static int prev(int val, int max);
static void setup_audio(void);

void
setup_rx(void)
{
	int hfs_buflen;

	setup_audio();

	phase_rate = 1/((double)settings.dsp_rate/((double)settings.baud_numerator / settings.baud_denominator));
	hfs_buflen = ((double)settings.dsp_rate/((double)settings.baud_numerator / settings.baud_denominator)) * 7.5 + 1;
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
		hfs_start = hfs_bufmax * 0.133333333333333333;
		hfs_b0 = hfs_bufmax * 0.266666666666666666;
		hfs_b1 = hfs_bufmax * 0.4;
		hfs_b2 = hfs_bufmax * 0.533333333333333333;
		hfs_b3 = hfs_bufmax * 0.666666666666666666;
		hfs_b4 = hfs_bufmax * 0.8;
		hfs_stop1 = hfs_bufmax * 0.866666666666666666;
		hfs_stop2 = hfs_bufmax * 0.933333333333333333;
		return -2;
	}
	else {
		/*
		 * Now we're in HfS mode
		 * First, check the existing buffer.
		 */
		if (hfs_buf[hfs_tail] < 0.0 && hfs_buf[prev(hfs_tail, hfs_bufmax)] >= 0.0) {
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
	phase = 0;
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
	dsp_buf = malloc(sizeof(dsp_buf[0]) * dsp_buflen);
	if (dsp_buf == NULL)
		printf_errno("allocating dsp buffer");
	dsp_bufmax = dsp_buflen - 1;
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
	int i;
	int need = ((double)settings.dsp_rate / ((double)settings.baud_numerator / settings.baud_denominator)) * 0.9;
	int rst = 0;
	double cv;

	for (i = 0; i < need; i++) {
		if ((cv = current_value()) < 0.0) {
			i = 0;
			rst++;
			if (rst > need*2)
				return false;
			continue;
		}
	}
	return true;
}

/*
 * The current demodulated value.  Essentially the difference between
 * the mark and space envelopes.
 */
static double
current_value(void)
{
	int ret;
	int max;
	uint16_t tmpbuf[1];
	uint16_t *tb = tmpbuf;
	double mv, emv, sv, esv, cv;
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
#if RX_OVERRUNS
		// TODO: Figure out how to get this back in.
		if (last_ro != -1 && errinfo.rec_overruns)
			printf_errno("rec_overrun (%d)", errinfo.rec_overruns);
		last_ro = 0;
#endif
	}

	if (tail == head)
		printf_errno("underrun %d == %d (%d)\n", tail, head, dsp_bufmax);
	mv = bq_filter(dsp_buf[tail], mbpfilt, mbpbuf);
	emv = bq_filter(mv*mv, mlpfilt, mlpbuf);
	sv = bq_filter(dsp_buf[tail], sbpfilt, sbpbuf);
	esv = bq_filter(sv*sv, slpfilt, slpbuf);
	update_tuning_aid(bq_filter(mv, mapfilt, mapbuf), bq_filter(sv, sapfilt, sapbuf));
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
	/* TODO: Look into "Matched Filters"... all the rage */
	calc_bpf_coef(settings.mark_freq, settings.bp_filter_q, mbpfilt);
	mbpbuf[0] = mbpbuf[1] = mbpbuf[2] = mbpbuf[3] = 0.0;
	calc_bpf_coef(settings.space_freq, settings.bp_filter_q, sbpfilt);
	sbpbuf[0] = sbpbuf[1] = sbpbuf[2] = sbpbuf[3] = 0.0;

	/*
	 * TODO: Do we need to get the envelopes separately, or just
	 * take the envelope of the differences?
	 */
	calc_lpf_coef(((double)settings.baud_numerator / settings.baud_denominator)*1.1, settings.lp_filter_q, mlpfilt);
	mlpbuf[0] = mlpbuf[1] = mlpbuf[2] = mlpbuf[3] = 0.0;
	calc_lpf_coef(((double)settings.baud_numerator / settings.baud_denominator)*1.1, settings.lp_filter_q, slpfilt);
	slpbuf[0] = slpbuf[1] = slpbuf[2] = slpbuf[3] = 0.0;

	/*
	 * These are here to fix the phasing for the crossed bananas
	 * display.  The centre frequencies aren't calculated, they were
	 * selected via trial and error, so likely don't work for other
	 * mark/space frequencies.
	 * 
	 * TODO: Figure out how to calculate phase in biquad IIR filters.
	 */
	calc_apf_coef(settings.mark_freq / 1.75, 1, mapfilt);
	mapbuf[0] = mapbuf[1] = mapbuf[2] = mapbuf[3] = 0.0;
	calc_apf_coef(settings.space_freq * 1.75, 1, sapfilt);
	sapbuf[0] = sapbuf[1] = sapbuf[2] = sapbuf[3] = 0.0;
}

static void
calc_lpf_coef(double f0, double q, double *filt)
{
	double w0, cw0, sw0, a[5], b[5], alpha;

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
	filt[0] = b[0]/a[0];
	filt[1] = b[1]/a[0];
	filt[2] = b[2]/a[0];
	filt[3] = a[1]/a[0];
	filt[4] = a[2]/a[0];
}

static void
calc_bpf_coef(double f0, double q, double *filt)
{
	double w0, cw0, sw0, a[5], b[5], alpha;

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
	filt[0] = b[0]/a[0];
	filt[1] = b[1]/a[0];
	filt[2] = b[2]/a[0];
	filt[3] = a[1]/a[0];
	filt[4] = a[2]/a[0];
}

static void
calc_apf_coef(double f0, double q, double *filt)
{
	double w0, cw0, sw0, a[5], b[5], alpha;

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
	filt[0] = b[0]/a[0];
	filt[1] = b[1]/a[0];
	filt[2] = b[2]/a[0];
	filt[3] = a[1]/a[0];
	filt[4] = a[2]/a[0];
}

static double
bq_filter(double value, double *filt, double *buf)
{
	double y;

	y = (filt[0] * value) + (filt[1] * buf[0]) + (filt[2] * buf[1]) - (filt[3] * buf[2]) - (filt[4] * buf[3]);
	buf[1] = buf[0];
	buf[0] = value;
	buf[3] = buf[2];
	buf[2] = y;
	return y;
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

static int
prev(int val, int max)
{
	if (--val < 0)
		val = max;
	return val;
}

static int
next(int val, int max)
{
	if (++val > max)
		val = 0;
	return val;
}
