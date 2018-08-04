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

#include <sockets.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <netinet/tcp.h>
#ifdef WITH_SIGNAL
#include <signal.h>
#endif

#include <api.h>
#include <iniparser.h>

struct rig_entry {
	struct rig			*rig;
	struct rig_entry	*next_rig_entry;
	struct rig_entry	*prev_rig_entry;
};
struct rig_entry	*rigs = NULL;

struct listener {
	struct rig		*rig;
	int				socket;
	struct listener	*next_listener;
	struct listener	*prev_listener;
};
struct listener		*listeners = NULL;

struct connection {
	int					socket;
	struct rig			*rig;
	char				*rx_buf;
	size_t				rx_buf_size;
	size_t				rx_buf_pos;
	char				*tx_buf;
	size_t				tx_buf_size;
	size_t				tx_buf_pos;
	size_t				tx_buf_terminator;
	struct connection	*next_connection;
	struct connection	*prev_connection;
	enum vfos			current_vfo;
	uint64_t			vfoa_freq;
	uint64_t			vfob_freq;
	uint64_t			vfom_freq;
	uint64_t			vfos_freq;
	bool				split;
	enum rig_modes		vfoa_mode;
	enum rig_modes		vfob_mode;
	enum rig_modes		vfom_mode;
	enum rig_modes		vfos_mode;
};
struct connection	*connections = NULL;

struct long_cmd {
	const char	*lng;
	char		shrt;
	size_t		len;
};
struct long_cmd long_cmds[] = {
	{"\\set_split_freq", 'I', 15},
	{"\\get_split_freq", 'i', 15},
	{"\\set_split_mode", 'X', 15},
	{"\\get_split_mode", 'x', 15},
	{"\\set_rptr_shift", 'R', 15},
	{"\\get_rptr_shift", 'r', 15},
	{"\\set_ctcss_tone", 'C', 15},
	{"\\get_ctcss_tone", 'c', 15},
	{"\\set_rptr_offs", 'O', 14},
	{"\\get_rptr_offs", 'o', 14},
	{"\\set_split_vfo", 'S', 14},
	{"\\get_split_vfo", 's', 14},
	{"\\set_ctcss_sql", '\x90', 14},
	{"\\get_ctcss_sql", '\x91', 14},
	{"\\set_powerstat", '\x87', 14},
	{"\\get_powerstat", '\x88', 14},
	{"\\set_dcs_code", 'D', 13},
	{"\\get_dcs_code", 'd', 13},
	{"\\set_dcs_sql", '\x92', 12},
	{"\\get_dcs_sql", '\x93', 12},
	{"\\set_channel", 'H', 12},
	{"\\get_channel", 'h', 12},
	{"\\send_morse", 'b', 11},
	{"\\dump_state", '\x8f', 11},
	{"\\set_level", 'L', 10},
	{"\\get_level", 'l', 10},
	{"\\send_dtmf", '\x89', 10},
	{"\\recv_dtmf", '\x8a', 10},
	{"\\dump_caps", '1', 10},
	{"\\dump_conf", '3', 10},
	{"\\set_freq", 'F', 9},
	{"\\get_freq", 'f', 9},
	{"\\set_mode", 'M', 9},
	{"\\get_mode", 'm', 9},
	{"\\set_func", 'U', 9},
	{"\\get_func", 'u', 9},
	{"\\set_parm", 'P', 9},
	{"\\get_parm", 'p', 9},
	{"\\set_bank", 'B', 9},
	{"\\get_info", '_', 9},
	{"\\send_cmd", 'w', 9},
	{"\\power2mW", '2', 9},
	{"\\mW2power", '4', 9},
	{"\\set_trn", 'A', 8},
	{"\\get_trn", 'a', 8},
	{"\\set_rit", 'J', 8},
	{"\\get_rit", 'j', 8},
	{"\\set_xit", 'Z', 8},
	{"\\get_xit", 'z', 8},
	{"\\set_ant", 'Y', 8},
	{"\\get_ant", 'y', 8},
	{"\\get_dcd", '\x8b', 8},
	{"\\chk_vfo", '\xf0', 8},
	{"\\set_vfo", 'V', 8},
	{"\\get_vfo", 'v', 8},
	{"\\set_ptt", 'T', 8},
	{"\\get_ptt", 't', 8},
	{"\\set_mem", 'E', 8},
	{"\\get_mem", 'e', 8},
	{"\\set_ts", 'N', 7},
	{"\\get_ts", 'n', 7},
	{"\\vfo_op", 'G', 7},
	{"\\reset", '*', 6},
	{"\\scan", 'g', 5},
	{"\\halt", '\xf1', 5},
	{NULL, 0}
};

