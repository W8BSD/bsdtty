/*-
 * Copyright (c) 2018 Stephen Hurd, W8BSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * This implements enough of the fldigi XML-RPC to use TLF... but that's
 * it for now.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#ifdef __FreeBSD__
#include <pthread_np.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsdtty.h"
#include "fsk_demod.h"
#include "rigctl.h"
#include "ui.h"

static int *lsocks;
static size_t nlsocks;
static int *csocks;
static size_t ncsocks;
static fd_set rsocks;
static int msocks;
static char *tx_buffer;
static size_t tx_bufsz;
static size_t tx_buflen;
static char *rx_buffer;
static size_t rx_bufsz;
static size_t rx_buflen;
static size_t rx_offset;
static const char * base64alphabet = 
 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
static char *req_buffer;
static size_t req_bufsz;

static int add_char(char *pos, char ch, int done, char *end);
static void add_tx(const char *str);
static int b64_encode(char *target, size_t tlen, const char *source, size_t slen);
static bool handle_request(int si);
static void remove_sock(int *sarr, size_t *n, fd_set *fds, int *max, size_t si);
static void send_xmlrpc_fault(int sock);
static void send_xmlrpc_response(int sock, char *type, char *value);
static void sendbuf(int sock, char *buf, size_t len);
static int xr_sock_readbuf(int *sock, char **buf, size_t *bufsz, unsigned long len);
static int xr_sock_readln(int *sock, char *buf, size_t bufsz);
static void * xmlrpc_thread(void *arg);
static void handle_xmlrpc(void);
static void close_sockets(void *arg);

static pthread_mutex_t rxbuf_mutex = PTHREAD_MUTEX_INITIALIZER;
#define RXBUF_LOCK()	assert(pthread_mutex_lock(&rxbuf_mutex) == 0)
#define RXBUF_UNLOCK()	assert(pthread_mutex_unlock(&rxbuf_mutex) == 0)

void
setup_xmlrpc(pthread_t *tid)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE
	};
	struct addrinfo *ai;
	struct addrinfo *aip;
	char port[6];
	int sock;
	int *tmp;
	int ret;
	char hostname[256];
	int opt;

	FD_ZERO(&rsocks);
	SETTING_RLOCK();
	if (settings.xmlrpc_host == NULL || settings.xmlrpc_host[0] == 0) {
		SETTING_UNLOCK();
		return;
	}
	if (settings.xmlrpc_port == 0) {
		SETTING_UNLOCK();
		return;
	}
	sprintf(port, "%hu", settings.xmlrpc_port);
	if (getaddrinfo(settings.xmlrpc_host, port, &hints, &ai) != 0) {
		SETTING_UNLOCK();
		return;
	}
	SETTING_UNLOCK();
	for (aip = ai; aip != NULL; aip = aip->ai_next) {
		sock = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol);
		if (sock == -1)
			continue;
		opt = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		for (;;) {
			if ((ret = bind(sock, aip->ai_addr, aip->ai_addrlen)) == 0) {
				if ((ret = listen(sock, 8)) == 0) {
					opt = 1;
					if (ioctl(sock, FIONBIO, &opt) == -1)
						printf_errno("setting socket nonblocking");
					nlsocks++;
					tmp = realloc(lsocks, sizeof(*lsocks) * nlsocks);
					if (tmp == NULL)
						close(sock);
					else {
						lsocks = tmp;
						lsocks[nlsocks - 1] = sock;
						FD_SET(sock, &rsocks);
						if (sock > msocks)
							msocks = sock;
					}
				}
				else
					close(sock);
				break;
			}
			else {
				if (errno == EADDRINUSE) {
					if (getnameinfo(aip->ai_addr, aip->ai_addrlen, hostname, sizeof(hostname), NULL, 0, 0) == 0) {
						fprintf(stderr, "Address %s:%s is in use... will retry unless you hit CTRL-C\n", hostname, port);
						sleep(1);
						continue;
					}
					else {
						close(sock);
						break;
					}
				}
				fprintf(stderr, "%s: binding XML-RPC port\n", strerror(errno));
				close(sock);
				break;
			}
		}
	}
	freeaddrinfo(ai);
	pthread_create(tid, NULL, xmlrpc_thread, NULL);
}

/*
 * Currently, this assumes that the entire request will be available and that
 * the entire response can be written.
 */
