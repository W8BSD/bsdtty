#ifndef BSDTTY_H
#define BSDTTY_H

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>

struct bt_settings {
	char		*log_name;
	char		*tty_name;
	char		*dsp_name;
	double		bp_filter_q;
	double		lp_filter_q;
	double		mark_freq;
	double		space_freq;
	int		dsp_rate;
	int		baud_denominator;
	int		baud_numerator;
	char		*macros[10];
	int		charset;
	bool		afsk;
	char		*callsign;
	bool		ctl_ptt;
	bool		rigctld;
	char		*rigctld_host;
	uint16_t	rigctld_port;
	int		freq_offset;
	char		*xmlrpc_host;
	uint16_t	xmlrpc_port;
};

struct send_fsk_api {
	void (*toggle_reverse)(void);
	void (*end_tx)(void);
	void (*send_preamble)(void);
	void (*send_char)(char ch);
	void (*setup)(void);
	void (*diddle)(void);
	void (*end_fsk)(void);
	void (*flush)(void);
};

extern struct bt_settings settings;
extern pthread_rwlock_t settings_lock;
#define SETTING_RLOCK()		assert(pthread_rwlock_rdlock(&settings_lock) == 0)
#define SETTING_WLOCK()		assert(pthread_rwlock_wrlock(&settings_lock) == 0)
#define SETTING_UNLOCK()	assert(pthread_rwlock_unlock(&settings_lock) == 0)

extern bool reverse;
extern char *their_callsign;
extern unsigned serial;
extern struct send_fsk_api *send_fsk;
extern pthread_mutex_t bsdtty_lock;
#define BSDTTY_LOCK()		assert(pthread_mutex_lock(&bsdtty_lock) == 0)
#define BSDTTY_UNLOCK()		assert(pthread_mutex_unlock(&bsdtty_lock) == 0)

extern bool rts;
extern pthread_mutex_t rts_lock;
extern pthread_rwlock_t rts_rwlock;
#define RTS_RLOCK()	assert(pthread_mutex_lock(&rts_lock) == 0 && \
				pthread_rwlock_rdlock(&rts_rwlock) == 0 && \
				pthread_mutex_unlock(&rts_lock) == 0)
#define RTS_WLOCK()	assert(pthread_mutex_lock(&rts_lock) == 0 && \
				pthread_rwlock_wrlock(&rts_rwlock) == 0 && \
				pthread_mutex_unlock(&rts_lock) == 0)
#define RTS_DGLOCK()	assert(pthread_mutex_lock(&rts_lock) == 0 && \
				pthread_rwlock_unlock(&rts_rwlock) == 0 && \
				pthread_rwlock_rdlock(&rts_rwlock) == 0 && \
				pthread_mutex_unlock(&rts_lock) == 0)
#define RTS_UNLOCK()	assert(pthread_rwlock_unlock(&rts_rwlock) == 0)

int strtoi(const char *, char **endptr, int base);
unsigned int strtoui(const char *nptr, char **endptr, int base);
void captured_callsign(const char *str);
const char *format_freq(uint64_t freq);
void reinit(void);
void send_string(char *str);
bool do_macro(int fkey);

#endif
