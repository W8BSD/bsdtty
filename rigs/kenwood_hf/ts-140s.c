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

#include <api.h>
#include <iniparser.h>
#include <io.h>
#include <serial.h>

#include "kenwood_hf.h"

struct rig	*ts140s_init(struct _dictionary_ *d, const char *section)
{
	struct rig				*ret = (struct rig *)calloc(1, sizeof(struct rig));
	struct kenwood_hf		*khf;
	char					*rig_name;

	if (ret == NULL)
		return NULL;

	rig_name = getstring(d, section, "rig", NULL);
	if (rig_name == NULL)
		return NULL;

	// Fill in serial port defaults
	set_default(d, section, "type", "serial");
	set_default(d, section, "speed", "4800");
	set_default(d, section, "databits", "8");
	set_default(d, section, "stopbits", "2");
	set_default(d, section, "parity", "None");
	set_default(d, section, "flow", "CTSRTS");
	set_default(d, section, "rx_bandlimit_low_hf", "500000");
	set_default(d, section, "rx_bandlimit_high_hf", "30000000");
	set_default(d, section, "tx_bandlimit_low_160m", "1800000");
	set_default(d, section, "tx_bandlimit_high_160m", "2000000");
	set_default(d, section, "tx_bandlimit_low_80m", "3500000");
	set_default(d, section, "tx_bandlimit_high_80m", "4000000");
	set_default(d, section, "tx_bandlimit_low_40m", "7000000");
	set_default(d, section, "tx_bandlimit_high_40m", "7300000");
	set_default(d, section, "tx_bandlimit_low_30m", "10100000");
	set_default(d, section, "tx_bandlimit_high_30m", "10150000");
	set_default(d, section, "tx_bandlimit_low_20m", "14000000");
	set_default(d, section, "tx_bandlimit_high_20m", "14350000");
	set_default(d, section, "tx_bandlimit_low_17m", "18068000");
	set_default(d, section, "tx_bandlimit_high_17m", "18168000");
	set_default(d, section, "tx_bandlimit_low_15m", "21000000");
	set_default(d, section, "tx_bandlimit_high_15m", "21450000");
	set_default(d, section, "tx_bandlimit_low_12m", "24890000");
	set_default(d, section, "tx_bandlimit_high_12m", "24990000");
	set_default(d, section, "tx_bandlimit_low_10m", "28000000");
	set_default(d, section, "tx_bandlimit_high_10m", "29700000");
	if (strcmp(rig_name, "TS-680S") == 0) {
		set_default(d, section, "rx_bandlimit_low_6m", "50000000");
		set_default(d, section, "rx_bandlimit_high_6m", "54000000");
		set_default(d, section, "tx_bandlimit_low_6m", "50000000");
		set_default(d, section, "tx_bandlimit_high_6m", "54000000");
	}

	khf = kenwood_hf_new(d, section);
	if (khf == NULL) {
		free(ret);
		return NULL;
	}
	ret->supported_modes = MODE_CW | MODE_CWN | MODE_AM |
			MODE_LSB | MODE_USB | MODE_FM;
	ret->supported_vfos = VFO_A | VFO_B | VFO_MEMORY;
	ret->close = kenwood_hf_close;
	ret->set_frequency = kenwood_hf_set_frequency;
	ret->get_frequency = kenwood_hf_get_frequency;
	ret->set_split_frequency = kenwood_hf_set_split_frequency;
	ret->get_split_frequency = kenwood_hf_get_split_frequency;
	ret->set_mode = kenwood_hf_set_mode;
	ret->get_mode = kenwood_hf_get_mode;
	ret->set_vfo = kenwood_hf_set_vfo;
	ret->get_vfo = kenwood_hf_get_vfo;
	ret->set_ptt = kenwood_hf_set_ptt;
	ret->get_ptt = kenwood_hf_get_ptt;
	ret->cbdata = khf;
	kenwood_hf_setbits(khf->set_cmds, KW_HF_CMD_AI,
			KW_HF_CMD_DN, KW_HF_CMD_UP, KW_HF_CMD_FA,
			KW_HF_CMD_FB, KW_HF_CMD_FN, KW_HF_CMD_LK,
			KW_HF_CMD_MC, KW_HF_CMD_MD,
			KW_HF_CMD_MW, KW_HF_CMD_RC, KW_HF_CMD_RD, KW_HF_CMD_RU,
			KW_HF_CMD_RT, KW_HF_CMD_RX, KW_HF_CMD_TX, KW_HF_CMD_SC,
			KW_HF_CMD_SP, KW_HF_TERMINATOR);
	kenwood_hf_setbits(khf->read_cmds, KW_HF_CMD_FA,
			KW_HF_CMD_FB, KW_HF_CMD_ID, KW_HF_CMD_IF,
			KW_HF_CMD_LK, KW_HF_CMD_MR, KW_HF_TERMINATOR);

	khf->handle=io_start_from_dictionary(d, section, IO_H_SERIAL, kenwood_hf_read_response, kenwood_hf_handle_extra, khf);
	if (khf->handle == NULL) {
		free(khf);
		free(ret);
		return NULL;
	}
	/* TODO: Taken from TS-940S... should be verified. */
	kenwood_hf_set_cmd_delays(khf,
			KW_HF_CMD_FA, 200,
			KW_HF_CMD_FB, 200,
			KW_HF_CMD_SP, 200,
	KW_HF_TERMINATOR);
	if (kenwood_hf_init(khf) != 0) {
		kenwood_hf_close(khf);
		return NULL;
	}
	return ret;
}
