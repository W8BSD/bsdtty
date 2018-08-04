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

#ifndef YAESU_BINCAT_H
#define YAESU_BINCAT_H

enum yaesu_bincat_cmds {
	Y_BC_CMD_CAT_ON,				// CAT on/off... this is lock in newer versions.
	Y_BC_CMD_CAT_OFF,
	Y_BC_CMD_FREQUENCY,
	Y_BC_CMD_MODE,
	Y_BC_CMD_TX,
	Y_BC_CMD_RX,
	Y_BC_CMD_SPLIT_PLUS,
	Y_BC_CMD_SPLIT_MINUS,
	Y_BC_CMD_SPLIT_OFF,
	Y_BC_CMD_SPLIT_OFFSET,
	Y_BC_CMD_CTCSS_ENCDEC,
	Y_BC_CMD_CTCSS_ENC,
	Y_BC_CMD_CTCSS_OFF,
	Y_BC_CMD_CTCSS_TONE_CODE,
	Y_BC_CMD_FULL_DUPLEX_ON,
	Y_BC_CMD_FULL_DUPLEX_OFF,
	Y_BC_CMD_FULL_DUPLEX_RX_MODE,
	Y_BC_CMD_FULL_DUPLEX_TX_MODE,
	Y_BC_CMD_FULL_DUPLEX_RX_FREQ,
	Y_BC_CMD_FULL_DUPLEX_TX_FREQ,
	Y_BC_CMD_AQS_ON,
	Y_BC_CMD_AQS_OFF,
	Y_BC_CMD_ID_CALLSIGN_SET,
	Y_BC_CMD_GROUP_CODE_SET,
	Y_BC_CMD_CALLSIGN_MEM_SET,
	Y_BC_CMD_CAC_ON,
	Y_BC_CMD_CONTROL_FREQ_SET,
	Y_BC_CMD_COMM_FREQ_SET,
	Y_BC_CMD_AQS_RESET,
	Y_BC_CMD_DIGITAL_SQUELCH_ON,
	Y_BC_CMD_DIGITAL_SQUELCH_OFF,
	Y_BC_CMD_TEST_SQUELCH,			// For rigs that ONLY set the squelch bit.
	Y_BC_CMD_TEST_S_METER,			// For rigs that ONLY set the S-meter reating.
	
	Y_BC_CMD_COUNT,
	Y_BC_TERMINATOR = Y_BC_CMD_COUNT
};

struct yaesu_bincat {
	struct io_handle 	*handle;
	unsigned			response_timeout;	// Max time to wait in between responses.
	unsigned			char_timeout;		// Max time to wait in between chars of a response.
	unsigned			send_timeout;		// Max time to wait in between chars while sending.
	char				read_cmds[Y_BC_CMD_COUNT/8+1];
	char				set_cmds[Y_BC_CMD_COUNT/8+1];
	bool				hands_on;

	uint64_t			freq;
	uint64_t			duplex_rx;
	uint64_t			duplex_tx;
	uint64_t			duplex_rx_mode;
	uint64_t			duplex_tx_mode;
	unsigned			mode;
	unsigned			split_offset;
	bool				ptt;
};

#define yaesu_bincat_cmd_set(ybc, cmd)		((ybc->set_cmds[cmd/8] & (1 << (cmd % 8)))?1:0)
#define yaesu_bincat_cmd_read(ybc, cmd)		((ybc->read_cmds[cmd/8] & (1 << (cmd % 8)))?1:0)

int yaesu_bincat_init(struct yaesu_bincat *ybc);
struct io_response *yaesu_bincat_read_response(void *cbdata);
void yaesu_bincat_handle_extra(void *handle, struct io_response *resp);
void yaesu_bincat_setbits(char *array, ...);
struct io_response *yaesu_bincat_command(struct yaesu_bincat *ybc, bool set, enum yaesu_bincat_cmds cmd, ...);
void yaesu_bincat_free(struct yaesu_bincat *ybc);
struct yaesu_bincat *yaesu_bincat_new(struct _dictionary_ *d, const char *section);

int yaesu_bincat_set_frequency(void *cbdata, enum vfos vfo, uint64_t freq);
int yaesu_bincat_set_split_frequency(void *cbdata, uint64_t freq_rx, uint64_t freq_tx);
int yaesu_bincat_set_duplex(void *cbdata, uint64_t freq_rx, enum rig_modes mode_rx, uint64_t freq_tx, enum rig_modes mode_tx);
uint64_t yaesu_bincat_get_frequency(void *cbdata, enum vfos vfo);
int yaesu_bincat_get_split_frequency(void *cbdata, uint64_t *rx_freq, uint64_t *tx_freq);
int yaesu_bincat_get_duplex(void *cbdata, uint64_t *rx_freq, enum rig_modes *rx_mode, uint64_t *tx_freq, enum rig_modes *tx_mode);
int yaesu_bincat_set_mode(void *ybc, enum rig_modes mode);
enum rig_modes yaesu_bincat_get_mode(void *ybc);
int yaesu_bincat_set_ptt(void *cbdata, bool tx);
int yaesu_bincat_get_ptt(void *cbdata);
int yaesu_bincat_get_squelch(void *cbdata);
int yaesu_bincat_get_smeter(void *cbdata);

#endif
