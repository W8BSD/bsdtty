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

/*
 * This file contains function to actually perform the rig communications
 * for rigs which use the Kenwood HF protocol (Kenwood, Elecraft, etc)
 * 
 * Individual rig sources will just be configuration and pointers to
 * these functions.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>

#include <api.h>
#include <iniparser.h>
#include <io.h>
#include <serial.h>
#include <datetime.h>

#include "kenwood_hf.h"

struct khf_param {
	const char		name[64];
	const unsigned	cols;
	const char		print_format[32];
	const char		scan_format[32];
	const char		type;
};

enum khf_sw {
	SW_OFF,
	SW_ON
};

enum khf_mode {
	KHF_MODE_LSB = 1,
	KHF_MODE_USB,
	KHF_MODE_CW,
	KHF_MODE_FM,
	KHF_MODE_AM,
	KHF_MODE_FSK,
	KHF_MODE_CWN
};

enum khf_function {
	FUNCTION_VFO_A,
	FUNCTION_VFO_B,
	FUNCTION_MEMORY,
	FUNCTION_COM
};

enum khf_txrx {
	KHF_RECEIVE,
	KHF_TRANSMIT
};

enum khf_params {
	KHF_PARAM_SW = 1,
	KHF_PARAM_MODE,				// 2
	KHF_PARAM_FUNCTION,			// 3
	KHF_PARAM_FREQUENCY,		// 4
	KHF_PARAM_RIT_FREQUENCY,	// 5
	KHF_PARAM_STEP_FREQUENCY,	// 6
	KHF_PARAM_MEMORY_CHANNEL,	// 7
	KHF_PARAM_MEMORY_BANK,		// 8
	KHF_PARAM_MEM_SPLIT_SPEC,	// 9
	KHF_PARAM_MEMORY_LOCKOUT,	// 10
	KHF_PARAM_TX_RX,			// 11
	KHF_PARAM_PASSBAND,			// 12
	KHF_PARAM_OFFSET,			// 13
	KHF_PARAM_TONE_FREQUENCY,	// 14
	KHF_PARAM_CALL_SIGN,		// 15
	KHF_PARAM_MODEL_NO			// 16
};

static const struct khf_param params[] = {
	{ "DUMMY", 0, "", "", 0 },
	{ "SW", 1, "%01d", "%1d", 'U' },
	{ "MODE", 1, "%01d", "%1d", 'U'},
	{ "FUNCTION", 1, "%01d", "%1d", 'U'},
	{ "FREQUENCY", 11, "%011"PRIu64, "%11"SCNu64, 'Q'},
	{ "RIT FREQUENCY", 5, "%+05d", "%5d", 'I'},
	{ "STEP FREQUENCY", 5, "%05d", "%5d", 'U'},
	{ "MEMORY CHANNEL", 2, "%02d", "%2d", 'U'},
	{ "MEMORY BANK", 1, "%01d", "%1d", 'U'},
	{ "MEMORY CHANNEL SPLIT SPECIFICATION", 1, "%01d", "%1d", 'U'},
	{ "MEMORY LOCKOUT", 1, "%01d", "%1d", 'U'},
	{ "TX/RX", 1, "%01d", "%1d", 'U'},
	{ "PASSBAND", 2, "%02d", "%2d", 'U'},
	{ "OFFSET", 1, "%01d", "%d", 'U'},
	{ "TONE FREQUENCY", 2, "%02d", "%2d", 'U'},
	{ "CALL SIGN", 6, "%-6.6s", "%6c", 'S'},
	{ "MODEL NO.", 3, "%03d", "%3d", 'U'}
};

struct khf_command {
	const char		cmd[8];
	const char		read_prefix[8];
	enum kenwood_hf_commands	cmd_num;
	unsigned		set_params_count;
	unsigned char	set_params[16];
	unsigned		get_params_count;
	unsigned char	get_params[16];
	unsigned		answer_params_count;
	unsigned char	answer_params[16];
};

enum khf_model {
	KHF_MODEL_TS_140_680 = 6,
	KHF_MODEL_TS_711 = 1,
	KHF_MODEL_TS_811 = 2,
	KHF_MODEL_TS_940 = 3
};

struct khf_command khf_cmd[] = {
	{ "AI", "AI", KW_HF_CMD_AI, 
		1, {KHF_PARAM_SW},
		1, {KHF_PARAM_SW},
		0
	},
	{ "AT1", "AT", KW_HF_CMD_AT1, 
		0, {0}, 
		0, {0}, 
		0, {0}
	},
	{ "DI", "DI", KW_HF_CMD_DI, 
		0, {0}, 
		0, {0}, 
		2, {KHF_PARAM_CALL_SIGN, KHF_PARAM_CALL_SIGN}
	},
	{ "DN", "DN", KW_HF_CMD_DN, 
		0, {0}, 
		0, {0}, 
		0, {0}
	},
	{ "DS", "DS", KW_HF_CMD_DS, 
		1, {KHF_PARAM_SW}, 
		0, {0}, 
		1, {KHF_PARAM_SW}
	},
	{ "FA", "FA", KW_HF_CMD_FA, 
		1, {KHF_PARAM_FREQUENCY}, 
		0, {0}, 
		1, {KHF_PARAM_FREQUENCY}
	},
	{ "FB", "FB", KW_HF_CMD_FB, 
		1, {KHF_PARAM_FREQUENCY}, 
		0, {0}, 
		1, {KHF_PARAM_FREQUENCY}
	},
	{ "FN", "FN", KW_HF_CMD_FN, 
		1, {KHF_PARAM_FUNCTION},
		0, {0}, 
		0, {0}
	},
	{ "HD", "HD", KW_HF_CMD_HD, 
		1, {KHF_PARAM_SW}, 
		0, {0}, 
		1, {KHF_PARAM_SW}
	},
	{ "ID", "ID", KW_HF_CMD_ID, 
		0, {0}, 
		0, {0}, 
		1, {KHF_PARAM_MODEL_NO}
	},
	{ "IF", "IF", KW_HF_CMD_IF, 
		0, {0}, 
		0, {0}, 
		15, {
			KHF_PARAM_FREQUENCY, 
			KHF_PARAM_STEP_FREQUENCY, 
			KHF_PARAM_RIT_FREQUENCY, 
			KHF_PARAM_SW, 
			KHF_PARAM_SW, 
			KHF_PARAM_MEMORY_BANK, 
			KHF_PARAM_MEMORY_CHANNEL, 
			KHF_PARAM_TX_RX, 
			KHF_PARAM_MODE, 
			KHF_PARAM_FUNCTION, 
			KHF_PARAM_SW, 
			KHF_PARAM_SW, 
			KHF_PARAM_SW, 
			KHF_PARAM_TONE_FREQUENCY, 
			KHF_PARAM_OFFSET
		}
	},
	{ "LK", "LK", KW_HF_CMD_LK, 
		1, {KHF_PARAM_SW}, 
		0, {0}, 
		1, {KHF_PARAM_SW}
	},
	{ "LO", "LO", KW_HF_CMD_LO, 
		0, {0}, 
		0, {0}, 
		0, {0}
	},
	{ "MC", "MC", KW_HF_CMD_MC, 
		2, {KHF_PARAM_MEMORY_BANK, KHF_PARAM_MEMORY_CHANNEL},
		0, {0}, 
		0, {0}
	},
	{ "MD", "MD", KW_HF_CMD_MD, 
		1, {KHF_PARAM_MODE}, 
		0, {0}, 
		0, {0}
	},
	{ "MR", "MR", KW_HF_CMD_MR, 
		0, {0}, 
		3, {
			KHF_PARAM_MEM_SPLIT_SPEC, 
			KHF_PARAM_MEMORY_BANK, 
			KHF_PARAM_MEMORY_CHANNEL
		},
		9, {
			KHF_PARAM_MEM_SPLIT_SPEC, 
			KHF_PARAM_MEMORY_BANK, 
			KHF_PARAM_MEMORY_CHANNEL, 
			KHF_PARAM_FREQUENCY, 
			KHF_PARAM_MODE, 
			KHF_PARAM_MEMORY_LOCKOUT, 
			KHF_PARAM_SW, 
			KHF_PARAM_TONE_FREQUENCY, 
			KHF_PARAM_OFFSET
		}
	},
	{ "MS", "MS", KW_HF_CMD_MS, 
		1, {KHF_PARAM_SW}, 
		0, {0}, 
		1, {KHF_PARAM_SW}
	},
	{ "MW", "MW", KW_HF_CMD_MW, 
		9, {
			KHF_PARAM_MEM_SPLIT_SPEC, 
			KHF_PARAM_MEMORY_BANK, 
			KHF_PARAM_MEMORY_CHANNEL, 
			KHF_PARAM_FREQUENCY, 
			KHF_PARAM_MODE, 
			KHF_PARAM_MEMORY_LOCKOUT, 
			KHF_PARAM_SW, 
			KHF_PARAM_TONE_FREQUENCY, 
			KHF_PARAM_OFFSET
		}, 
		0, {0}, 
		0, {0}
	},
	{ "OS", "OS", KW_HF_CMD_OS, 
		1, {KHF_PARAM_TONE_FREQUENCY}, 
		0, {0}, 
		0, {0}
	},
	{ "RC", "RC", KW_HF_CMD_RC, 
		0, {0}, 
		0, {0}, 
		0, {0}
	},
	{ "RD", "RD", KW_HF_CMD_RD, 
		0, {0}, 
		0, {0}, 
		0, {0}
	},
	{ "RU", "RU", KW_HF_CMD_RU, 
		0, {0}, 
		0, {0}, 
		0, {0}
	},
	{ "RT", "RT", KW_HF_CMD_RT,
		1, { KHF_PARAM_SW },
		0, {0},
		0, {0}
	},
	{ "RX", "RX", KW_HF_CMD_RX,
		0, {0},
		0, {0},
		0, {0}
	},
	{ "TX", "TX", KW_HF_CMD_TX,
		0, {0},
		0, {0},
		0, {0}
	},
	{ "SC", "SC", KW_HF_CMD_SC,
		1, {KHF_PARAM_SW},
		0, {0},
		0, {0}
	},
	{ "SH", "SH", KW_HF_CMD_SH,
		1, {KHF_PARAM_PASSBAND},
		0, {0},
		1, {KHF_PARAM_PASSBAND}
	},
	{ "SL", "SL", KW_HF_CMD_SL,
		1, {KHF_PARAM_PASSBAND},
		0, {0},
		1, {KHF_PARAM_PASSBAND}
	},
	{ "SP", "SP", KW_HF_CMD_SP,
		1, {KHF_PARAM_SW},
		0, {0},
		0, {0}
	},
	{ "ST", "ST", KW_HF_CMD_ST,
		1, {KHF_PARAM_STEP_FREQUENCY},
		0, {0},
		0, {0}
	},
	{ "TN", "TN", KW_HF_CMD_TN,
		1, {KHF_PARAM_TONE_FREQUENCY},
		0, {0},
		0, {0}
	},
	{ "TO", "TO", KW_HF_CMD_TO,
		1, {KHF_PARAM_SW},
		0, {0},
		0, {0}
	},
	{ "VB", "VB", KW_HF_CMD_VB,
		1, {KHF_PARAM_PASSBAND},
		0, {0},
		1, {KHF_PARAM_PASSBAND}
	},
	{ "VR", "VR", KW_HF_CMD_VR,
		0, {0},
		0, {0},
		0, {0}
	},
	{ "XT", "XT", KW_HF_CMD_XT,
		1, {KHF_PARAM_SW},
		0, {0},
		0, {0}
	},


	{ "", "", KW_HF_CMD_COUNT, 0, {0}, 0, {0}, 0, {0} }
};

/*
 * realloc()s a string to the specified size.
 * If realloc() failes, free()s the string and
 * sets it to NULL
 */