static void
handle_xmlrpc(void)
{
	fd_set rfds;
	size_t i;
	struct timeval tv = {
		.tv_sec = 1,
		.tv_usec = 0
	};
	struct sockaddr sa;
	socklen_t salen = sizeof(sa);
	int sock;
	int *tmp;
	int count = 0;

	rfds = rsocks;
	switch (count = select(msocks + 1, &rfds, NULL, NULL, &tv)) {
		case -1:
			if (errno != EINTR)
				printf_errno("selecting xmlrpc");
			return;
		case 0:
			return;
	}

	/* Now service connected sockets */
	for (i = 0; i < ncsocks; i++) {
		if (FD_ISSET(csocks[i], &rfds)) {
			if (!handle_request(i)) {
				remove_sock(csocks, &ncsocks, &rsocks, &msocks, i);
				i--;
			}
			if (--count == 0)
				return;
		}
	}

	/* Now, service listening sockets */
	for (i = 0; i < nlsocks; i++) {
		if (FD_ISSET(lsocks[i], &rfds)) {
			sock = accept(lsocks[i], &sa, &salen);
			if (sock == -1)
				remove_sock(lsocks, &nlsocks, &rsocks, &msocks, i);
			else {
				ncsocks++;
				tmp = realloc(csocks, sizeof(*csocks) * ncsocks);
				if (tmp == NULL)
					close(sock);
				else {
					csocks = tmp;
					csocks[ncsocks - 1] = sock;
					if (sock > msocks)
						msocks = sock;
					FD_SET(sock, &rsocks);
				}
			}
			if (--count == 0)
				return;
		}
	}

}

/*
 * Super fragile...
 */