#define debug 1

void save_freq(struct connection *c, uint64_t freq, enum vfos vfo)
{
	switch(vfo) {
		case VFO_A:
			c->vfoa_freq = freq;
			break;
		case VFO_B:
			c->vfob_freq = freq;
			break;
		case VFO_MAIN:
			c->vfom_freq = freq;
			break;
		case VFO_SUB:
			c->vfos_freq = freq;
			break;
		default:
			break;
	}
}

void save_mode(struct connection *c, enum rig_modes mode, enum vfos vfo)
{
	switch(vfo) {
		case VFO_A:
			c->vfoa_mode = mode;
			break;
		case VFO_B:
			c->vfob_mode = mode;
			break;
		case VFO_MAIN:
			c->vfom_mode = mode;
			break;
		case VFO_SUB:
			c->vfos_mode = mode;
			break;
		default:
			break;
	}
}

uint64_t current_freq(struct connection *c, enum vfos vfo)
{
	uint64_t	freq = 0;

	switch(vfo) {
		case VFO_A:
			freq = c->vfoa_freq;
			break;
		case VFO_B:
			freq = c->vfob_freq;
			break;
		case VFO_MAIN:
			freq = c->vfom_freq;
			break;
		case VFO_SUB:
			freq = c->vfos_freq;
			break;
		default:
			break;
	}
	return freq;
}

enum vfos paired_vfo(enum vfos vfo)
{
	switch(vfo) {
		case VFO_A:
			return VFO_B;
		case VFO_B:
			return VFO_A;
		case VFO_MAIN:
			return VFO_SUB;
		case VFO_SUB:
			return VFO_MAIN;
		default:
			return vfo;
	}
}

enum rig_modes current_mode(struct connection *c, enum vfos vfo)
{
	enum rig_modes	mode = MODE_UNKNOWN;

	switch(vfo) {
		case VFO_A:
			mode = c->vfoa_mode;
			break;
		case VFO_B:
			mode = c->vfob_mode;
			break;
		case VFO_MAIN:
			mode = c->vfom_mode;
			break;
		case VFO_SUB:
			mode = c->vfos_mode;
			break;
		default:
			break;
	}
	return mode;
}

void replace_cmd(char *str)
{
	int		i;

	for(i=0; long_cmds[i].lng; i++) {
		if (strncmp(long_cmds[i].lng, str, long_cmds[i].len) == 0) {
			memmove(str+1, str+long_cmds[i].len, strlen(str+long_cmds[i].len) + 1);
			*str = long_cmds[i].shrt;
			return;
		}
	}
}

void shorten_cmds(char *str)
{
	char	*lng;

	while ((lng = strchr(str, '\\')) != NULL)
		replace_cmd(lng);
}

