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

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <iniparser.h>

#include "api.h"

#include <ft-736r.h>
#include <ts-140s.h>
#include <ts-440s.h>
#include <ts-711a.h>
#include <ts-940s.h>

struct supported_rig supported_rigs[] = {
	{ "FT-736R", ft736r_init },
	{ "TS-140S", ts140s_init },
	{ "TS-440S", ts440s_init },
	{ "TS-680S", ts140s_init },
	{ "TS-711A", ts711a_init },
	{ "TS-711E", ts711a_init },
	{ "TS-811A", ts711a_init },
	{ "TS-811B", ts711a_init },
	{ "TS-811E", ts711a_init },
	{ "TS-940S", ts940s_init },
	{ "", NULL }
};

int set_default(struct _dictionary_ *d, const char *section, const char *key, const char *dflt)
{
	char	skey[1024];
	int		sret;
	int		ret=0;

	if (section == NULL || key == NULL)
		return -1;
	sret = snprintf(skey, sizeof(skey), "%s:%s", section, key);
	if (sret < 0 || sret >= sizeof(skey))
		return -1;
	if (!iniparser_find_entry(d, skey))
		ret = iniparser_set(d, skey, dflt);
	return ret;
}

int getint(struct _dictionary_ *d, const char *section, const char *key, int dflt)
{
	char	skey[1024];
	int		sret;
	int		ret=0;

	if (section == NULL || key == NULL)
		return -1;
	sret = snprintf(skey, sizeof(skey), "%s:%s", section, key);
	if (sret < 0 || sret >= sizeof(skey))
		return -1;
	ret = iniparser_getint(d, skey, dflt);
	return ret;
}

char *getstring(struct _dictionary_ *d, const char *section, const char *key, char *dflt)
{
	char	skey[1024];
	int		sret;
	char	*ret=0;

	if (section == NULL || key == NULL)
		return NULL;
	sret = snprintf(skey, sizeof(skey), "%s:%s", section, key);
	if (sret < 0 || sret >= sizeof(skey))
		return NULL;
	ret = iniparser_getstring(d, skey, dflt);
	return ret;
}

uint64_t getuint64(struct _dictionary_ *d, const char *section, const char *key, uint64_t dflt)
{
	uintmax_t	ret;
	char		*value = getstring(d, section, key, NULL);

	if (value == NULL)
		return dflt;
	ret = strtoumax(value, NULL, 10);
	if (ret == UINTMAX_MAX)
		return dflt;
	if (ret > UINT64_MAX)
		return UINT64_MAX;
	return ret;
}

struct bandlimit *find_bandlimit_by_name(struct rig *rig, const char *name, bool tx)
{
	struct bandlimit *limit;

	for (limit = tx?rig->tx_limits:rig->rx_limits; limit; limit = limit->next) {
		if (strcmp(name, limit->name) == 0)
			return limit;
	}
	return NULL;
}

struct bandlimit *find_bandlimit_by_freq(struct rig *rig, uint64_t freq, bool tx)
{
	struct bandlimit *limit;

	for (limit = tx?rig->tx_limits:rig->rx_limits; limit; limit = limit->next) {
		if (freq >= limit->low && freq <= limit->high)
			return limit;
	}
	return NULL;
}

struct bandlimit *new_bandlimit(struct rig *rig, char *name, bool tx)
{
	struct bandlimit *limit;

	limit = (struct bandlimit *)calloc(1, sizeof(struct bandlimit));
	if (limit == NULL)
		return NULL;
	limit->name = name;
	limit->high = UINT64_MAX;
	limit->low = 0;
	if (tx) {
		limit->next = rig->tx_limits;
		rig->tx_limits = limit;
	}
	else {
		limit->next = rig->rx_limits;
		rig->rx_limits = limit;
	}
	return limit;
}