static void resize_response(struct io_response **resp, size_t newsize)
{
	struct io_response *newresp = realloc(*resp, newsize);

	if(newresp == NULL)
		free(&resp);
	*resp=newresp;
}

/*
 * Reads a single semi-colon terminated string from the serial port
 * and returns a null terminated malloc()ed struct io_response *
 */
struct io_response *kenwood_hf_read_response(void *cbdata)
{
	size_t	retsize = 128;
	size_t	pos = 0;
	int		rd;
	struct kenwood_hf *khf = (struct kenwood_hf *)cbdata;
	struct io_response	*ret = (struct io_response *)malloc(retsize);

	if (ret == NULL)
		return NULL;
	if (io_wait_read(khf->handle, khf->response_timeout) != 1)
		goto fail;
	for(;;) {
		rd = io_read(khf->handle, ret->msg+pos, 1, khf->char_timeout);
		if (rd != 1)
			goto fail;
		pos++;
		if(pos + 1 + offsetof(struct io_response, msg) == retsize) {
			retsize *= 2;
			resize_response(&ret, retsize);
			if (ret==NULL)
				goto fail;
		}
		if (ret->msg[pos-1] == ';') {
			ret->msg[pos] = 0;
			ret->len = pos;
			return ret;
		}
	}

fail:
	if(ret)
		free(ret);
	return NULL;
}