int add_rig(dictionary *d, char *section)
{
	char				*port;
	char				*addr;
	struct addrinfo		hints, *res, *res0;
	int					listener_count = 0;
	struct rig_entry	*entry;
	struct listener		*listener;

	addr = getstring(d, section, "rigctld_address", NULL);
	if (addr == NULL)
		return 0;
	port = getstring(d, section, "rigctld_port", "4532");
	entry = (struct rig_entry *)calloc(1, sizeof(struct rig_entry));
	if (entry == NULL)
		return 0;
	entry->rig = init_rig(d, section);
	if (entry->rig == NULL) {
		free(entry);
		return 0;
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_ADDRCONFIG|AI_PASSIVE;
	if (getaddrinfo(addr, port, &hints, &res0) != 0) {
		close_rig(entry->rig);
		free(entry);
		return 0;
	}
	for (res = res0; res; res = res->ai_next) {
		listener = (struct listener *)calloc(1, sizeof(struct listener));
		listener->socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (listener->socket == -1) {
			free(listener);
			continue;
		}
		if (socket_nonblocking(listener->socket) == -1) {
			closesocket(listener->socket);
			free(listener);
			continue;
		}
		if (bind(listener->socket, res->ai_addr, res->ai_addrlen) < 0) {
			closesocket(listener->socket);
			free(listener);
			continue;
		}
		listen(listener->socket, 5);
		listener->rig = entry->rig;
		listener->next_listener = listeners;
		listeners = listener;
		listener_count++;
	}
	if (listener_count) {
		entry->next_rig_entry = rigs;
		if (rigs)
			rigs->prev_rig_entry = entry;
		rigs = entry;
		return listener_count;
	}
	close_rig(entry->rig);
	free(entry);
	return 0;
}

void close_connection(struct connection *c)
{
	if (debug)
		printf("Closing socket %d\n", c->socket);
	closesocket(c->socket);
	if (c->tx_buf)
		free(c->tx_buf);
	if (c->rx_buf)
		free(c->rx_buf);
	if (c->next_connection)
		c->next_connection->prev_connection = c->prev_connection;
	if (c->prev_connection)
		c->prev_connection->next_connection = c->next_connection;
	else
		connections = c->next_connection;
	free(c);
}

void tx_append(struct connection *c, const char *str)
{
	char	*buf;
	size_t	len = strlen(str);

	if (debug)
		printf("TX: %s", str);
	if ((c->tx_buf_size - c->tx_buf_terminator) < len) {
		buf = (char *)realloc(c->tx_buf, c->tx_buf_size + len);
		if (buf == NULL)
			return;
		c->tx_buf = buf;
		c->tx_buf[c->tx_buf_size] = 0;
		c->tx_buf_size += len;
	}
	memcpy(c->tx_buf + c->tx_buf_terminator, str, len);
	c->tx_buf_terminator += len;
}

int tx_printf(struct connection *c, const char *format, ...)
{
	va_list	args;
	char	buf[128];
	int		ret;

	va_start(args, format);
	ret = vsnprintf(buf, sizeof(buf), format, args);
	if (ret < 0 || ret >= sizeof(buf)) {
		tx_append(c, "RPRT -1\n");
		return -1;
	}
	tx_append(c, buf);
	return 0;
}

int tx_rprt(struct connection *c, int ret)
{
	char	buf[64];
	int		sret;

	if (ret > 0)
		ret = 0-ret;
	sret = snprintf(buf, sizeof(buf), "RPRT %d\n", ret);
	if (sret > 0 && sret < sizeof(buf)) {
		tx_append(c, buf);
		return 0;
	}
	tx_append(c, "RPRT -1\n");
	return -1;
}

static int send_vfo(struct connection *c, enum vfos vfo)
{
	char	*buf = "VFOA\n";
	
	switch(vfo) {
		case VFO_B:
			buf = "VFOB\n";
			break;
		case VFO_MEMORY:
			buf = "MEM\n";
			break;
		case VFO_A:
			buf = "VFOA\n";
			break;
		case VFO_MAIN:
			buf = "Main\n";
			break;
		case VFO_SUB:
			buf = "Sub\n";
			break;
		default:
			return -1;
	}
	tx_append(c, buf);
	return 0;
}

enum vfos current_vfo(struct connection *c)
{
	if (c->rig->get_vfo)
		return get_vfo(c->rig);
	else
		return c->current_vfo;
}

static int send_mode(struct connection *c, enum rig_modes mode)
{
	char	*buf;

	switch (mode) {
		case MODE_USB:
			buf="USB";
			break;
		case MODE_LSB:
			buf="LSB";
			break;
		case MODE_CW:
			buf="CW";
			break;
		case MODE_CWR:
			buf="CWR";
			break;
		case MODE_FSK:
			buf="RTTY";
			break;
		case MODE_AM:
			buf="AM";
			break;
		case MODE_FM:
			buf="FM";
			break;
		default:
			buf=NULL;
			break;
	}
	if (buf == NULL)
		return -1;
	return tx_printf(c, "%s\n0\n", buf);
}

static int do_frequency_set(struct connection *c, enum vfos vfo, uint64_t freq, bool tx)
{
	uint64_t		tx_freq;
	uint64_t		rx_freq;
	enum rig_modes	tx_mode;
	enum rig_modes	rx_mode;
	
	if (tx) {
		tx_mode = current_mode(c, vfo);
		if (tx_mode == MODE_UNKNOWN)
			tx_mode = get_mode(c->rig);
		rx_mode = current_mode(c, paired_vfo(vfo));
		if (rx_mode == MODE_UNKNOWN)
			rx_mode = tx_mode;
		rx_freq = current_freq(c, paired_vfo(vfo));
		tx_freq = freq;
		if (tx_freq == 0)
			tx_freq = get_frequency(c->rig, vfo);
		if (rx_freq == 0)
			rx_freq = tx_freq;
	}
	else {
		rx_mode = current_mode(c, vfo);
		if (rx_mode == MODE_UNKNOWN)
			rx_mode = get_mode(c->rig);
		tx_mode = current_mode(c, paired_vfo(vfo));
		if (tx_mode == MODE_UNKNOWN)
			tx_mode = rx_mode;
		tx_freq = current_freq(c, paired_vfo(vfo));
		rx_freq = freq;
		if (rx_freq == 0)
			rx_freq = get_frequency(c->rig, vfo);
		if (tx_freq == 0)
			tx_freq = rx_freq;
	}

	if (vfo == VFO_MAIN || vfo == VFO_SUB) {
		if (set_duplex(c->rig, rx_freq, rx_mode, tx_freq, tx_mode) == 0) {
			save_freq(c, freq, vfo);
			return 0;
		}
	}
	else if (c->split) {
		if (set_split_frequency(c->rig, rx_freq, tx_freq) == 0) {
			save_freq(c, freq, vfo);
			return 0;
		}
	}
	else {
		if (set_frequency(c->rig, vfo, freq) == 0) {
			save_freq(c, freq, vfo);
			return 0;
		}
	}
	return -1;
}

#define GET_ARG(c) { \
	char *new_arg = strchr(c, ' '); \
	if (new_arg == NULL) \
		goto fail; \
	while (*new_arg == ' ') \
		new_arg++; \
	c = strchr(new_arg, ' '); \
	if (c == NULL) \
		c = strchr(new_arg, 0); \
	arg = alloca((c - new_arg) + 1); \
	if (arg == NULL) \
		goto fail; \
	strncpy(arg, new_arg, c - new_arg); \
	arg[c - new_arg] = 0; \
}