static bool
handle_request(int si)
{
	char buf[1024];
	char cmd[128] = "";
	bool headers = true;
	unsigned long content_len = ULONG_MAX;
	unsigned long start = 0;
	unsigned long len = 0;
	char *c;
	char *p;
	int ret;
	unsigned int uret;
	size_t bytes;

	for (bytes = 0; bytes < content_len;) {
		if (headers) {
			if ((ret = xr_sock_readln(&csocks[si], buf, sizeof(buf))) < 0 || csocks[si] == -1)
				break;
			if (buf[0] == 0) {
				headers = false;
				continue;
			}
			if (strncasecmp(buf, "Content-Length:", 15) == 0) {
				c = strrchr(buf, ':');
				if (c == NULL)
					continue;
				c++;
				while (isspace(*c))
					c++;
				errno = 0;
				content_len = strtoul(c, NULL, 10);
				if (content_len == 0 && errno == ERANGE)
					content_len = ULONG_MAX;
			}
		}
		else {
			if ((ret = xr_sock_readbuf(&csocks[si], &req_buffer, &req_bufsz, content_len)) == -1)
				break;
			bytes += ret;
			c = strstr(req_buffer, "<methodName>");
			if (c != NULL) {
				c += 12;
				strncpy(cmd, c, sizeof(cmd));
				cmd[sizeof(cmd) - 1] = 0;
				c = strchr(cmd, '<');
				if (c != NULL)
					*c = 0;
			}
			else {
				cmd[0] = 0;
				printf_errno("No command in XML-RPC request");
			}
			for (p = c = req_buffer; c != NULL;) {
				c = strstr(c, "<value>");
				if (c != NULL) {
					c += 7;
					c = strchr(c, '>');
					if (c != NULL) {
						c++;
						memmove(p, c, strlen(c) + 1);
						c = strchr(p, '<');
						if (c != NULL) {
							*(c++) = 0;
							p = c;
						}
					}
				}
			}
			if (p == req_buffer)
				*p = 0;
			if (csocks[si] == -1)
				break;
		}
	}

	/* Now handle the command */
	if (strcmp(cmd, "main.rx") == 0) {
		RTS_RLOCK();
		if (rts) {
			RTS_UNLOCK();
			send_string(strdup("\t"));
		}
		else
			RTS_UNLOCK();
		send_xmlrpc_response(csocks[si], NULL, NULL);
	}
	else if (strcmp(cmd, "main.get_trx_state") == 0) {
		RTS_RLOCK();
		if (rts) {
			RTS_UNLOCK();
			send_xmlrpc_response(csocks[si], "string", "TX");
		}
		else {
			RTS_UNLOCK();
			send_xmlrpc_response(csocks[si], "string", "RX");
		}
	}
	else if (strcmp(cmd, "text.clear_tx") == 0) {
		send_xmlrpc_response(csocks[si], NULL, NULL);
		tx_buflen = 0;
		tx_buffer[0] = 0;
	}
	else if (strcmp(cmd, "text.add_tx") == 0) {
		send_xmlrpc_response(csocks[si], NULL, NULL);
		if (req_buffer[0])
			add_tx(req_buffer);
	}
	else if (strcmp(cmd, "main.tx") == 0) {
		send_xmlrpc_response(csocks[si], NULL, NULL);
		send_string(tx_buffer);
		tx_buffer = NULL;
		tx_bufsz = 0;
		tx_buflen = 0;
	}
	else if (strcmp(cmd, "text.get_rx_length") == 0) {
		RXBUF_LOCK();
		sprintf(buf, "%zu", rx_offset + rx_buflen);
		RXBUF_UNLOCK();
		send_xmlrpc_response(csocks[si], "int", buf);
	}
	else if (strcmp(cmd, "text.get_rx") == 0) {
		start = strtoul(req_buffer, &c, 10);
		len = strtoul(c + 1, NULL, 10);
		/*
		 * TODO: TLF uses start:end, not start:len as documented
		 * on fldigi website... but since end is always greater
		 * then len, that "works" since fldigi truncates. 
		 * Unfortuantely, we're throwing away the rx buffer as
		 * soon as we send it, so we need to respect this.
		 */
		RXBUF_LOCK();
		if (len > rx_buflen + rx_offset - start)
			len = start - len;
		if (rx_offset <= start) {
			if (len + start <= rx_offset + rx_buflen) {
				b64_encode(buf, sizeof(buf), rx_buffer + (start - rx_offset), len);
				send_xmlrpc_response(csocks[si], "base64", buf);
				memmove(rx_buffer, rx_buffer + (start - rx_offset) + len, strlen(rx_buffer + (start - rx_offset)) + 1);
				rx_buflen -= (start - rx_offset) + len;
				rx_offset += (start - rx_offset) + len;
			}
			else
				printf_errno("invalid rxbuf request length %ld + %ld (%zu + %zu)", start, len, rx_offset, rx_buflen);
		}
		else
			printf_errno("invalid rxbuf request offset (%ld:%ld from %zu:%zu)", start, len, rx_offset, rx_offset + rx_buflen);
		RXBUF_UNLOCK();
	}
	else if (strcmp(cmd, "modem.get_carrier") == 0) {
		SETTING_RLOCK();
		sprintf(buf, "%d", (int)((settings.mark_freq + settings.space_freq) / 2));
		SETTING_UNLOCK();
		send_xmlrpc_response(csocks[si], "int", buf);
	}
	else if (strcmp(cmd, "modem.set_carrier") == 0) {
		// TODO?
		SETTING_RLOCK();
		sprintf(buf, "%d", (int)((settings.mark_freq + settings.space_freq) / 2));
		SETTING_UNLOCK();
		send_xmlrpc_response(csocks[si], "int", buf);
	}
	/* The rest are for completeness... */
	else if (strcmp(cmd, "fldigi.name") == 0) {
		send_xmlrpc_response(csocks[si], "string", "bsdtty");
	}
	else if (strcmp(cmd, "modem.get_name") == 0) {
		send_xmlrpc_response(csocks[si], "string", "RTTY");
	}
	else if (strcmp(cmd, "modem.get_names") == 0) {
		send_xmlrpc_response(csocks[si], "array", "<data><value>RTTY</value></data>");
	}
	else if (strcmp(cmd, "modem.id") == 0) {
		send_xmlrpc_response(csocks[si], "int", "0");
	}
	else if (strcmp(cmd, "modem.get_max_id") == 0) {
		send_xmlrpc_response(csocks[si], "int", "0");
	}
	else if (strcmp(cmd, "modem.set_by_name") == 0) {
		if (strcmp(req_buffer, "RTTY"))
			send_xmlrpc_fault(csocks[si]);
		else
			send_xmlrpc_response(csocks[si], "string", "RTTY");
	}
	else if (strcmp(cmd, "modem.set_by_id") == 0) {
		if (strcmp(req_buffer, "0"))
			send_xmlrpc_fault(csocks[si]);
		else
			send_xmlrpc_response(csocks[si], "string", "0");
	}
	else if (strcmp(cmd, "modem.get_reverse") == 0) {
		BSDTTY_LOCK();
		sprintf(buf, "%d", reverse);
		BSDTTY_UNLOCK();
		send_xmlrpc_response(csocks[si], "boolean", buf);
	}
	else if (strcmp(cmd, "modem.set_reverse") == 0) {
		BSDTTY_LOCK();
		sprintf(buf, "%d", reverse);
		if (atoi(req_buffer) != reverse) {
			toggle_reverse(&reverse);
			send_fsk->toggle_reverse();
		}
		BSDTTY_UNLOCK();
		send_xmlrpc_response(csocks[si], "boolean", buf);
	}
	else if (strcmp(cmd, "modem.toggle_reverse") == 0) {
		BSDTTY_LOCK();
		toggle_reverse(&reverse);
		send_fsk->toggle_reverse();
		sprintf(buf, "%d", reverse);
		BSDTTY_UNLOCK();
		send_xmlrpc_response(csocks[si], "boolean", buf);
	}
	else if (strcmp(cmd, "modem.run_macro") == 0) {
		uret = strtoui(req_buffer, NULL, 10);
		SETTING_RLOCK();
		if (uret < sizeof(settings.macros) / sizeof(*settings.macros)) {
			SETTING_UNLOCK();
			send_xmlrpc_response(csocks[si], NULL, NULL);
			do_macro(uret + 1);
		}
		else {
			SETTING_UNLOCK();
			send_xmlrpc_fault(csocks[si]);
		}
	}
	else if (strcmp(cmd, "modem.get_max_macro_id") == 0) {
		SETTING_RLOCK();
		sprintf(buf, "%zu", sizeof(settings.macros) / sizeof(*settings.macros) - 1);
		SETTING_UNLOCK();
		send_xmlrpc_response(csocks[si], "int", buf);
	}
	else if (strcmp(cmd, "log.get_serial_number_sent") == 0) {
		BSDTTY_LOCK();
		sprintf(buf, "%03d", serial);
		BSDTTY_UNLOCK();
		send_xmlrpc_response(csocks[si], "string", buf);
	}
	else if (strcmp(cmd, "log.set_call") == 0) {
		captured_callsign(req_buffer);
		send_xmlrpc_response(csocks[si], NULL, NULL);
	}
	else if (strcmp(cmd, "log.get_call") == 0) {
		send_xmlrpc_response(csocks[si], "string", their_callsign ? their_callsign : "");
	}
	else if (strcmp(cmd, "log.get_exchange") == 0) {
		// TODO: Ignored
		send_xmlrpc_response(csocks[si], "string", "");
	}
	else if (strcmp(cmd, "log.set_exchange") == 0) {
		// TODO: Ignored
		send_xmlrpc_response(csocks[si], NULL, NULL);
	}
	else if (strcmp(cmd, "rig.set_mode") == 0) {
		// TODO: Ignored.
		send_xmlrpc_response(csocks[si], NULL, NULL);
	}
	else if (strcmp(cmd, "rig.set_frequency") == 0) {
		// TODO: Ignored.
		sprintf(buf, "%" PRIu64, get_rig_freq());
		send_xmlrpc_response(csocks[si], "double", buf);
	}
	else if (strcmp(cmd, "fldigi.list") == 0) {
		send_xmlrpc_response(csocks[si], "array", "<data>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Lists all supported commands</value></member>"
		                   "<member><name>name</name>"
		                       "<value>fldigi.list</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>A:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Sets RX mode (disables TX)</value></member>"
		                   "<member><name>name</name>"
		                       "<value>main.rx</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>n:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns \"RX\" in RX mode, \"TX\" in TX mode</value></member>"
		                   "<member><name>name</name>"
		                       "<value>main.get_trx_state</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>s:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Clears the TX buffer</value></member>"
		                   "<member><name>name</name>"
		                       "<value>text.clear_tx</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>n:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Adds string to the TX buffer</value></member>"
		                   "<member><name>name</name>"
		                       "<value>text.add_tx</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>n:s</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Sets TX mode (ie: PTT) main.rx to turn of PTT</value></member>"
		                   "<member><name>name</name>"
		                       "<value>main.tx</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>n:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Gets the total number of bytes decoded so far</value></member>"
		                   "<member><name>name</name>"
		                       "<value>text.get_rx_length</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>i:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns the string from start to end.  In fldigi this is start and length, but TLF expects start:end</value></member>"
		                   "<member><name>name</name>"
		                       "<value>text.get_rx</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>6:ii</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns the average of the mark and space frequencies</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.get_carrier</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>i:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Not actually supported, simply returns the average of the mark and space frequencies</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.set_carrier</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>i:i</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns \"bsdtty\"</value></member>"
		                   "<member><name>name</name>"
		                       "<value>fldigi.name</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>s:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns \"RTTY\"</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.get_name</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>s:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns an array containing only \"RTTY\"</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.get_names</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>A:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns 0 (index into modem list)</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.id</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>i:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns 0 (only one modem supported)</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.get_max_id</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>i:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>If the name isn't \"RTTY\" returns a fault, otherwise, returns \"RTTY\"</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.set_by_name</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>s:s</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>If the parameter isn't 0, returns a fault.  Otherwise, returns zero.</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.set_by_id</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>i:i</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns 1 in reverse mode, 0 in normal mode</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.get_reverse</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>b:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Sets the reverse state, returns old state</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.set_reverse</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>b:b</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Toggles reverse state, returns new start</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.toggle_reverse</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>b:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Runs the specified macro (0-9)</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.run_macro</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>n:i</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns 9</value></member>"
		                   "<member><name>name</name>"
		                       "<value>modem.get_max_macro_id</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>i:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Returns the current serial number as a string</value></member>"
		                   "<member><name>name</name>"
		                       "<value>log.get_serial_number_sent</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>s:n</value></member></struct></value>"
		    "<value><struct><member><name>help</name>"
		                       "<value>Sets their call</value></member>"
		                   "<member><name>name</name>"
		                       "<value>log.set_call</value></member>"
		                   "<member><name>signature</name>"
		                       "<value>n:s</value></member></struct></value>"
		    "</data>");
	}
	else
		send_xmlrpc_fault(csocks[si]);

	if (bytes != content_len || csocks[si] == -1)
		return false;
	return true;
}

