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

#ifndef KENWOOD_HF_H
#define KENWOOD_HF_H

#include <inttypes.h>
#include <stdbool.h>

/*
 * These are the commands available for both reading and setting.
 * 
 * When a new format for an existing message is used, there will
 * be a suffix added so that it's clearly listed as a "separate"
 * command.
 */

enum kenwood_hf_commands {
	KW_HF_CMD_AI,		// 0
	KW_HF_CMD_AT1,		// 1
	KW_HF_CMD_DI,		// 2
	KW_HF_CMD_DN,		// 3
	KW_HF_CMD_UP,		// 4
	KW_HF_CMD_DS,		// 5
	KW_HF_CMD_FA,		// 6
	KW_HF_CMD_FB,		// 7
	KW_HF_CMD_FN,		// 1-0
	KW_HF_CMD_HD,		// 1-1
	KW_HF_CMD_ID,		// 1-2
	KW_HF_CMD_IF,		// 1-3
	KW_HF_CMD_LK,
	KW_HF_CMD_LO,
	KW_HF_CMD_MC,
	KW_HF_CMD_MD,
	KW_HF_CMD_MR,
	KW_HF_CMD_MS,
	KW_HF_CMD_MW,
	KW_HF_CMD_OS,
	KW_HF_CMD_RC,
	KW_HF_CMD_RD,
	KW_HF_CMD_RU,
	KW_HF_CMD_RT,
	KW_HF_CMD_RX,
	KW_HF_CMD_TX,
	KW_HF_CMD_SC,
	KW_HF_CMD_SH,
	KW_HF_CMD_SL,
	KW_HF_CMD_SP,
	KW_HF_CMD_ST,
	KW_HF_CMD_TN,
	KW_HF_CMD_TO,
	KW_HF_CMD_VB,
	KW_HF_CMD_VR,
	KW_HF_CMD_XT,

	KW_HF_CMD_COUNT,
	KW_HF_TERMINATOR = KW_HF_CMD_COUNT
};

struct kenwood_if {
	uint64_t			freq;
	unsigned			step;
	int					rit;
	unsigned			rit_on;
	unsigned			xit_on;
	unsigned			bank;
	unsigned			channel;
	unsigned			tx;
	unsigned			mode;
	unsigned			function;
	unsigned			scan;
	unsigned			split;
	unsigned			tone;
	unsigned			tone_freq;
	unsigned			offset;
};

struct kenwood_hf {
	struct io_handle 	*handle;
	unsigned			response_timeout;	// Max time to wait in between responses.
	unsigned			char_timeout;		// Max time to wait in between chars of a response.
	unsigned			send_timeout;		// Max time to wait in between chars while sending.
	unsigned			if_lifetime;		// Time in milliseconds to keep and IF response cached.
	unsigned			inter_cmd_delay;	// Minimum time between commands
	unsigned			additional_intercmd_delay;	// Additional one-shot delay...
	unsigned			set_cmd_delays[KW_HF_CMD_COUNT];	// Additional delay for each command.
	uint64_t			last_cmd_tick;
	char				read_cmds[KW_HF_CMD_COUNT/8+1];
	char				set_cmds[KW_HF_CMD_COUNT/8+1];
	mutex_t				cache_mtx;
	struct kenwood_if	last_if;
	uint64_t			last_if_tick;
	bool				hands_on;
};

#define kenwood_hf_cmd_set(hf, cmd)		((hf->set_cmds[cmd/8] & (1 << (cmd % 8)))?1:0)
#define kenwood_hf_cmd_read(hf, cmd)	((hf->read_cmds[cmd/8] & (1 << (cmd % 8)))?1:0)

int kenwood_hf_init(struct kenwood_hf *khf);
struct io_response *kenwood_hf_read_response(void *cbdata);
void kenwood_hf_handle_extra(void *handle, struct io_response *resp);
void kenwood_hf_setbits(char *array, ...);
void kenwood_hf_set_cmd_delays(struct kenwood_hf *khf, ...);
struct io_response *kenwood_hf_command(struct kenwood_hf *khf, bool set, enum kenwood_hf_commands cmd, ...);
void kenwood_hf_free(struct kenwood_hf *khf);
struct kenwood_hf *kenwood_hf_new(struct _dictionary_ *d, const char *section);

int kenwood_hf_set_frequency(void *cbdata, enum vfos vfo, uint64_t freq);
int kenwood_hf_set_split_frequency(void *cbdata, uint64_t freq_rx, uint64_t freq_tx);
uint64_t kenwood_hf_get_frequency(void *cbdata, enum vfos vfo);
int kenwood_hf_get_split_frequency(void *cbdata, uint64_t *rx_freq, uint64_t *tx_freq);
int kenwood_hf_set_mode(void *khf, enum rig_modes mode);
enum rig_modes kenwood_hf_get_mode(void *khf);
int kenwood_hf_set_vfo(void *cbdata, enum vfos vfo);
enum vfos kenwood_hf_get_vfo(void *cbdata);
int kenwood_hf_set_ptt(void *cbdata, bool tx);
int kenwood_hf_get_ptt(void *cbdata);
int kenwood_hf_close(void *cbdata);

#endif
