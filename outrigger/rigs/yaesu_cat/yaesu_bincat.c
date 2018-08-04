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
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>

#include <api.h>
#include <io.h>
#include <iniparser.h>

#include "yaesu_bincat.h"

enum ybc_param_type {
	YBC_PARAM_BCD,
	YBC_PARAM_ASCII,
	YBC_PARAM_BIGBCD,		// BCD but the high nybble can be hex indicating two decimal digits...
							// a=10, b=11, c=12, d=13, e=14, f=15
							// These are a uint64_t
	YBC_PARAM_ENUM,
	YBC_PARAM_CMD_NYBBLE	// High nybble of the opcode.
};

struct ybc_param {
	const char		name[64];
	const unsigned	nybbles;
	const char		type;
};

enum ybc_mode {
	YBC_MODE_LSB,
	YBC_MODE_USB,
	YBC_MODE_CW,
	YBC_MODE_FM = 8,
	YBC_MODE_CWN = 0x82,
	YBC_MODE_FMN = 0x88,
	YBC_MODE_UNKNOWN
};

enum ybc_params {
	YBC_PARAM_FREQUENCY,
	YBC_PARAM_MODE,
	YBC_PARAM_CTCSS_CODE,
	YBC_PARAM_GROUP_CODE,
	YBC_PARAM_CALLSIGN,
	YBC_PARAM_MESSAGE,
	YBC_PARAM_TONE_MEM
};

struct ybc_param params[] = {
	{"FREQUENCY", 8, YBC_PARAM_BIGBCD},
	{"MODE", 2, YBC_PARAM_BCD},
	{"CTCSS TONE CODE", 2, YBC_PARAM_BCD},
	{"ID CALLSIGN", 16, YBC_PARAM_ASCII},
	{"GROUP CODE", 5, YBC_PARAM_BCD},
	{"ASCII MESSAGE", 28, YBC_PARAM_ASCII},
	{"TONE MEMORY", 1, YBC_PARAM_TONE_MEM},
};

struct ybc_command {
	enum yaesu_bincat_cmds	cmd;
	unsigned char			opcode;
	unsigned				param_count;
	enum ybc_params			params[5];
	unsigned				answer_bytes;
};

struct ybc_command ybc_cmd[] = {
	{Y_BC_CMD_CAT_ON, 0x00, 0, {0}, 0},
	{Y_BC_CMD_CAT_OFF, 0x80, 0, {0}, 0},
	{Y_BC_CMD_FREQUENCY, 0x01, 1, {YBC_PARAM_FREQUENCY}, 0},
	{Y_BC_CMD_MODE, 0x07, 1, {YBC_PARAM_MODE}, 0},
	{Y_BC_CMD_TX, 0x08, 0, {0}, 0},
	{Y_BC_CMD_RX, 0x88, 0, {0}, 0},
	{Y_BC_CMD_SPLIT_PLUS, 0x49, 0, {0}, 0},
	{Y_BC_CMD_SPLIT_MINUS, 0x09, 0, {0}, 0},
	{Y_BC_CMD_SPLIT_OFF, 0x89, 0, {0}, 0},
	{Y_BC_CMD_SPLIT_OFFSET, 0xF9, 1, {YBC_PARAM_FREQUENCY}, 0},
	{Y_BC_CMD_CTCSS_ENCDEC, 0x0A, 0, {0}, 0},
	{Y_BC_CMD_CTCSS_ENC, 0x4A, 0, {0}, 0},
	{Y_BC_CMD_CTCSS_OFF, 0x8A, 0, {0}, 0},
	{Y_BC_CMD_CTCSS_TONE_CODE, 0xFA, 1, {YBC_PARAM_CTCSS_CODE}, 0},
	{Y_BC_CMD_FULL_DUPLEX_ON, 0x0E, 0, {0}, 0},
	{Y_BC_CMD_FULL_DUPLEX_OFF, 0x8E, 0, {0}, 0},
	{Y_BC_CMD_FULL_DUPLEX_RX_MODE, 0x17, 1, {YBC_PARAM_MODE}, 0},
	{Y_BC_CMD_FULL_DUPLEX_TX_MODE, 0x27, 1, {YBC_PARAM_MODE}, 0},
	{Y_BC_CMD_FULL_DUPLEX_RX_FREQ, 0x1E, 1, {YBC_PARAM_FREQUENCY}, 0},
	{Y_BC_CMD_FULL_DUPLEX_TX_FREQ, 0x2E, 1, {YBC_PARAM_FREQUENCY}, 0},
	{Y_BC_CMD_AQS_ON, 0x0B, 0, {0}, 0},
	{Y_BC_CMD_AQS_OFF, 0x8B, 0, {0}, 0},
	{Y_BC_CMD_ID_CALLSIGN_SET, 0x05, 1, {YBC_PARAM_CALLSIGN}, 0},
	{Y_BC_CMD_GROUP_CODE_SET, 0x04, 2, {YBC_PARAM_GROUP_CODE, YBC_PARAM_TONE_MEM}, 0},
	{Y_BC_CMD_CALLSIGN_MEM_SET, 0x05, 2, {YBC_PARAM_CALLSIGN, YBC_PARAM_TONE_MEM}, 0},
	{Y_BC_CMD_CAC_ON, 0x0D, 0, {0}, 0},
	{Y_BC_CMD_CONTROL_FREQ_SET, 0x02, 1, {YBC_PARAM_FREQUENCY}, 0},
	{Y_BC_CMD_COMM_FREQ_SET, 0x03, 1, {YBC_PARAM_FREQUENCY}, 0},
	{Y_BC_CMD_AQS_RESET, 0x8D, 0, {0}, 0},
	{Y_BC_CMD_DIGITAL_SQUELCH_ON, 0x0C, 0, {0}, 0},
	{Y_BC_CMD_DIGITAL_SQUELCH_OFF, 0x8C, 0, {0}, 0},
	{Y_BC_CMD_TEST_SQUELCH, 0xE7, 0, {0}, 1},
	{Y_BC_CMD_TEST_S_METER, 0xF7, 0, {0}, 1}
};