static int
xr_sock_readln(int *sock, char *buf, size_t bufsz)
{
	fd_set rd;
	size_t i;
	int bytes = 0;
	int ret;
	struct timeval tv;

	for (i = 0; i < bufsz - 1; i++) {
		FD_ZERO(&rd);
		FD_SET(*sock, &rd);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		switch(select(*sock+1, &rd, NULL, NULL, &tv)) {
			case -1:
				close(*sock);
				*sock = -1;
				if (errno == EINTR)
					continue;
				return -1;
			case 0:
				goto done;
			case 1:
				ret = recv(*sock, buf + i, 1, MSG_WAITALL);
				if (ret == -1) {
					close(*sock);
					*sock = -1;
					return -1;
				}
				if (ret == 0) {
					close(*sock);
					*sock = -1;
					goto done;
				}
				bytes++;
				if (buf[i] == '\n')
					goto done;
				break;
		}
	}
done:
	buf[i] = 0;
	while (i > 0 && buf[i-1] == '\n')
		buf[--i] = 0;
	while (i > 0 && buf[i-1] == '\r')
		buf[--i] = 0;
	return bytes;
}

static int
xr_sock_readbuf(int *sock, char **buf, size_t *bufsz, unsigned long len)
{
	fd_set rd;
	unsigned long i;
	int ret;
	struct timeval tv;
	char *tmp;

	if (len > 0) {
		if (len >= *bufsz) {
			tmp = realloc(*buf, len + 1);
			if (tmp == NULL)
				return -1;
			*buf = tmp;
			*bufsz = len + 1;
		}
	}
	else
		return -1;

	for (i = 0; i < len;) {
		FD_ZERO(&rd);
		FD_SET(*sock, &rd);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		switch(select(*sock+1, &rd, NULL, NULL, &tv)) {
			case -1:
				if (errno == EINTR)
					continue;
				return -1;
			case 0:
				close(*sock);
				*sock = -1;
				goto done;
			case 1:
				ret = recv(*sock, (*buf) + i, len, MSG_WAITALL);
				if (ret == -1) {
					close(*sock);
					*sock = -1;
					return -1;
				}
				if (ret == 0) {
					close(*sock);
					*sock = -1;
					goto done;
				}
				i += ret;
				break;
		}
	}
done:
	(*buf)[i] = 0;
	return i;
}