/*
 * Sends the first wlen bytes of cmd to the serial port
 */
static int kenwood_send(struct kenwood_hf *khf, const char *cmd, size_t wlen)
{
	uint64_t	now = ms_ticks();
	
	if (khf == NULL)
		return -1;

	if(now <= khf->last_cmd_tick + khf->inter_cmd_delay + khf->additional_intercmd_delay)
		ms_sleep((unsigned)(khf->last_cmd_tick + khf->inter_cmd_delay + khf->additional_intercmd_delay - now));
	khf->last_cmd_tick = ms_ticks();
	khf->additional_intercmd_delay = 0;
	return io_write(khf->handle, cmd, wlen, khf->char_timeout);
}

/*
 * Sends the first cmdlen bytes of cmd to the serial port, then calls
 * kenwood_get_response() to wait for a response matching the first
 * matchlen bytes of match.
 * 
 * Returns a null-termianted malloc()ed string and sets retlen to the
 * length of that string.
 */
static struct io_response *kenwood_cmd_response(struct kenwood_hf *khf, const char *match, size_t matchlen, const char *cmd, size_t cmdlen)
{
	if (kenwood_send(khf, cmd, cmdlen) == -1)
		return NULL;
	return io_get_response(khf->handle, match, matchlen, 0);
}

