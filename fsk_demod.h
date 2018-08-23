#ifndef FSK_DEMOD_H
#define FSK_DEMOD_H

int get_rtty_ch(void);
void setup_rx(pthread_t *tid);
void toggle_reverse(bool *rev);
double get_waterfall(size_t bucket);
void setup_spectrum_filters(size_t buckets);

extern pthread_mutex_t rx_lock;
#define RX_LOCK()	assert(pthread_mutex_lock(&rx_lock) == 0)
#define RX_UNLOCK()	assert(pthread_mutex_unlock(&rx_lock) == 0)

#define FSK_DEMOD_HFS	-1
#define FSK_DEMOD_SYNC	-2
#define is_fsk_char(ch)	(ch >= 0)

#endif