void handle_command(struct connection *c, size_t len)
{
	char			*cmdline = strdup(c->rx_buf);
	char			*cmd;
	char			*arg;
	size_t			remain = c->rx_buf_size - (c->rx_buf_pos + len + 1);
	uint64_t		u64;
	uint64_t		rx_freq, tx_freq;
	int				i;
	char			*buf;
	int				ret;
	enum rig_modes	mode;
	enum vfos		vfo;
	struct bandlimit *limit;

	// First, clean up the buffer...
	if (remain) {
		memmove(c->rx_buf, c->rx_buf + len + 1, c->rx_buf_size - (len + 1));
		c->rx_buf_pos = 0;
	}
	else {
		free(c->rx_buf);
		c->rx_buf_pos = 0;
		c->rx_buf_size = 0;
		c->rx_buf = NULL;
	}
	buf = strchr(cmdline, '\r');
	if (buf)
		*buf = 0;
	if (debug)
		printf("RX: %s\n", cmdline);
	shorten_cmds(cmdline);
	if (debug)
		printf("SC: %s\n", cmdline);
	// Now handle the commands...
	for (cmd = cmdline; *cmd; cmd++) {
		switch(*cmd) {
			case 'F':
				vfo = current_vfo(c);
				GET_ARG(cmd);
				if (sscanf(arg, "%"SCNu64, &u64) != 1)
					goto fail;
				if (current_freq(c, vfo) == u64)
					ret = 0;
				else
					ret = do_frequency_set(c, vfo, u64, false);
				if (tx_rprt(c, 0) != 0)
					goto abort;
				break;
			case 'I':
				vfo = current_vfo(c);
				GET_ARG(cmd);
				if (sscanf(arg, "%"SCNu64, &u64) != 1)
					goto fail;
				if (current_freq(c, paired_vfo(vfo)) == u64)
					ret = 0;
				else
					ret = do_frequency_set(c, paired_vfo(vfo), u64, true);
				if (tx_rprt(c, 0) != 0)
					goto abort;
				break;
			case 'f':
				u64 = get_frequency(c->rig, VFO_UNKNOWN);
				if (u64 == 0)
					goto fail;
				tx_printf(c, "%"PRIu64"\n", u64);
				break;
			case 'i':
				vfo = current_vfo(c);
				ret = get_split_frequency(c->rig, NULL, &tx_freq);
				if (ret != 0) {
					tx_freq = current_freq(c, paired_vfo(vfo));
					if (tx_freq == 0)
						goto fail;
				}
				tx_printf(c, "%"PRIu64"\n", tx_freq);
				break;
			case 'M':
				vfo = current_vfo(c);
				GET_ARG(cmd);
				mode = MODE_UNKNOWN;
				if (strcmp(arg, "USB")==0)
					mode = MODE_USB;
				else if (strcmp(arg, "LSB")==0)
					mode = MODE_LSB;
				else if (strcmp(arg, "CW")==0)
					mode = MODE_CW;
				else if (strcmp(arg, "CWR")==0)
					mode = MODE_CWR;
				else if (strcmp(arg, "RTTY")==0)
					mode = MODE_FSK;
				else if (strcmp(arg, "AM")==0)
					mode = MODE_AM;
				else if (strcmp(arg, "FM")==0)
					mode = MODE_FM;
				if (mode == MODE_UNKNOWN)
					goto fail;
				GET_ARG(cmd);
				if (current_mode(c, vfo) == mode)
					ret = 0;
				else
					ret = set_mode(c->rig, mode);
				if (tx_rprt(c, ret) == 0)
					save_mode(c, mode, vfo);
				else
					goto abort;
				break;
			case 'X':
				vfo = paired_vfo(current_vfo(c));
				GET_ARG(cmd);
				mode = MODE_UNKNOWN;
				if (strcmp(arg, "USB")==0)
					mode = MODE_USB;
				else if (strcmp(arg, "LSB")==0)
					mode = MODE_LSB;
				else if (strcmp(arg, "CW")==0)
					mode = MODE_CW;
				else if (strcmp(arg, "CWR")==0)
					mode = MODE_CWR;
				else if (strcmp(arg, "RTTY")==0)
					mode = MODE_FSK;
				else if (strcmp(arg, "AM")==0)
					mode = MODE_AM;
				else if (strcmp(arg, "FM")==0)
					mode = MODE_FM;
				if (mode == MODE_UNKNOWN)
					goto fail;
				GET_ARG(cmd);
				/* TODO: This matters when setting duplex... */
				if (current_mode(c, vfo) == mode)
					ret = 0;
				else
					ret = set_mode(c->rig, mode);
				if (tx_rprt(c, ret) == 0)
					save_mode(c, mode, vfo);
				else
					goto abort;
				break;
			case 'm':
				mode = get_mode(c->rig);
				send_mode(c, mode);
				break;
			case 'x':
				mode = current_mode(c, paired_vfo(current_vfo(c)));
				send_mode(c, mode);
				break;
			case 'V':
				GET_ARG(cmd);
				vfo = VFO_UNKNOWN;
				if ((strcmp(arg, "VFOA")==0) || (strcmp(arg, "VFO")==0))
					vfo = VFO_A;
				else if (strcmp(arg, "VFOB")==0)
					vfo = VFO_B;
				else if (strcmp(arg, "MEM")==0)
					vfo = VFO_MEMORY;
				else if (strcmp(arg, "Main")==0)
					vfo = VFO_MAIN;
				else if (strcmp(arg, "Sub")==0)
					vfo = VFO_SUB;
				if (vfo == VFO_UNKNOWN)
					goto fail;
				if (c->rig->set_vfo) {
					if (tx_rprt(c, set_vfo(c->rig, vfo)) == 0)
						c->current_vfo = vfo;
					else
						goto abort;
				}
				else {
					// Fake a VFO...
					if (do_frequency_set(c, vfo, current_freq(c, vfo), false) != 0)
						goto fail;
					if (current_mode(c, vfo) == MODE_UNKNOWN)
						save_mode(c, get_mode(c->rig), vfo);
					if (set_mode(c->rig, current_mode(c, vfo)) != 0)
						goto fail;
					c->current_vfo = vfo;
					tx_append(c, "RPRT 0\n");
				}
				break;
			case 'S':
				GET_ARG(cmd);
				if (sscanf(arg, "%d", &i) != 1)
					goto fail;
				if (i==0) {
					if (get_split_frequency(c->rig, NULL, NULL) == 0) {
						u64 = get_frequency(c->rig, VFO_UNKNOWN);
						if (u64 == 0)
							tx_append(c, "RPRT -1\n");
						else {
							if (tx_rprt(c, set_frequency(c->rig, VFO_UNKNOWN, u64)) != 0)
								goto abort;
							c->split = false;
						}
					}
					else
						tx_append(c, "RPRT 0\n");
				}
				else {
					if (get_split_frequency(c->rig, NULL, NULL) != 0) {
						// "Enable split"
						// First, switch to the "other" VFO to get the frequency
						vfo = current_vfo(c);
						rx_freq = get_frequency(c->rig, VFO_UNKNOWN);
						if (rx_freq == 0)
							goto fail;
						if (c->rig->set_vfo) {
							tx_freq = get_frequency(c->rig, paired_vfo(vfo));

							if (tx_freq == 0)
								goto fail;
						}
						else {
							tx_freq = current_freq(c, paired_vfo(vfo));
						}
						// And finally, set the split.
						c->split = true;
						if (tx_rprt(c, do_frequency_set(c, paired_vfo(vfo), tx_freq, true)) != 0) {
							c->split = false;
							goto abort;
						}
					}
					else
						tx_append(c, "RPRT 0\n");
				}
				GET_ARG(cmd);
				break;
			case 'v':
				vfo = current_vfo(c);
				if (send_vfo(c, vfo) != 0)
					goto fail;
				break;
			case 's':
				ret = get_split_frequency(c->rig, NULL, NULL);
				vfo = current_vfo(c);
				if (vfo == VFO_UNKNOWN)
					goto fail;
				tx_append(c, ret==0?"1\n":"0\n");
				if (ret) {
					if (send_vfo(c, vfo) != 0)
						goto fail;
				}
				else {
					if (send_vfo(c, paired_vfo(vfo)) != 0)
						goto fail;
				}
				break;
			case 'T':
				GET_ARG(cmd);
				if (sscanf(arg, "%d", &i) != 1)
					goto fail;
				tx_rprt(c, set_ptt(c->rig, i));
				break;
			case 't':
				switch (get_ptt(c->rig)) {
					case 0:
						tx_append(c, "0\n");
						break;
					case 1:
						tx_append(c, "1\n");
						break;
					default:
						goto fail;
				}
				break;
			case '\xf0':
				tx_append(c, "CHKVFO 0\n");
				break;
			case '\x8b':
				switch (get_squelch(c->rig)) {
					case 0:
						tx_append(c, "0\n");
						break;
					case 1:
						tx_append(c, "1\n");
						break;
					default:
						goto fail;
				}
				break;
			case 'l':
				GET_ARG(cmd);
				if (strcmp(arg, "STRENGTH") == 0) {
					i = get_smeter(c->rig);
					if ( i == -1)
						goto fail;
					tx_printf(c, "%d\n", i-49);
				}
				else
					goto fail;
				break;
			case '\x8f':
				// Output copied from the dummy driver...
				tx_append(c, "0\n");			// Protocol version
				tx_append(c, "2\n");			// Rig model (dummy)
				tx_append(c, "2\n");			// ITU region (!)
					// RX info: lowest/highest freq, modes available, low power, high power, VFOs, antennas
				i = 0x10000003;	// VFO_MEM, VFO_A, VFO_B
				if (c->rig->set_duplex)
					i |= 0xc000000;
				for (limit = c->rig->rx_limits; limit; limit = limit->next)
					tx_printf(c, "%"PRIu64" %"PRIu64" 0x1ff -1 -1 0x%x 0x01\n", limit->low, limit->high, i);
					// Terminated with all zeros
				tx_append(c, "0 0 0 0 0 0 0\n");
					// TX info (as above)
				for (limit = c->rig->tx_limits; limit; limit = limit->next)
					tx_printf(c, "%"PRIu64" %"PRIu64" 0x1ff 0 100 0x%x 0x01\n", limit->low, limit->high, i);
				tx_append(c, "0 0 0 0 0 0 0\n");
					// Tuning steps available, modes, steps
				tx_append(c, "0 0\n");
					// Filter sizes, mode, bandwidth
				tx_append(c, "0 0\n");
				tx_append(c, "0\n");			// Max RIT
				tx_append(c, "0\n");			// Max XIT
				tx_append(c, "0\n");			// Max IF shift
				tx_append(c, "0\n");			// "announces"
				tx_append(c, "\n");				// Preamp settings
				tx_append(c, "\n");				// Attenuator settings
				tx_append(c, "0x0\n");			// has get func
				tx_append(c, "0x0\n");			// has set func
				tx_printf(c, "0x%x\n", c->rig->get_smeter?0x40000000:0);	// get level
				tx_append(c, "0x0\n");			// set level
				tx_append(c, "0x0\n");			// get param
				tx_append(c, "0x0\n");			// set param
				break;
			case '\r':
			case '\n':
			case ' ':
				break;
			default:
				goto fail;
		}
		// Did args extend to the end?
		if (*cmd == 0)
			break;
	}
	free(cmdline);
	return;

fail:
	tx_append(c, "RPRT -1\n");
abort:
	free(cmdline);
}

