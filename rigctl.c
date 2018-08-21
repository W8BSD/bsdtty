#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <inttypes.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bsdtty.h"
#include "ui.h"

static int sock_readln(int sock, char *buf, size_t bufsz);
static int rigctld_socket = -1;
static int rc_tty;

void
setup_rig_control(int tty)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV
	};
	struct addrinfo *ai;
	struct addrinfo *aip;
	char port[6];
	int opt;

	rc_tty = tty;
	if (rigctld_socket != -1) {
		close(rigctld_socket);
		rigctld_socket = -1;
	}
	if (settings.rigctld_host && settings.rigctld_host[0] &&
	    settings.rigctld_port) {
		sprintf(port, "%hu", settings.rigctld_port);
		if (getaddrinfo(settings.rigctld_host, port, &hints, &ai) != 0)
			return;
		for (aip = ai; aip != NULL; aip = aip->ai_next) {
			rigctld_socket = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol);
			if (rigctld_socket == -1)
				continue;
			if (connect(rigctld_socket, aip->ai_addr, aip->ai_addrlen) == 0) {
				opt = 1;
				setsockopt(rigctld_socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
				break;
			}
			close(rigctld_socket);
			rigctld_socket = -1;
		}
		if (settings.ctl_ptt && rigctld_socket == -1)
			printf_errno("unable to connect to rigctld");
		freeaddrinfo(ai);
	}
}

const char *
format_freq(uint64_t freq)
{
	static char fstr[32];
	const char *prefix = " kMGTPEZY";
	int pc = 0;
	int pos;
	char *ch;

	fstr[0] = 0;
	if (freq) {
		sprintf(fstr, "%" PRIu64, freq);
		for (pos = strlen(fstr) - 3; pos > 0; pos -= 3) {
			memmove(&fstr[pos]+1, &fstr[pos], strlen(&fstr[pos])+1);
			fstr[pos] = '.';
		}
	}
	while (strlen(fstr) > 11) {
		ch = strrchr(fstr, '.');
		if (ch == NULL)
			printf_errno("unable to find dot in freq \"%s\"", fstr);
		*ch = 0;
		pc++;
	}

	ch = strrchr(fstr, 0);
	if (ch == NULL)
		printf_errno("unable to find end of string");
	*(ch++) = prefix[pc];
	*(ch++) = 'H';
	*(ch++) = 'z';
	*ch = 0;

	return fstr;
}

static int
sock_readln(int sock, char *buf, size_t bufsz)
{
	size_t i;
	int ret;

	for (i = 0; i < bufsz - 1; i++) {
		ret = recv(sock, buf + i, 1, MSG_WAITALL);
		if (ret == -1)
			return -1;
		if (buf[i] == '\n')
			goto done;
	}
done:
	buf[i] = 0;
	while (i > 0 && buf[i-1] == '\n')
		buf[--i] = 0;
	return i;
}

void
get_rig_freq_mode(uint64_t *freq, char *mbuf, size_t sz)
{
	char buf[1024];
	char tbuf[1024];

	if (rigctld_socket != -1) {
		if (send(rigctld_socket, "fm\n", 3, 0) != 3)
			goto next;
		if (sock_readln(rigctld_socket, buf, sizeof(buf)) <= 0) {
			close(rigctld_socket);
			rigctld_socket = -1;
			if (settings.ctl_ptt)
				printf_errno("lost connection getting rig frequency");
			goto next;
		}
		if (sock_readln(rigctld_socket, mbuf, sz) <= 0) {
			close(rigctld_socket);
			rigctld_socket = -1;
			if (settings.ctl_ptt)
				printf_errno("lost connection getting rig mode");
			goto next;
		}
		if (sock_readln(rigctld_socket, tbuf, sz) <= 0) {
			close(rigctld_socket);
			rigctld_socket = -1;
			if (settings.ctl_ptt)
				printf_errno("lost connection getting rig bandwidth");
			goto next;
		}
		if (sscanf(buf, "%" SCNu64, freq) != 1)
			goto next;
		return;
	}
next:

	*freq = 0;
	mbuf[0] = 0;
	return;
}

uint64_t
get_rig_freq(void)
{
	uint64_t ret;
	char buf[1024];

	if (rigctld_socket != -1) {
		if (send(rigctld_socket, "f\n", 2, 0) != 2)
			goto next;
		if (sock_readln(rigctld_socket, buf, sizeof(buf)) <= 0) {
			close(rigctld_socket);
			rigctld_socket = -1;
			if (settings.ctl_ptt)
				printf_errno("lost connection getting rig frequency");
			goto next;
		}
		if (sscanf(buf, "%" SCNu64, &ret) == 1)
			return ret;
	}
next:

	return 0;
}

bool
get_rig_ptt(void)
{
	char buf[1024];
	int state;

	if (settings.ctl_ptt) {
		if (rigctld_socket != -1) {
			if (send(rigctld_socket, "t\n", 2, 0) != 2)
				goto next;
			if (sock_readln(rigctld_socket, buf, sizeof(buf)) <= 0)
				printf_errno("lost connection getting rig PTT");
			if (buf[0] == '1')
				return true;
			return false;
		}
	}
next:

	if (ioctl(rc_tty, TIOCMGET, &state) == -1)
		printf_errno("getting RTS state");
	return !!(state & TIOCM_RTS);
}

bool
set_rig_ptt(bool val)
{
	char buf[1024];
	int state = TIOCM_RTS;
	int ret = true;
	int i;

	if (settings.ctl_ptt) {
		if (rigctld_socket != -1) {
			sprintf(buf, "T %d\n", val);
			if (send(rigctld_socket, buf, strlen(buf), 0) != (ssize_t)strlen(buf))
				goto next;
			if (sock_readln(rigctld_socket, buf, sizeof(buf)) <= 0)
				printf_errno("lost connection setting rig PTT");
			if (strcmp(buf, "RPRT 0") == 0)
				ret = true;
			else
				ret = false;
		}
		if (ret) {
			for (i = 0; i < 200; i++) {
				if (get_rig_ptt() == val)
					break;
				usleep(10000);
			}
		}
		return ret;
	}
next:

	if (ioctl(rc_tty, val ? TIOCMBIS : TIOCMBIC, &state) != 0)
		printf_errno("%s RTS bit", val ? "setting" : "resetting");
	return false;
}