static void
remove_sock(int *sarr, size_t *n, fd_set *fds, int *max, size_t si)
{
	size_t i;

	if (max && *max == sarr[si]) {
		*max = 0;
		for (i = 0; i < *n; i++) {
			if (i == si)
				continue;
			if (sarr[si] > *max)
				*max = sarr[si];
		}
	}
	if (sarr[si] != -1) {
		if (fds)
			FD_CLR(sarr[si], fds);
		close(sarr[si]);
	}
	if (si != *n - 1) {
		memmove(&sarr[si], &sarr[si + 1],
		    sizeof(*sarr) * (*n - si - 1));
	}
	(*n)--;
}

static void
send_xmlrpc_response(int sock, char *type, char *value)
{
	char *buf;

	asprintf(&buf, "HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Type: text/xml\r\nContent-Length: %lu\r\n\r\n<?xml version=\"1.0\"?>\n<methodResponse><params><param><value><%s%s%s%s%s%s</value></param></params></methodResponse>", 61+(type == NULL ? 5 : ((strlen(type) * 2) + strlen(value) + 4)) + 42, type == NULL ? "nil/>" : type, type == NULL ? "" : ">", value == NULL ? "" : value, type == NULL ? "" : "</", type == NULL ? "" : type, type == NULL ? "" : ">");
	sendbuf(sock, buf, strlen(buf));
	free(buf);
}