/*
 * TODO: Build an index
 */
static struct khf_command *kenwood_find_command(enum kenwood_hf_commands cmd)
{
	int i;

	for (i=0; khf_cmd[i].cmd_num != KW_HF_CMD_COUNT; i++) {
		if (khf_cmd[i].cmd_num == cmd)
			return &khf_cmd[i];
	}
	return NULL;
}

static struct io_response *kenwood_command_response(struct kenwood_hf *khf, struct khf_command *cmdinfo, const char *cmd, size_t cmdlen)
{
	if (cmdinfo == NULL || cmd == NULL || cmdlen == 0)
		return NULL;

	return kenwood_cmd_response(khf, cmdinfo->read_prefix, strlen(cmdinfo->read_prefix), cmd, cmdlen);
}

static int kenwood_rscanf(enum kenwood_hf_commands cmd, struct io_response *resp, ...)
{
	va_list		args;
	size_t		pos = 0;
	unsigned	i;
	char		*strval;
	int			*ival;
	unsigned	*uval;
	uint64_t	*qval;
	struct khf_command	*cmdinfo = kenwood_find_command(cmd);
	int			ret = 0;
	int			res;

	if (resp == NULL)
		return EOF;
	if (cmdinfo == NULL)
		return EOF;
	pos = strlen(cmdinfo->cmd);
	if (strncmp(cmdinfo->cmd, resp->msg, pos) != 0)
		return EOF;
	va_start(args, resp);
	for(i=0; i<cmdinfo->answer_params_count; i++) {
		switch(params[cmdinfo->answer_params[i]].type) {
			case 'I':
				ival = va_arg(args, int *);
				res = sscanf(resp->msg+pos, params[cmdinfo->answer_params[i]].scan_format, ival);
				if (res != 1) {
					res = 0;
					*ival = INT_MAX;
				}
				break;
			case 'U':
				uval = va_arg(args, unsigned *);
				res = sscanf(resp->msg+pos, params[cmdinfo->answer_params[i]].scan_format, uval);
				if (res != 1) {
					res = 0;
					*uval = UINT_MAX;
				}
				break;
			case 'Q':
				qval = va_arg(args, uint64_t *);
				res = sscanf(resp->msg+pos, params[cmdinfo->answer_params[i]].scan_format, qval);
				if (res != 1) {
					res = 0;
					*qval = UINT64_MAX;
				}
				break;
			case 'S':
				strval = va_arg(args, char *);
				res = sscanf(resp->msg+pos, params[cmdinfo->answer_params[i]].scan_format, strval);
				if (res != 1) {
					res = 0;
					*strval = 0;
				}
				break;
		}
		pos += params[cmdinfo->answer_params[i]].cols;
		ret += res;
	}

	va_end(args);
	return ret;
}

struct io_response *kenwood_hf_command(struct kenwood_hf *khf, bool set, enum kenwood_hf_commands cmd, ...)
{
	char			cmdstr[128];
	char			pstr[128];
	unsigned		i;
	va_list			args;
	struct khf_command	*cmdinfo = kenwood_find_command(cmd);
	int				ival;
	unsigned		uval;
	uint64_t		qval;
	char			*strval;
	int				ret;
	size_t			len=0;
	unsigned		count;
	unsigned char	*par;

	if (cmdinfo == NULL)
		return NULL;
	if (set) {
		if (!kenwood_hf_cmd_set(khf, cmd))
			return NULL;
	}
	else {
		if (!kenwood_hf_cmd_read(khf, cmd))
			return NULL;
	}

	count = set?cmdinfo->set_params_count:cmdinfo->get_params_count;
	par = set?cmdinfo->set_params:cmdinfo->get_params;

	strcpy(cmdstr, cmdinfo->cmd);
	len = strlen(cmdstr);
	va_start(args, cmd);
	for(i=0; i<count; i++) {
		switch(params[par[i]].type) {
			case 'Q':
				qval = va_arg(args, uint64_t);
				ret = sprintf(pstr, params[par[i]].print_format, qval);
				strcpy(cmdstr+len, pstr);
				break;
			case 'U':
				uval = va_arg(args, unsigned);
				ret = sprintf(pstr, params[par[i]].print_format, uval);
				strcpy(cmdstr+len, pstr);
				break;
			case 'I':
				ival = va_arg(args, int);
				ret = sprintf(pstr, params[par[i]].print_format, ival);
				strcpy(cmdstr+len, pstr);
				break;
			case 'S':
				strval = va_arg(args, char *);
				ret = sprintf(pstr, params[par[i]].print_format, strval);
				strcpy(cmdstr+len, pstr);
				break;
			default:
				va_end(args);
				return NULL;
		}
		if (ret == -1) {
			va_end(args);
			return NULL;
		}
		len += ret;
	}
	cmdstr[len++]=';';
	cmdstr[len]=0;
	va_end(args);
	if (set) {
		struct io_response *resp = (struct io_response *)malloc(offsetof(struct io_response, msg));
		resp->len = kenwood_send(khf, cmdstr, len);
		khf->additional_intercmd_delay = khf->set_cmd_delays[cmd];
		return resp;
	}
	return kenwood_command_response(khf, cmdinfo, cmdstr, len);
}