/*
 * Reads five bytes from the serial port
 * and returns a malloc()ed struct io_response *
 */
struct io_response *yaesu_bincat_read_response(void *cbdata)
{
	size_t	retsize = offsetof(struct io_response, msg)+5;
	int		rd;
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;
	struct io_response	*ret = (struct io_response *)malloc(retsize);

	if (ret == NULL)
		return NULL;
	for(ret->len=0; ret->len < 5;) {
		if (io_wait_read(ybc->handle, ybc->response_timeout) != 1)
			goto fail;
		rd = io_read(ybc->handle, ret->msg+ret->len, 1, ybc->char_timeout);
		if (rd != 1)
			goto fail;
		ret->len += rd;
	}
	if (ret->len != 5)
		goto fail;
	return ret;

fail:
	if(ret)
		free(ret);
	return NULL;
}

/*
 * TODO: Build an index
 */
static struct ybc_command *yaesu_bindcat_find_command(enum yaesu_bincat_cmds cmd)
{
	int i;

	for (i=0; ybc_cmd[i].cmd != Y_BC_CMD_COUNT; i++) {
		if (ybc_cmd[i].cmd == cmd)
			return &ybc_cmd[i];
	}
	return NULL;
}

static size_t fill_bcd(char *buf, size_t nybbles, bool big, uint64_t val)
{
	int				i;
	unsigned char	ch;
	char			*b;

	for (i=nybbles; i>0; i--) {
		b = buf + ((i-1)/2);
		if (i%2)
			*b &= 0x0f;
		else
			*b &= 0xf0;
		if (i==1 && big)
			ch = val;
		else
			ch = val % 10;
		val /= 10;
		if (i%2)
			ch <<= 4;
		*b |= ch;
	}
	return nybbles / 2;
}

static uint64_t round_freq(uint64_t freq)
{
	freq += 5;
	freq -= (freq % 10);
	return freq;
}

struct io_response *yaesu_bincat_command(struct yaesu_bincat *ybc, bool set, enum yaesu_bincat_cmds cmd, ...)
{
	char				cmdstr[5];
	unsigned			i;
	int					j;
	va_list				args;
	struct ybc_command	*cmdinfo = yaesu_bindcat_find_command(cmd);
	int					ival;
	unsigned			uval;
	uint64_t			qval;
	char				*strval;
	size_t				len=0;
	size_t				slen;
	unsigned			count;
	enum ybc_params		*par;

