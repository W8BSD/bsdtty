#ifndef FSK_DEMOD_H
#define FSK_DEMOD_H

int get_rtty_ch(int state);
void setup_rx(void);
void toggle_reverse(bool *rev);
double get_waterfall(int bucket);
void setup_spectrum_filters(int buckets);

#endif