static struct kenwood_if *kenwood_parse_if(struct io_response *resp)
{
	struct kenwood_if *ret = (struct kenwood_if *)malloc(sizeof(struct kenwood_if));

	if(ret == NULL)
		return ret;

	switch(kenwood_rscanf(KW_HF_CMD_IF, resp, &ret->freq, &ret->step, &ret->rit,
			&ret->rit_on, &ret->xit_on, &ret->bank, &ret->channel, &ret->tx,
			&ret->mode, &ret->function, &ret->scan, &ret->split, &ret->tone,
			&ret->tone_freq, &ret->offset)) {
		case EOF:
		case 0:
			free(ret);
			return NULL;
		default:
			return ret;
	}
}

/*
 * This handles any "extra" responses recieved
 * ie: AI mode
 * 
 * Any lock may be held, so MUST NOT lock or post semaphores.
 */
void kenwood_hf_handle_extra(void *handle, struct io_response *resp)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)handle;
	struct kenwood_if	*rif;

	if (resp==NULL)
		return;

	if (resp->len >= 2) {
		if (resp->msg[0] == 'I' && resp->msg[1] == 'F') {
			rif = kenwood_parse_if(resp);
			if (rif != NULL) {
				mutex_lock(&khf->cache_mtx);
				khf->last_if = *rif;
				mutex_unlock(&khf->cache_mtx);
				free(rif);
			}
		}
	}

	return;
}

static int kenwood_update_if(struct kenwood_hf *khf, bool lock)
{
	uint64_t			now = ms_ticks();
	struct io_response	*resp;
	struct kenwood_if	*rif;

	/*
	 * We shouldn't really need to do this ever because of AI mode
	 */
	if (khf == NULL || (khf->last_if_tick != 0 && khf->last_if_tick + khf->if_lifetime >= now)) {
		if (lock)
			mutex_lock(&khf->cache_mtx);
		return 0;
	}

	resp = kenwood_hf_command(khf, false, KW_HF_CMD_IF);
	if (resp==NULL) {
		if (lock)
			mutex_lock(&khf->cache_mtx);
		return -1;
	}
	rif = kenwood_parse_if(resp);
	if (rif == NULL) {
		if (lock)
			mutex_lock(&khf->cache_mtx);
		return -1;
	}
	mutex_lock(&khf->cache_mtx);
	khf->last_if = *rif;
	if(!lock)
		mutex_unlock(&khf->cache_mtx);
	free(rif);
	khf->last_if_tick = now;
	return 0;
}

struct kenwood_hf *kenwood_hf_new(struct _dictionary_ *d, const char *section)
{
	struct kenwood_hf *khf = (struct kenwood_hf *)calloc(1, sizeof(struct kenwood_hf));

	if (khf == NULL || d == NULL)
		return NULL;
	if (mutex_init(&khf->cache_mtx) != 0) {
		free(khf);
		return NULL;
	}

	/*
	 * Set up some reasonable defaults to be shared among ALL rigs
	 */
	khf->response_timeout = getint(d, section, "response_timeout", 1000);
	khf->char_timeout = getint(d, section, "char_timeout", 50);
	khf->send_timeout = getint(d, section, "send_timeout", 500);
	khf->if_lifetime = getint(d, section, "cache_lifetime", 1000);
	khf->inter_cmd_delay = getint(d, section, "inter_cmd_delay", 0);

	return khf;
}

int kenwood_hf_init(struct kenwood_hf *khf)
{
	struct io_response			*resp;

	// Send an IF command to synchronize... may fail.
	resp = kenwood_hf_command(khf, false, KW_HF_CMD_IF);
	if (resp)
		free(resp);
	// Lock the front panel and enable AI mode
	resp = kenwood_hf_command(khf, true, KW_HF_CMD_LK, 0);
	if (resp)
		free(resp);
	else
		return -1;
	resp = kenwood_hf_command(khf, true, KW_HF_CMD_AI, 1);
	if (resp)
		free(resp);
	else
		return -1;
	// Get the initial state...
	return kenwood_update_if(khf, false);
}

void kenwood_hf_free(struct kenwood_hf *khf)
{
	if (khf == NULL)
		return;
	mutex_destroy(&khf->cache_mtx);
	free(khf);
}