struct rig *init_rig(struct _dictionary_ *d, char *section)
{
	int					i;
	char				*rig_name;
	struct rig			*rig;
	int					key_count;
	char				**keys;
	size_t				slen;
	struct bandlimit	*limit;

	rig_name = getstring(d, section, "rig", NULL);
	if (rig_name==NULL)
		return NULL;
	for (i=0; supported_rigs[i].init != NULL; i++) {
		if (strcmp(supported_rigs[i].name, rig_name)==0)
			break;
	}
	if (supported_rigs[i].init == NULL)
		return NULL;
	rig = supported_rigs[i].init(d, section);
	if (rig != NULL) {
		slen = strlen(section);
		slen++;
		// Fill in band edges.
		key_count = iniparser_getsecnkeys(d, section);
		keys = iniparser_getseckeys(d, section);
		for (i=0; i<key_count; i++) {
			if (strncmp(keys[i]+slen, "rx_bandlimit_low_", 17) == 0) {
				limit = find_bandlimit_by_name(rig, keys[i]+slen+17, false);
				if (limit == NULL)
					limit = new_bandlimit(rig, keys[i]+slen+17, false);
				if (limit == NULL) {
					close_rig(rig);
					return NULL;
				}
				limit->low = getuint64(d, section, keys[i]+slen, 0);
			}
			else if (strncmp(keys[i]+slen, "rx_bandlimit_high_", 18) == 0) {
				limit = find_bandlimit_by_name(rig, keys[i]+slen+18, false);
				if (limit == NULL)
					limit = new_bandlimit(rig, keys[i]+slen+18, false);
				if (limit == NULL) {
					close_rig(rig);
					return NULL;
				}
				limit->high = getuint64(d, section, keys[i]+slen, UINT64_MAX);
			}
			if (strncmp(keys[i]+slen, "tx_bandlimit_low_", 17) == 0) {
				limit = find_bandlimit_by_name(rig, keys[i]+slen+17, true);
				if (limit == NULL)
					limit = new_bandlimit(rig, keys[i]+slen+17, true);
				if (limit == NULL) {
					close_rig(rig);
					return NULL;
				}
				limit->low = getuint64(d, section, keys[i]+slen, 0);
			}
			else if (strncmp(keys[i]+slen, "tx_bandlimit_high_", 18) == 0) {
				limit = find_bandlimit_by_name(rig, keys[i]+slen+18, true);
				if (limit == NULL)
					limit = new_bandlimit(rig, keys[i]+slen+18, true);
				if (limit == NULL) {
					close_rig(rig);
					return NULL;
				}
				limit->high = getuint64(d, section, keys[i]+slen, UINT64_MAX);
			}
		}
	}
	return rig;
}

int close_rig(struct rig *rig)
{
	int	ret;
	struct bandlimit	*limit, *next_limit;

	if (rig == NULL)
		return EINVAL;
	if (rig->close == NULL)
		return 0;
	ret = rig->close(rig->cbdata);
	if (ret==0) {
		for (limit = rig->tx_limits; limit; limit = next_limit) {
			next_limit = limit->next;
			free(limit);
		}
		for (limit = rig->rx_limits; limit; limit = next_limit) {
			next_limit = limit->next;
			free(limit);
		}
		free(rig);
	}
	return ret;
}

int set_frequency(struct rig *rig, enum vfos vfo, uint64_t freq)
{
	if (rig == NULL)
		return EINVAL;
	if (rig->set_frequency == NULL)
		return ENOTSUP;
	if (find_bandlimit_by_freq(rig, freq, false) == NULL)
		return EINVAL;
	return rig->set_frequency(rig->cbdata, vfo, freq);
}

int set_split_frequency(struct rig *rig, uint64_t freq_rx, uint64_t freq_tx)
{
	if (rig == NULL)
		return EINVAL;
	if (rig->set_split_frequency == NULL)
		return ENOTSUP;
	if (find_bandlimit_by_freq(rig, freq_rx, false) == NULL)
		return EINVAL;
	if (find_bandlimit_by_freq(rig, freq_tx, true) == NULL)
		return EINVAL;
	return rig->set_split_frequency(rig->cbdata, freq_rx, freq_tx);
}

