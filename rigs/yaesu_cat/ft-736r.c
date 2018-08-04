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

#include "yaesu_bincat.h"

static int ft736r_close(void *cbdata)
{
	struct yaesu_bincat	*ybc = (struct yaesu_bincat *)cbdata;
	struct io_response	*resp;
	int					ret;

	if (ybc==NULL)
		return EINVAL;
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_CAT_OFF);
	if (resp)
		free(resp);

	ret = io_end(ybc->handle);
	yaesu_bincat_free(ybc);
	return ret;
}

struct rig	*ft736r_init(struct _dictionary_ *d, const char *section)
{
	struct rig			*ret = (struct rig *)calloc(1, sizeof(struct rig));
	struct yaesu_bincat	*ybc;

	if (ret == NULL)
		return NULL;

	// Fill in serial port defaults
	set_default(d, section, "type", "serial");
	set_default(d, section, "speed", "4800");
	set_default(d, section, "databits", "8");
	set_default(d, section, "stopbits", "2");
	set_default(d, section, "parity", "None");
	set_default(d, section, "flow", "CTSRTS");
	set_default(d, section, "rx_bandlimit_low_2m", "144000000");
	set_default(d, section, "rx_bandlimit_high_2m", "147999990");
	set_default(d, section, "rx_bandlimit_low_70cm", "430000000");
	set_default(d, section, "rx_bandlimit_high_70cm", "449999990");
	set_default(d, section, "tx_bandlimit_low_2m", "144000000");
	set_default(d, section, "tx_bandlimit_high_2m", "147999990");
	set_default(d, section, "tx_bandlimit_low_70cm", "430000000");
	set_default(d, section, "tx_bandlimit_high_70cm", "449999990");

	ybc = yaesu_bincat_new(d, section);
	if (ybc == NULL) {
		free(ret);
		return NULL;
	}
	ret->supported_modes = MODE_CW | MODE_CWN | 
			MODE_LSB | MODE_USB | MODE_FM | MODE_FMN;
	ret->supported_vfos = VFO_A|VFO_MAIN|VFO_SUB;
	ret->close = ft736r_close;
	ret->set_frequency = yaesu_bincat_set_frequency;
	ret->get_frequency = yaesu_bincat_get_frequency;
	ret->set_split_frequency = yaesu_bincat_set_split_frequency;
	ret->get_split_frequency = yaesu_bincat_get_split_frequency;
	ret->set_duplex = yaesu_bincat_set_duplex;
	ret->get_duplex = yaesu_bincat_get_duplex;
	ret->set_mode = yaesu_bincat_set_mode;
	ret->get_mode = yaesu_bincat_get_mode;
	ret->set_ptt = yaesu_bincat_set_ptt;
	ret->get_ptt = yaesu_bincat_get_ptt;
	ret->get_squelch = yaesu_bincat_get_squelch;
	ret->get_smeter = yaesu_bincat_get_smeter;
	ret->cbdata = ybc;
	yaesu_bincat_setbits(ybc->set_cmds, Y_BC_CMD_CAT_ON, 
		Y_BC_CMD_CAT_OFF, Y_BC_CMD_FREQUENCY, Y_BC_CMD_MODE, Y_BC_CMD_TX,
		Y_BC_CMD_RX, Y_BC_CMD_SPLIT_PLUS, Y_BC_CMD_SPLIT_MINUS,
		Y_BC_CMD_SPLIT_OFF, Y_BC_CMD_SPLIT_OFFSET, Y_BC_CMD_CTCSS_ENCDEC,
		Y_BC_CMD_CTCSS_ENC, Y_BC_CMD_CTCSS_OFF, Y_BC_CMD_CTCSS_TONE_CODE,
		Y_BC_CMD_FULL_DUPLEX_ON, Y_BC_CMD_FULL_DUPLEX_OFF,
		Y_BC_CMD_FULL_DUPLEX_RX_MODE, Y_BC_CMD_FULL_DUPLEX_TX_MODE,
		Y_BC_CMD_FULL_DUPLEX_RX_FREQ, Y_BC_CMD_FULL_DUPLEX_TX_FREQ,
		Y_BC_CMD_AQS_ON, Y_BC_CMD_AQS_OFF, Y_BC_CMD_ID_CALLSIGN_SET,
		Y_BC_CMD_GROUP_CODE_SET, Y_BC_CMD_CALLSIGN_MEM_SET,
		Y_BC_CMD_CAC_ON, Y_BC_CMD_CONTROL_FREQ_SET,
		Y_BC_CMD_COMM_FREQ_SET, Y_BC_CMD_AQS_RESET,
		Y_BC_CMD_DIGITAL_SQUELCH_ON, Y_BC_CMD_DIGITAL_SQUELCH_OFF,
		Y_BC_TERMINATOR
	);
	yaesu_bincat_setbits(ybc->read_cmds, Y_BC_CMD_TEST_SQUELCH,
		Y_BC_CMD_TEST_S_METER, Y_BC_TERMINATOR);

	ybc->handle=io_start_from_dictionary(d, section, IO_H_SERIAL, yaesu_bincat_read_response, yaesu_bincat_handle_extra, ybc);
	if (ybc->handle == NULL) {
		free(ybc);
		free(ret);
		return NULL;
	}
	if (yaesu_bincat_init(ybc) != 0) {
		ft736r_close(ybc);
		return NULL;
	}
	// Force split/duplex off
	ybc->split_offset = 1;
	ybc->duplex_rx = 1;
	if (yaesu_bincat_set_frequency(ybc, VFO_UNKNOWN, 144000000) != 0) {
		ft736r_close(ybc);
		return NULL;
	}
	if (yaesu_bincat_set_mode(ybc, MODE_FM) != 0) {
		ft736r_close(ybc);
		return NULL;
	}
	if (yaesu_bincat_set_ptt(ybc, false) != 0) {
		ft736r_close(ybc);
		return NULL;
	}

	return ret;
}