void main_loop(void) {
	fd_set				rx_set;
	fd_set				tx_set;
	fd_set				err_set;
	int					max_sock;
	int					ret;
	int					avail;
	int					sockopt;
	struct listener		*l;
	struct connection	*c;
	char				*buf;

	for (;;) {
		max_sock = 0;
		FD_ZERO(&rx_set);
		FD_ZERO(&tx_set);
		FD_ZERO(&err_set);
		// First, add all listening sockets to the rx_set
		for (l = listeners; l; l=l->next_listener) {
			FD_SET(l->socket, &rx_set);
			if (l->socket > max_sock)
				max_sock = l->socket;
		}
		// Next, add all active connections to the other sets as appropriate
		for (c = connections; c; c=c->next_connection) {
			FD_SET(c->socket, &rx_set);
			FD_SET(c->socket, &err_set);
			if (c->socket > max_sock)
				max_sock = c->socket;
			if (c->tx_buf_terminator)
				FD_SET(c->socket, &tx_set);
		}
		// select()
		ret = select(max_sock+1, &rx_set, &tx_set, &err_set, NULL);
		if (ret==-1)
			return;
		if (ret == 0)
			continue;
		// Read/write data as appropriate...
		for (c = connections; c; c=c->next_connection) {
			// First, the exceptions... we'll just close it for now.
			if (FD_ISSET(c->socket, &err_set)) {
				close_connection(c);
			}
			// Next the writes
			if (FD_ISSET(c->socket, &tx_set)) {
				if (c->tx_buf != NULL && c->tx_buf_terminator) {
					ret = send(c->socket, c->tx_buf + c->tx_buf_pos, c->tx_buf_terminator - c->tx_buf_pos, 0);
					if (ret > 0) {
						c->tx_buf_pos += ret;
						if (c->tx_buf_pos >= c->tx_buf_terminator) {
							c->tx_buf_size = 0;
							c->tx_buf_pos = 0;
							c->tx_buf_terminator = 0;
						}
					}
					else if (ret < 0)
						close_connection(c);
				}
			}
			// Now the read()s.
			if (FD_ISSET(c->socket, &rx_set)) {
				ret = ioctl(c->socket, FIONREAD, &avail);
				if (ret == -1)
					close_connection(c);
				else if (avail > 0) {
					buf = realloc(c->rx_buf, c->rx_buf_size + avail);
					if (buf == NULL)
						close_connection(c);
					else {
						c->rx_buf = buf;
						c->rx_buf_size += avail;
						ret = recv(c->socket, c->rx_buf + c->rx_buf_pos, avail, MSG_DONTWAIT);
						if (ret <= 0)
							close_connection(c);
						else {
							c->rx_buf_pos += ret;
							if ((buf = memchr(c->rx_buf, '\n', c->rx_buf_pos)) != NULL) {
								*buf = 0;
								handle_command(c, (size_t)(buf - c->rx_buf));
							}
						}
					}
				}
				else
					close_connection(c);
			}
		}
		// Accept() new connections...
		for (l = listeners; l; l=l->next_listener) {
			if (FD_ISSET(l->socket, &rx_set)) {
				c = (struct connection *)calloc(1, sizeof(struct connection));
				if (c == NULL)
					continue;
				c->socket = accept(l->socket, NULL, NULL);
				if (c->socket == -1) {
					free(c);
					continue;
				}
				if (debug)
					printf("Accepted new connection %d\n", c->socket);
				sockopt = 1;
				setsockopt(c->socket, IPPROTO_TCP, TCP_NODELAY, &sockopt, sizeof(sockopt));
				c->rig = l->rig;
				c->next_connection = connections;
				connections = c;
				/* Read the current state */
				c->split = get_split_frequency(c->rig, &c->vfoa_freq, &c->vfob_freq) == 0;
				if (c->split) {
					if (get_vfo(c->rig) == VFO_B) {
						c->vfom_freq = c->vfob_freq;
						c->vfob_freq = c->vfoa_freq;
						c->vfoa_freq = c->vfom_freq;
						c->vfom_freq = 0;
						c->vfob_mode = get_mode(c->rig);
					}
					else
						c->vfoa_mode = get_mode(c->rig);
				}
			}
		}
	}
}