static void
add_tx(const char *str)
{
	char *tmp;
	size_t slen = strlen(str);
	const char *c;
	char *b = &tx_buffer[tx_buflen];

	if (slen + tx_buflen >= tx_bufsz) {
		tmp = realloc(tx_buffer, tx_buflen + slen + 1);
		if (tmp == NULL)
			return;
		tx_buffer = tmp;
		tx_bufsz = tx_buflen + slen + 1;
		b = &tx_buffer[tx_buflen];
	}

	/*
	 * Now copy it in, but we need to parse out ^r and XML entities
	 */
	for (c = str; *c; c++) {
		switch (*c) {
			case '^':
				c++;
				switch (*c) {
					case 0:
						break;
					case 'r':
					case 'R':
						*(b++) = '\t';
						break;
					case '^':
						*(b++) = '^';
						break;
					default:
						break;
				}
				break;
			case '&':
				if (strncmp(c, "&amp;", 5) == 0) {
					*(b++) = '&';
					c += 4;
				}
				else if (strncmp(c, "&lt;", 4) == 0) {
					*(b++) = '<';
					c += 3;
				}
				else
					printf_errno("unhandled entity: %s", c);
				break;
			case '\n':
				*(b++) = '\r';
				break;
			default:
				*(b++) = *c;
				break;
		}
	}
	*b = 0;
	tx_buflen = b - tx_buffer;
	RTS_RLOCK();
	if (rts) {
		RTS_UNLOCK();
		send_string(tx_buffer);
		tx_buffer = NULL;
		tx_bufsz = 0;
		tx_buflen = 0;
	}
	else
		RTS_UNLOCK();
}

void
fldigi_add_rx(char ch)
{
	char *tmp;

	if (ncsocks == 0 && nlsocks == 0)
		return;

	RXBUF_LOCK();
	if (rx_buflen + 1 >= rx_bufsz) {
		tmp = realloc(rx_buffer, rx_bufsz ? rx_bufsz * 2 : 128);
		if (tmp == NULL) {
			RXBUF_UNLOCK();
			return;
		}
		rx_buffer = tmp;
		rx_bufsz = rx_bufsz ? rx_bufsz * 2 : 128;
	}

	tmp = &rx_buffer[rx_buflen];
	*(tmp++) = ch;
	*tmp = 0;
	rx_buflen++;
	RXBUF_UNLOCK();
}