void kenwood_hf_setbits(char *array, ...)
{
	va_list						bits;
	enum kenwood_hf_commands	bit;

	va_start(bits, array);
	for (bit = va_arg(bits, int); bit != KW_HF_TERMINATOR; bit = va_arg(bits, int)) {
		array[bit/8] |= (1<<(bit%8));
	}
	va_end(bits);
}

void kenwood_hf_set_cmd_delays(struct kenwood_hf *khf, ...)
{
	va_list						cmds;
	enum kenwood_hf_commands	cmd;
	int							delay;

	va_start(cmds, khf);
	for (cmd = va_arg(cmds, int); cmd != KW_HF_TERMINATOR; cmd = va_arg(cmds, int)) {
		delay = va_arg(cmds, int);
		if (delay >= 0)
			khf->set_cmd_delays[cmd] = delay;
	}
	va_end(cmds);
}

static int disable_rit_xit(struct kenwood_hf *khf, bool xit)
{
	struct io_response			*resp;

	resp = kenwood_hf_command(khf, true, xit?KW_HF_CMD_XT:KW_HF_CMD_RT, SW_OFF);
	if (resp == NULL)
		return EINTR;
	free(resp);
	mutex_lock(&khf->cache_mtx);
	if (xit)
		khf->last_if.xit_on = SW_OFF;
	else
		khf->last_if.rit_on = SW_OFF;
	mutex_unlock(&khf->cache_mtx);
	return 0;
}

int kenwood_hf_set_frequency(void *cbdata, enum vfos vfo, uint64_t freq)
{
	struct kenwood_hf			*khf = (struct kenwood_hf *)cbdata;
	struct io_response			*resp;
	enum kenwood_hf_commands	cmd;
	enum khf_function			func;
	enum khf_sw					split;
	enum khf_sw					rit_on;
	enum khf_sw					xit_on;
	int							ret;

	if (khf == NULL)
		return EINVAL;

	// TODO: Ensure we're not changing bands too
	if (vfo == VFO_UNKNOWN) {
		// First, get the current VFO.
		kenwood_update_if(khf, true);
		func = khf->last_if.function;
		split = khf->last_if.split;
		rit_on = khf->last_if.rit_on;
		xit_on = khf->last_if.xit_on;
		mutex_unlock(&khf->cache_mtx);
		switch(func) {
			case FUNCTION_MEMORY:
			case FUNCTION_COM:
				return EACCES;
			case FUNCTION_VFO_A:
				cmd = KW_HF_CMD_FA;
				break;
			case FUNCTION_VFO_B:
				cmd = KW_HF_CMD_FB;
				break;
		}
	}
	else {
		switch(vfo) {
			case VFO_A:
				cmd = KW_HF_CMD_FA;
				break;
			case VFO_B:
				cmd = KW_HF_CMD_FB;
				break;
			default:
				return EACCES;
		}
	}
	resp = kenwood_hf_command(khf, true, cmd, freq);
	if (resp == NULL)
		return ENODEV;
	mutex_lock(&khf->cache_mtx);
	khf->last_if.freq = freq;
	mutex_unlock(&khf->cache_mtx);
	free(resp);
	if (split == SW_ON) {
		resp = kenwood_hf_command(khf, true, KW_HF_CMD_SP, SW_OFF);
		if (resp == NULL)
			return EINTR;
		free(resp);
		mutex_lock(&khf->cache_mtx);
		khf->last_if.split = SW_OFF;
		mutex_unlock(&khf->cache_mtx);
	}
	if (rit_on == SW_ON) {
		ret = disable_rit_xit(khf, false);
		if (ret != 0)
			return ret;
	}
	if (xit_on == SW_ON) {
		ret = disable_rit_xit(khf, true);
		if (ret != 0)
			return ret;
	}
	
	return 0;
}