	if (cmdinfo == NULL)
		return NULL;
	if (set) {
		if (!yaesu_bincat_cmd_set(ybc, cmd))
			return NULL;
	}
	else {
		if (!yaesu_bincat_cmd_read(ybc, cmd))
			return NULL;
	}

	memset(cmdstr, 0, sizeof(cmdstr));
	count = cmdinfo->param_count;
	par = cmdinfo->params;

	cmdstr[4] = cmdinfo->opcode;
	va_start(args, cmd);
	for(i=0; i<count; i++) {
		switch(params[par[i]].type) {
			case YBC_PARAM_BCD:
				ival = va_arg(args, int);
				len += fill_bcd(cmdstr+len, params[par[i]].nybbles, false, ival);
				break;
			case YBC_PARAM_BIGBCD:
				qval = va_arg(args, uint64_t);
				len += fill_bcd(cmdstr+len, params[par[i]].nybbles, true, qval);
				break;
			case YBC_PARAM_ENUM:
				ival = va_arg(args, int);
				if (ival < 0)
					break;
				uval = ival;
				for (j=params[par[i]].nybbles/2-1; j >= 0; j--) {
					cmdstr[len+j] = (uval & 0xff);
					uval >>= 8;
				}
				len += params[par[i]].nybbles/2;
				break;
			case YBC_PARAM_ASCII:
				strval = va_arg(args, char *);
				slen = strlen(strval);
				for (j=0; j<params[par[i]].nybbles; j+=2) {
					if (*strval) {
						cmdstr[len++] = *strval;
						strval++;
					}
					else
						cmdstr[len++] = ' ';
				}
				break;
			case YBC_PARAM_CMD_NYBBLE:
				ival = va_arg(args, int);
				cmdstr[4] |= (ival << 4);
				break;
		}
	}
	va_end(args);
	if (set) {
		struct io_response *resp = (struct io_response *)malloc(offsetof(struct io_response, msg));

		if (resp != NULL)
			resp->len = io_write(ybc->handle, cmdstr, sizeof(cmdstr), ybc->char_timeout);
		return resp;
	}
	if (io_write(ybc->handle, cmdstr, sizeof(cmdstr), ybc->char_timeout) != 5)
		return NULL;
	return io_get_response(ybc->handle, NULL, 0, 0);
}

/*
 * This handles any "extra" responses recieved
 * ie: AQS messages
 * 
 * Any lock may be held, so MUST NOT lock or post semaphores.
 */
void yaesu_bincat_handle_extra(void *handle, struct io_response *resp)
{
	if (resp==NULL)
		return;

	return;
}

struct yaesu_bincat *yaesu_bincat_new(struct _dictionary_ *d, const char *section)
{
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)calloc(1, sizeof(struct yaesu_bincat));

	if (ybc == NULL || d == NULL)
		return NULL;
	/*
	 * Set up some reasonable defaults to be shared among ALL rigs
	 */
	ybc->response_timeout = getint(d, section, "response_timeout", 1000);
	ybc->char_timeout = getint(d, section, "char_timeout", 50);
	ybc->send_timeout = getint(d, section, "send_timeout", 500);

	return ybc;
}

int yaesu_bincat_init(struct yaesu_bincat *ybc)
{
	struct io_response		*resp;

	// Enter CAT mode
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_CAT_ON);
	if (resp)
		free(resp);
	else
		return -1;
	return 0;
}

void yaesu_bincat_free(struct yaesu_bincat *ybc)
{
	if (ybc == NULL)
		return;
	free(ybc);
}

void yaesu_bincat_setbits(char *array, ...)
{
	va_list					bits;
	enum yaesu_bincat_cmds	bit;

	va_start(bits, array);
	for (bit = va_arg(bits, int); bit != Y_BC_TERMINATOR; bit = va_arg(bits, int)) {
		array[bit/8] |= (1<<(bit%8));
	}
	va_end(bits);
}

int yaesu_bincat_set_frequency(void *cbdata, enum vfos vfo, uint64_t freq)
{
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;
	struct io_response	*resp;

	freq = round_freq(freq);
	if (ybc->duplex_rx || ybc->duplex_tx) {
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FULL_DUPLEX_OFF);
		if (resp == NULL)
			return ENODEV;
		ybc->duplex_rx = ybc->duplex_tx = 0;
		free(resp);
	}
	if (ybc->split_offset) {
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_SPLIT_OFF);
		if (resp == NULL)
			return ENODEV;
		free(resp);
	}
	/* TODO: No VFO control... always set current VFO. */
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FREQUENCY, freq/10);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	ybc->freq = freq;
	ybc->split_offset = 0;
	return 0;
}

