#ifndef RIGCTL_H
#define RIGCTL_H

uint64_t get_rig_freq(void);
const char *get_rig_mode(char *buf, size_t sz);
bool get_rig_ptt(void);
bool set_rig_ptt(bool val);
void get_rig_freq_mode(uint64_t *freq, char *buf, size_t sz);
void setup_rig_control(void);

#endif