int kenwood_hf_set_split_frequency(void *cbdata, uint64_t freq_rx, uint64_t freq_tx)
{
	struct kenwood_hf			*khf = (struct kenwood_hf *)cbdata;
	struct io_response			*resp;
	enum kenwood_hf_commands	rx_cmd;
	enum kenwood_hf_commands	tx_cmd;
	enum khf_function			func;
	enum khf_sw					rit_on;
	enum khf_sw					xit_on;
	enum khf_sw					split;
	int							ret;

	if (khf == NULL)
		return EINVAL;

	// First, get the current VFO.
	kenwood_update_if(khf, true);
	func = khf->last_if.function;
	rit_on = khf->last_if.rit_on;
	xit_on = khf->last_if.xit_on;
	split = khf->last_if.split;
	mutex_unlock(&khf->cache_mtx);
	switch(func) {
		case FUNCTION_MEMORY:
		case FUNCTION_COM:
			return EACCES;
		case FUNCTION_VFO_A:
			rx_cmd = KW_HF_CMD_FA;
			tx_cmd = KW_HF_CMD_FB;
			break;
		case FUNCTION_VFO_B:
			rx_cmd = KW_HF_CMD_FB;
			tx_cmd = KW_HF_CMD_FA;
			break;
	}
	resp = kenwood_hf_command(khf, true, rx_cmd, freq_rx);
	if (resp == NULL)
		return ENODEV;
	mutex_lock(&khf->cache_mtx);
	khf->last_if.freq = freq_rx;
	mutex_unlock(&khf->cache_mtx);
	free(resp);
	resp = kenwood_hf_command(khf, true, tx_cmd, freq_tx);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	if (rit_on == SW_ON) {
		ret = disable_rit_xit(khf, false);
		if (ret != 0)
			return ret;
	}
	if (xit_on == SW_ON) {
		ret = disable_rit_xit(khf, true);
		if (ret != 0)
			return ret;
	}
	if (split == SW_OFF) {
		resp = kenwood_hf_command(khf, true, KW_HF_CMD_SP, SW_ON);
		if (resp == NULL)
			return ENODEV;
		mutex_lock(&khf->cache_mtx);
		khf->last_if.split = SW_ON;
		mutex_unlock(&khf->cache_mtx);
	}
	free(resp);
	return 0;
}

uint64_t kenwood_hf_get_frequency(void *cbdata, enum vfos vfo)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	uint64_t			ret;
	enum kenwood_hf_commands	cmd;
	struct io_response			*resp;

	if (khf == NULL)
		return 0;

	if (vfo == VFO_UNKNOWN) {
		kenwood_update_if(khf, true);
		ret = khf->last_if.freq;
		mutex_unlock(&khf->cache_mtx);
	}
	else {
		switch (vfo) {
			case VFO_A:
				cmd = KW_HF_CMD_FA;
				break;
			case VFO_B:
				cmd = KW_HF_CMD_FB;
				break;
			default:
				return EACCES;
		}
		resp = kenwood_hf_command(khf, false, cmd);
		if (resp == NULL)
			return ENODEV;
		kenwood_rscanf(cmd, resp, &ret);
		free(resp);
	}
	return ret;
}

int kenwood_hf_get_split_frequency(void *cbdata, uint64_t *rx_freq, uint64_t *tx_freq)
{
	struct kenwood_hf			*khf = (struct kenwood_hf *)cbdata;
	enum khf_function			func;
	enum khf_sw					split;
	enum khf_sw					rit_on;
	enum khf_sw					xit_on;
	enum khf_sw					tx;
	int							rit;
	enum kenwood_hf_commands	rx_cmd;
	enum kenwood_hf_commands	tx_cmd;
	struct io_response			*resp;

	if (khf == NULL)
		return EINVAL;

	kenwood_update_if(khf, true);
	func = khf->last_if.function;
	split = khf->last_if.split;
	rit_on = khf->last_if.rit_on;
	xit_on = khf->last_if.xit_on;
	rit = khf->last_if.rit;
	tx = khf->last_if.tx;
	mutex_unlock(&khf->cache_mtx);
	if (split == SW_OFF) {
		if (rit_on == xit_on)
			return EACCES;
	}
	switch(func) {
		case FUNCTION_MEMORY:
		case FUNCTION_COM:
			return EACCES;
		case FUNCTION_VFO_A:
			rx_cmd = KW_HF_CMD_FA;
			tx_cmd = KW_HF_CMD_FB;
			break;
		case FUNCTION_VFO_B:
			rx_cmd = KW_HF_CMD_FB;
			tx_cmd = KW_HF_CMD_FA;
			break;
	}
	if (rx_freq != NULL) {
		resp = kenwood_hf_command(khf, false, rx_cmd);
		if (resp == NULL)
			return ENODEV;
		kenwood_rscanf(rx_cmd, resp, rx_freq);
		free(resp);
		if (rit_on == SW_ON) {
			if (xit_on != SW_ON)
				if (tx == SW_ON)
					rx_freq += rit;
		}
	}
	if (tx_freq != NULL) {
		resp = kenwood_hf_command(khf, false, tx_cmd);
		if (resp == NULL)
			return ENODEV;
		kenwood_rscanf(tx_cmd, resp, tx_freq);
		free(resp);
		if (xit_on == SW_ON) {
			if (rit_on != SW_ON)
				if (tx == SW_OFF)
					tx_freq += rit;
		}
	}
	return 0;
}