int yaesu_bincat_set_split_frequency(void *cbdata, uint64_t freq_rx, uint64_t freq_tx)
{
	struct yaesu_bincat	*ybc = (struct yaesu_bincat *)cbdata;
	struct io_response	*resp;

	freq_rx = round_freq(freq_rx);
	freq_tx = round_freq(freq_tx);
	if (ybc->duplex_rx || ybc->duplex_tx) {
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FULL_DUPLEX_OFF);
		if (resp == NULL)
			return ENODEV;
		ybc->duplex_rx = ybc->duplex_tx = 0;
		free(resp);
	}
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FREQUENCY, freq_rx/10);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	if (freq_tx < freq_rx) {
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_SPLIT_OFFSET, (freq_rx - freq_tx)/10);
		if (resp == NULL)
			return ENODEV;
		free(resp);
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_SPLIT_MINUS);
		if (resp == NULL)
			return ENODEV;
		free(resp);
	}
	else {
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_SPLIT_OFFSET, (freq_tx - freq_rx)/10);
		if (resp == NULL)
			return ENODEV;
		free(resp);
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_SPLIT_PLUS);
		if (resp == NULL)
			return ENODEV;
		free(resp);
	}
	ybc->freq = freq_rx;
	ybc->split_offset = freq_rx - freq_tx;
	return 0;
}

static enum ybc_mode get_yaesu_bincat_mode(enum rig_modes mode)
{
	switch(mode) {
		case MODE_CW:
			return YBC_MODE_CW;
		case MODE_CWN:
			return YBC_MODE_CWN;
		case MODE_FM:
			return YBC_MODE_FM;
		case MODE_FMN:
			return YBC_MODE_FMN;
		case MODE_LSB:
			return YBC_MODE_LSB;
		case MODE_USB:
			return YBC_MODE_USB;
		default:
			return YBC_MODE_UNKNOWN;
	}
}

static enum rig_modes yaesu_bincat_mode_get_rig_mode(enum ybc_mode mode)
{
	switch(mode) {
		case YBC_MODE_CW:
			return MODE_CW;
		case YBC_MODE_CWN:
			return MODE_CWN;
		case YBC_MODE_FM:
			return MODE_FM;
		case YBC_MODE_FMN:
			return MODE_FMN;
		case YBC_MODE_LSB:
			return MODE_LSB;
		case YBC_MODE_USB:
			return MODE_USB;
		default:
			return MODE_UNKNOWN;
	}
}

int yaesu_bincat_set_duplex(void *cbdata, uint64_t freq_rx, enum rig_modes mode_rx, uint64_t freq_tx, enum rig_modes mode_tx)
{
	struct yaesu_bincat	*ybc = (struct yaesu_bincat *)cbdata;
	struct io_response	*resp;
	enum ybc_mode		tx_mode = get_yaesu_bincat_mode(mode_tx);
	enum ybc_mode		rx_mode = get_yaesu_bincat_mode(mode_rx);

	freq_rx = round_freq(freq_rx);
	freq_tx = round_freq(freq_tx);

	if (ybc->split_offset) {
		resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_SPLIT_OFF);
		if (resp == NULL)
			return ENODEV;
		free(resp);
	}
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FULL_DUPLEX_RX_MODE, rx_mode);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FULL_DUPLEX_TX_MODE, tx_mode);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FULL_DUPLEX_RX_FREQ, freq_rx);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FULL_DUPLEX_TX_FREQ, freq_tx);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_FULL_DUPLEX_ON);
	if (resp == NULL)
		return ENODEV;
	free(resp);
	ybc->freq = freq_rx;
	ybc->split_offset = 0;
	ybc->duplex_tx = freq_tx;
	ybc->duplex_rx = freq_rx;
	ybc->duplex_tx_mode = tx_mode;
	ybc->duplex_rx_mode = rx_mode;
	return 0;
}