int set_duplex(struct rig *rig, uint64_t freq_rx, enum rig_modes mode_rx, uint64_t freq_tx, enum rig_modes mode_tx)
{
	if (rig == NULL)
		return EINVAL;
	if (rig->set_duplex == NULL)
		return ENOTSUP;
	if (find_bandlimit_by_freq(rig, freq_rx, false) == NULL)
		return EINVAL;
	if (find_bandlimit_by_freq(rig, freq_tx, true) == NULL)
		return EINVAL;
	return rig->set_duplex(rig->cbdata, freq_rx, mode_rx, freq_tx, mode_tx);
}

uint64_t get_frequency(struct rig *rig, enum vfos vfo)
{
	if (rig == NULL)
		return 0;
	if (rig->get_frequency == NULL)
		return 0;
	return rig->get_frequency(rig->cbdata, vfo);
}

int get_split_frequency(struct rig *rig, uint64_t *freq_rx, uint64_t *freq_tx)
{
	if (rig == NULL)
		return EINVAL;
	if (rig->get_split_frequency == NULL)
		return ENOTSUP;
	return rig->get_split_frequency(rig->cbdata, freq_rx, freq_tx);
}

int get_duplex(struct rig *rig, uint64_t *freq_rx, enum rig_modes *mode_rx, uint64_t *freq_tx, enum rig_modes *mode_tx)
{
	if (rig == NULL || freq_rx == NULL || freq_tx == NULL)
		return EINVAL;
	if (rig->get_duplex == NULL)
		return ENOTSUP;
	return rig->get_duplex(rig->cbdata, freq_rx, mode_rx, freq_tx, mode_tx);
}

int set_mode(struct rig *rig, enum rig_modes mode)
{
	if (rig == NULL)
		return EINVAL;
	if (rig->set_mode == NULL)
		return ENOTSUP;
	if ((rig->supported_modes & mode) == 0)
		return ENOTSUP;
	return rig->set_mode(rig->cbdata, mode);
}

enum rig_modes get_mode(struct rig *rig)
{
	if (rig == NULL)
		return MODE_UNKNOWN;
	if (rig->get_mode == NULL)
		return MODE_UNKNOWN;
	return rig->get_mode(rig->cbdata);
}

int set_vfo(struct rig *rig, enum vfos vfo)
{
	if (rig == NULL)
		return EINVAL;
	if (rig->set_vfo == NULL)
		return ENOTSUP;
	if ((rig->supported_vfos & vfo) == 0)
		return ENOTSUP;
	return rig->set_vfo(rig->cbdata, vfo);
}

enum vfos get_vfo(struct rig *rig)
{
	if (rig == NULL)
		return VFO_UNKNOWN;
	if (rig->get_vfo == NULL)
		return VFO_UNKNOWN;
	return rig->get_vfo(rig->cbdata);
}

int set_ptt(struct rig *rig, bool tx)
{
	if (rig == NULL)
		return EINVAL;
	if (rig->set_ptt == NULL)
		return ENOTSUP;
	return rig->set_ptt(rig->cbdata, tx);
}

int get_ptt(struct rig *rig)
{
	if (rig == NULL)
		return -1;
	if (rig->get_ptt == NULL)
		return -1;
	return rig->get_ptt(rig->cbdata);
}

int get_squelch(struct rig *rig)
{
	if (rig == NULL)
		return -1;
	if (rig->get_squelch == NULL)
		return -1;
	return rig->get_squelch(rig->cbdata);
}

int get_smeter(struct rig *rig)
{
	if (rig == NULL)
		return -1;
	if (rig->get_smeter == NULL)
		return -1;
	return rig->get_smeter(rig->cbdata);
}