int kenwood_hf_set_mode(void *cbdata, enum rig_modes rmode)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	enum khf_mode		mode;
	struct io_response	*resp;

	if (khf == NULL)
		return EINVAL;

	switch(rmode) {
		case MODE_LSB:
			mode = KHF_MODE_LSB;
			break;
		case MODE_USB:
			mode = KHF_MODE_USB;
			break;
		case MODE_CW:
			mode = KHF_MODE_CW;
			break;
		case MODE_FM:
			mode = KHF_MODE_FM;
			break;
		case MODE_AM:
			mode = KHF_MODE_AM;
			break;
		case MODE_FSK:
			mode = KHF_MODE_FSK;
			break;
		case MODE_CWN:
			mode = KHF_MODE_CWN;
			break;
		default:
			return EINVAL;
	}
	resp = kenwood_hf_command(khf, true, KW_HF_CMD_MD, mode);
	if (resp == NULL)
		return ENODEV;
	mutex_lock(&khf->cache_mtx);
	khf->last_if.mode = mode;
	mutex_unlock(&khf->cache_mtx);
	free(resp);
	return 0;
}

enum rig_modes kenwood_hf_get_mode(void *cbdata)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	enum khf_mode		mode;

	kenwood_update_if(khf, true);
	mode = khf->last_if.mode;
	mutex_unlock(&khf->cache_mtx);
	switch (mode) {
		case KHF_MODE_LSB:
			return MODE_LSB;
		case KHF_MODE_USB:
			return MODE_USB;
		case KHF_MODE_CW:
			return MODE_CW;
		case KHF_MODE_FM:
			return MODE_FM;
		case KHF_MODE_AM:
			return MODE_AM;
		case KHF_MODE_FSK:
			return MODE_FSK;
		case KHF_MODE_CWN:
			return MODE_CWN;
		default:
			return MODE_UNKNOWN;
	}
}

int kenwood_hf_set_vfo(void *cbdata, enum vfos vfo)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	enum khf_function	func;
	struct io_response	*resp;

	if (khf == NULL)
		return EINVAL;

	switch(vfo) {
		case VFO_A:
			func = FUNCTION_VFO_A;
			break;
		case VFO_B:
			func = FUNCTION_VFO_B;
			break;
		case VFO_MEMORY:
			func = FUNCTION_MEMORY;
			break;
		case VFO_COM:
			func = FUNCTION_COM;
			break;
		default:
			return EINVAL;
	}
	resp = kenwood_hf_command(khf, true, KW_HF_CMD_FN, func);
	if (resp == NULL)
		return ENODEV;
	/*
	 *  Force next IF to be sent... changing VFOs can toggle a lot of things.
	 * like frequency etc. that we simply don't want to cache.
	 */

	khf->last_if_tick = 0;
	free(resp);
	return 0;
}

enum vfos kenwood_hf_get_vfo(void *cbdata)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	enum khf_function	cvfo;

	kenwood_update_if(khf, true);
	cvfo = khf->last_if.function;
	mutex_unlock(&khf->cache_mtx);
	switch (cvfo) {
		case FUNCTION_VFO_A:
			return VFO_A;
		case FUNCTION_VFO_B:
			return VFO_B;
		case FUNCTION_MEMORY:
			return VFO_MEMORY;
		case FUNCTION_COM:
			return VFO_COM;
		default:
			return VFO_UNKNOWN;
	}
}

int kenwood_hf_set_ptt(void *cbdata, bool tx)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	struct io_response	*resp;

	if (khf == NULL)
		return EINVAL;
	if (tx)
		resp = kenwood_hf_command(khf, true, KW_HF_CMD_TX);
	else
		resp = kenwood_hf_command(khf, true, KW_HF_CMD_RX);
	if (resp == NULL)
		return ENODEV;
	/*
	 *  Force next IF to be sent... toggling PTT can toggle a lot of things.
	 * like frequency etc. that we simply don't want to cache.
	 */
	
	khf->last_if_tick = 0;
	free(resp);
	return 0;
}

int kenwood_hf_get_ptt(void *cbdata)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	enum khf_sw			ret;

	if (khf == NULL)
		return -1;

	kenwood_update_if(khf, true);
	ret = khf->last_if.tx;
	mutex_unlock(&khf->cache_mtx);
	switch (ret) {
		case SW_ON:
			return 1;
		case SW_OFF:
			return 0;
		default:
			return -1;
	}
}

int kenwood_hf_close(void *cbdata)
{
	struct kenwood_hf	*khf = (struct kenwood_hf *)cbdata;
	struct io_response	*resp;
	int					ret;

	if (khf==NULL)
		return EINVAL;
	/*
	 * Most rigs don't support this, so it will fail.
	 * That's OK though.
	 */
	resp = kenwood_hf_command(khf, true, KW_HF_CMD_LO);
	if (resp)
		free(resp);
	resp = kenwood_hf_command(khf, true, KW_HF_CMD_LK, 0);
	if (resp)
		free(resp);
	resp = kenwood_hf_command(khf, true, KW_HF_CMD_AI, 0);
	if (resp)
		free(resp);

	ret = io_end(khf->handle);
	kenwood_hf_free(khf);
	return ret;
}