static int
add_char(char *pos, char ch, int done, char *end)
{
	if(pos>=end)  {
		return(1);
	}
	if(done)
		*pos=base64alphabet[64];
	else
		*pos=base64alphabet[(int)ch];
	return(0);
}

static int
b64_encode(char *target, size_t tlen, const char *source, size_t slen)
{
	const char	*inp;
	char	*outp;
	char	*outend;
	const char	*inend;
	char	*tmpbuf=NULL;
	int		done=0;
	char	enc;
	int		buf;
	
	if(slen==0)
		slen=strlen(source);
	inp=source;
	if(source==target)  {
		tmpbuf=(char *)malloc(tlen);
		if(tmpbuf==NULL)
			return(-1);
		outp=tmpbuf;
	}
	else
		outp=target;

	outend=outp+tlen;
	inend=inp+slen;
	for(;(inp < inend) && !done;)  {
		enc=*(inp++);
		buf=(enc & 0x03)<<4;
		enc=(enc&0xFC)>>2;
		if(add_char(outp++, enc, done, outend)) {
			if (tmpbuf)
				free(tmpbuf);
			return(-1);
		}
		if (inp>=inend)
			enc=buf;
		else
			enc=buf|((*inp & 0xF0) >> 4);
		if(add_char(outp++, enc, done, outend)) {
			if (tmpbuf)
				free(tmpbuf);
			return(-1);
		}
		if(inp==inend)
			done=1;
		if (!done) {
			buf=(*(inp++)<<2)&0x3C;
			if (inp == inend)
				enc=buf;
			else
				enc=buf|((*inp & 0xC0)>>6);
		}
		if(add_char(outp++, enc, done, outend)) {
			if (tmpbuf)
				free(tmpbuf);
			return(-1);
		}
		if(inp==inend)
			done=1;
		if (!done)
			enc=((int)*(inp++))&0x3F;
		if(add_char(outp++, enc, done, outend)) {
			if (tmpbuf)
				free(tmpbuf);
			return(-1);
		}
		if(inp==inend)
			done=1;
	}
	if(outp<outend)
		*outp=0;
	int result;
	if(source==target) {
		memcpy(target,tmpbuf,tlen);
		result = outp - tmpbuf;
		free(tmpbuf);
	} else
		result = outp - target;

	return result;
}

static void
sendbuf(int sock, char *buf, size_t len)
{
	int ret;
	size_t sent = 0;

	while(sent < len) {
		ret = send(sock, buf + sent, len - sent, 0);
		if (ret == -1)
			return;
		sent += ret;
	}
}

static void
send_xmlrpc_fault(int sock)
{
	char *buf = "HTTP/1.1 501 Crappy Server\r\nConnection: Keep-Alive\r\nContent-Type: text/xml\r\nContent-Length: 293\r\n\r\n<?xml version=\"1.0\"?>\n<methodResponse><fault><value><struct><member><name>faultCode</name><value><int>73</int></value></member><member><name>faultString</name><value><string>This server is too crap to even know what you want.</string></value></member></struct></value></fault></methodResponse>";

	sendbuf(sock, buf, strlen(buf));
}

static void
close_sockets(void *arg)
{
	(void)arg;

	/* First, close listening sockets */
	while (nlsocks)
		remove_sock(lsocks, &nlsocks, &rsocks, &msocks, 0);

	/* Now close connected sockets */
	while (ncsocks)
		remove_sock(csocks, &ncsocks, &rsocks, &msocks, 0);
}

static void *
xmlrpc_thread(void *arg)
{
	sigset_t blk;
	(void)arg;

	memset(&blk, 0xff, sizeof(blk));
	assert(pthread_sigmask(SIG_BLOCK, &blk, NULL) == 0);
#ifdef __linux__
	pthread_setname_np(pthread_self(), "XML-RPC");
#else
	pthread_set_name_np(pthread_self(), "XML-RPC");
#endif
	pthread_cleanup_push(close_sockets, NULL);

	for (;;) {
		handle_xmlrpc();
		pthread_testcancel();
	}

	pthread_cleanup_pop(true);

	return NULL;
}