void cleanup(void)
{
	struct connection	*c;
	struct connection	*nc;
	struct listener		*l;
	struct listener		*nl;
	struct rig_entry	*r;
	struct rig_entry	*nr;

	for (c=connections; c;) {
		nc = c->next_connection;
		close_connection(c);
		c = nc;
	}
	for (l=listeners; l;) {
		nl = l->next_listener;
		closesocket(l->socket);
		free(l);
		l = nl;
	}
	for (r=rigs; r;) {
		nr = r->next_rig_entry;
		close_rig(r->rig);
		r = nr;
	}
}

void die(int sig)
{
	exit(0);
}

int main(int argc, char **argv)
{
	int			i;
	int			rig_count;
	dictionary	*d = NULL;
	int			active_rig_count = 0;
#ifdef WITH_FORK
	pid_t		pid;
	bool		use_fork = true;
#endif

	// Parse command-line arguments to find the INI file...
	for (i=1; i<argc; i++) {
		if (argv[i][0]=='-') {
			switch(argv[i][1]) {
				case 'c':
					i++;
					if (i >= argc)
						goto usage;
					d = iniparser_load(argv[i]);
					if (d == NULL) {
						fprintf(stderr, "Unable to parse %s\n", argv[i]);
						return 1;
					}
					break;
#ifdef WITH_FORK
				case 'f':
					use_fork = false;
					break;
#endif
			}
		}
		else
			goto usage;
	}
	if (d == NULL)
		goto usage;

	/*
	 * Now for each rig in the INI file, fire up a thread to do the
	 * socket interface
	 */
	rig_count = iniparser_getnsec(d);
	if (rig_count <= 0) {
		fprintf(stderr, "No rigs found!  Aborting.\n");
		return 1;
	}

	atexit(cleanup);
#ifdef WITH_SIGNAL
	signal(SIGHUP, die);
	signal(SIGINT, die);
	signal(SIGKILL, die);
	signal(SIGPIPE, die);
	signal(SIGALRM, die);
	signal(SIGTERM, die);
	signal(SIGXCPU, die);
	signal(SIGXFSZ, die);
	signal(SIGVTALRM, die);
	signal(SIGPROF, die);
	signal(SIGUSR1, die);
	signal(SIGUSR2, die);
#ifdef SIGTHR
	signal(SIGTHR, die);
#endif
#ifdef SIGLIBRT
	signal(SIGLIBRT, die);
#endif
#endif

	for (i=0; i<rig_count; i++) {
#ifdef WITH_FORK
		if (use_fork) {
			pid = fork();
			if (pid == 0) {
				// Child process
				daemon(0, 0);
				active_rig_count += add_rig(d, iniparser_getsecname(d, i));
				break;
			}
		}
		else
#endif
		{
			active_rig_count += add_rig(d, iniparser_getsecname(d, i));
		}
	}

	if (active_rig_count == 0) {
#ifdef WITH_FORK
		if (use_fork)
			return 0;
#endif
		fprintf(stderr, "Unable to set up any sockets!  Aborting\n");
		return 1;
	}

	main_loop();

	if (d)
		iniparser_freedict(d);
	return 0;
usage:
	printf("Usage:\n"
		"%s %s-c <config>\n\n"
#ifdef WITH_FORK
		"If -f is passed, remains in the forground and doesn't fork\n\n"
#endif
		"Where <config> is the path to the ini file\n\n", argv[0],
#ifdef WITH_FORK
		"[-f] "
#else
		""
#endif
		);
	if (d)
		iniparser_freedict(d);
	return 1;
}