int yaesu_bincat_set_mode(void *cbdata, enum rig_modes mode)
{
	struct yaesu_bincat	*ybc = (struct yaesu_bincat *)cbdata;
	struct io_response	*resp;
	enum ybc_mode		ymode;

	switch(mode) {
		case MODE_CW:
			ymode = YBC_MODE_CW;
			break;
		case MODE_CWN:
			ymode = YBC_MODE_CWN;
			break;
		case MODE_LSB:
			ymode = YBC_MODE_LSB;
			break;
		case MODE_USB:
			ymode = YBC_MODE_USB;
			break;
		case MODE_FM:
			ymode = YBC_MODE_FM;
			break;
		case MODE_FMN:
			ymode = YBC_MODE_FMN;
			break;
		default:
			return ENOTSUP;
	}
	resp = yaesu_bincat_command(ybc, true, Y_BC_CMD_MODE, ymode);
	if (resp==NULL)
		return ENODEV;
	free(resp);
	ybc->mode = ymode;
	return 0;
}

int yaesu_bincat_set_ptt(void *cbdata, bool tx)
{
	struct yaesu_bincat		*ybc = (struct yaesu_bincat *)cbdata;
	struct io_response		*resp;
	enum yaesu_bincat_cmds	cmd;

	if (tx)
		cmd = Y_BC_CMD_TX;
	else
		cmd = Y_BC_CMD_RX;
	resp = yaesu_bincat_command(ybc, true, cmd);
	if (resp==NULL)
		return ENODEV;
	free(resp);
	ybc->ptt = tx;
	return 0;
}

uint64_t yaesu_bincat_get_frequency(void *cbdata, enum vfos vfo)
{
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;

	return ybc->freq;
}

int yaesu_bincat_get_split_frequency(void *cbdata, uint64_t *rx_freq, uint64_t *tx_freq)
{
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;

	if (ybc->split_offset == 0)
		return EACCES;
	if (rx_freq)
		*rx_freq = ybc->freq;
	if (tx_freq)
		*tx_freq = ybc->freq + ybc->split_offset;
	return 0;
}

int yaesu_bincat_get_duplex(void *cbdata, uint64_t *rx_freq, enum rig_modes *rx_mode, uint64_t *tx_freq, enum rig_modes *tx_mode)
{
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;

	if (ybc->duplex_rx == 0 || ybc->duplex_tx == 0)
		return EACCES;
	*rx_freq = ybc->duplex_rx;
	*tx_freq = ybc->duplex_tx;
	*rx_mode = yaesu_bincat_mode_get_rig_mode(ybc->duplex_rx_mode);
	*tx_mode = yaesu_bincat_mode_get_rig_mode(ybc->duplex_tx_mode);
	return 0;
}

enum rig_modes yaesu_bincat_get_mode(void *cbdata)
{
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;

	switch(ybc->mode) {
		case YBC_MODE_CW:
			return MODE_CW;
		case YBC_MODE_CWN:
			return MODE_CWN;
		case YBC_MODE_LSB:
			return MODE_LSB;
		case YBC_MODE_USB:
			return MODE_USB;
		case YBC_MODE_FM:
			return MODE_FM;
		case YBC_MODE_FMN:
			return MODE_FMN;
		default:
			return MODE_UNKNOWN;
	}
}

int yaesu_bincat_get_ptt(void *cbdata)
{
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;

	if (ybc->ptt)
		return 1;
	return 0;
}

int yaesu_bincat_get_squelch(void *cbdata)
{
	struct io_response	*resp;
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;

	resp = yaesu_bincat_command(ybc, false, Y_BC_CMD_TEST_SQUELCH);
	if (resp == NULL)
		return -1;
	if (resp->msg[1] & 0x80) {
		free(resp);
		return 1;
	}
	free(resp);
	return 0;
}

int yaesu_bincat_get_smeter(void *cbdata)
{
	struct io_response	*resp;
	struct yaesu_bincat *ybc = (struct yaesu_bincat *)cbdata;
	int					ret;

	if (ybc->ptt)
		return 0;
	resp = yaesu_bincat_command(ybc, false, Y_BC_CMD_TEST_S_METER);
	if (resp == NULL)
		return -1;
	ret = ((unsigned char)resp->msg[1])-0x20;
	free(resp);
	if (ret < 0)
		ret = 0;
	return ret;
}
