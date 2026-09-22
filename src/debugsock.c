/*
  Arculator - Acorn Archimedes emulator

  DebugSock - expose the built-in debugger over a local TCP socket.

  See debugsock.h. The socket carries plain text in both directions: the
  client sends one command line per '\n', and gets back exactly what the
  debugger would have printed in its console window. There is no framing,
  because a human at a telnet prompt should be able to use it and because
  the debugger's own output is the protocol.

  One departure from the console: a line sent while the machine is running
  is not a debugger command (the debugger is not in its command loop to
  read one). Only "pause" and "detach" are acted on there; anything else is
  refused with a message saying so.

  Copyright (C) 2026 David Sharp
  Licensed under the GNU General Public License v2 or later; see COPYING.
 */

#include <stdio.h>
#include <string.h>

#include "socket-compat.h"

#include "arc.h"
#include "config.h"
#include "debugger.h"
#include "debugsock.h"

#define DEBUGSOCK_DEFAULT_TCP_PORT 15601

/* Configuration, read from the machine's .cfg by config.c. */
int debugsock_enabled = 1;
int debugsock_port = DEBUGSOCK_DEFAULT_TCP_PORT;

#define DS_LINE_MAX	256
#define DS_QUEUE_MAX	8	/* commands accepted ahead of the debugger */

typedef struct {
	int	initialised;
	int	listen_fd;	/* -1 = disabled/failed */
	int	client_fd;	/* -1 = no client */

	char	in_buf[DS_LINE_MAX];
	size_t	in_len;
	int	in_overflow;

	/* Command lines read from the client and not yet handed to the
	   debugger. A small ring: a client that types ahead is served in
	   order, and one that floods is told to wait. */
	char	queue[DS_QUEUE_MAX][DS_LINE_MAX];
	int	queue_head, queue_count;
} DebugSockState;

static DebugSockState ds = {
	.listen_fd = -1,
	.client_fd = -1,
};

/* ---- plumbing -------------------------------------------------------- */

static void
ds_send(const char *s)
{
	size_t len, sent = 0;

	if (ds.client_fd < 0 || s == NULL) {
		return;
	}
	len = strlen(s);
	while (sent < len) {
		/* Blocking would stall the emulator, so a client that does not
		   read is simply dropped - the debugger must not be held up by
		   a stuck viewer. */
		ssize_t n = send(ds.client_fd, s + sent, (int) (len - sent),
		    MSG_NOSIGNAL);

		if (n <= 0) {
			if (n < 0 && (sock_errno() == SOCK_EWOULDBLOCK
			    || sock_errno() == SOCK_EAGAIN)) {
				return;		/* drop the rest, keep the client */
			}
			closesocket(ds.client_fd);
			ds.client_fd = -1;
			return;
		}
		sent += (size_t) n;
	}
}

static int
ds_listen_tcp_from(int first)
{
	const int attempts = 16;
	int i;

	for (i = 0; i < attempts; i++) {
		const int port = first + i;
		struct sockaddr_in addr;
		int fd, on = 1;

		if (port < 1 || port > 65535) {
			break;
		}
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			rpclog("DebugSock: socket() failed\n");
			return -1;
		}
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &on,
		    sizeof(on));
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	/* local only */
		addr.sin_port = htons((uint16_t) port);
		if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0
		    || listen(fd, 1) < 0) {
			closesocket(fd);
			continue;
		}
		socket_set_nonblocking(fd);
		debugsock_port = port;
		rpclog("DebugSock: listening on 127.0.0.1:%d\n", port);
		return fd;
	}
	rpclog("DebugSock: could not bind a port from %d\n", first);
	return -1;
}

void
debugsock_init(void)
{
	if (ds.initialised) {
		debugsock_close();
	}

	memset(&ds, 0, sizeof(ds));
	ds.listen_fd = -1;
	ds.client_fd = -1;
	ds.initialised = 1;

	if (!debugsock_enabled) {
		return;
	}

#ifdef _WIN32
	{
		WSADATA wsa;

		/* Harmless if HostCmd has already started Winsock: the calls
		   are reference counted. */
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			rpclog("DebugSock: WSAStartup failed\n");
			return;
		}
	}
#endif

	{
		int port = debugsock_port;

		if (port < 1 || port > 65535) {
			port = DEBUGSOCK_DEFAULT_TCP_PORT;
		}
		ds.listen_fd = ds_listen_tcp_from(port);
	}
}

void
debugsock_close(void)
{
	if (ds.client_fd >= 0) {
		closesocket(ds.client_fd);
		ds.client_fd = -1;
	}
	if (ds.listen_fd >= 0) {
		closesocket(ds.listen_fd);
		ds.listen_fd = -1;
	}
#ifdef _WIN32
	if (ds.initialised && debugsock_enabled) {
		WSACleanup();
	}
#endif
	ds.initialised = 0;
}

int
debugsock_connected(void)
{
	return ds.client_fd >= 0;
}

void
debugsock_output(const char *s)
{
	ds_send(s);
}

/* ---- reading commands ------------------------------------------------- */

static void
ds_queue_line(const char *line)
{
	int slot;

	if (ds.queue_count >= DS_QUEUE_MAX) {
		ds_send("busy, command dropped\n");
		return;
	}
	slot = (ds.queue_head + ds.queue_count) % DS_QUEUE_MAX;
	strncpy(ds.queue[slot], line, DS_LINE_MAX - 1);
	ds.queue[slot][DS_LINE_MAX - 1] = '\0';
	ds.queue_count++;
}

/*
 * Act on a line that arrived while the machine is running.
 *
 * The debugger is not in its command loop then, so there is nobody to read an
 * ordinary command. Rather than queue one that would take effect at some
 * unpredictable later breakpoint, only the commands that make sense here are
 * accepted and the rest are refused with an explanation.
 */
static void
ds_handle_out_of_band(const char *line)
{
	if (!strncasecmp(line, "pause", 5) || !strcasecmp(line, "p")) {
		/* debugon arms the per-instruction hook; debug makes the next
		   instruction enter the command loop. */
		debugon = 1;
		debug = 1;
		ds_send("pausing...\n");
	} else if (!strncasecmp(line, "detach", 6)) {
		ds_send("bye\n");
		closesocket(ds.client_fd);
		ds.client_fd = -1;
	} else if (!strncasecmp(line, "help", 4) || line[0] == '?') {
		ds_send("Machine is running. Commands here: pause, detach, help.\n"
			"Everything else is a debugger command, available once paused\n"
			"(by 'pause', a breakpoint or an enabled trap).\n");
	} else {
		ds_send("machine is running - send 'pause' first\n");
	}
}

static void
ds_read_client(int running)
{
	char tmp[512];
	ssize_t n, i;

	n = recv(ds.client_fd, tmp, sizeof(tmp), 0);
	if (n == 0) {
		closesocket(ds.client_fd);
		ds.client_fd = -1;
		return;
	}
	if (n < 0) {
		if (sock_errno() == SOCK_EWOULDBLOCK
		    || sock_errno() == SOCK_EAGAIN) {
			return;
		}
		closesocket(ds.client_fd);
		ds.client_fd = -1;
		return;
	}

	for (i = 0; i < n; i++) {
		const char c = tmp[i];

		if (ds.in_overflow) {
			if (c == '\n') {
				ds.in_overflow = 0;
				ds_send("command too long, ignored\n");
			}
			continue;
		}
		if (c == '\n') {
			size_t len = ds.in_len;

			ds.in_len = 0;
			if (len > 0 && ds.in_buf[len - 1] == '\r') {
				len--;
			}
			ds.in_buf[len] = '\0';
			if (len == 0) {
				/* An empty line repeats the last command in the
				   debugger, so pass it on when paused. */
				if (!running) {
					ds_queue_line("");
				}
				continue;
			}
			if (running) {
				ds_handle_out_of_band(ds.in_buf);
			} else {
				ds_queue_line(ds.in_buf);
			}
			continue;
		}
		if (ds.in_len < DS_LINE_MAX - 1) {
			ds.in_buf[ds.in_len++] = c;
		} else {
			ds.in_overflow = 1;
			ds.in_len = 0;
		}
	}
}

/*
 * Service the socket.
 *
 * Called from arc_run() while the machine runs, and from the debugger's input
 * wait while it is paused - in the second case the emulator thread is inside
 * debugger_do(), so nothing else would service the socket.
 */
static void
ds_service(int running)
{
	struct pollfd pfd;

	if (ds.listen_fd < 0) {
		return;
	}

	if (ds.client_fd < 0) {
		/* Throttled the way HostCmd's accept is: this is called very
		   often while running, and a client can wait a few ms. */
		static unsigned since_listen_check;

		if (running && (++since_listen_check & 0x3fu) != 0) {
			return;
		}

		pfd.fd = ds.listen_fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
			int c = accept(ds.listen_fd, NULL, NULL);

			if (c >= 0) {
				char greeting[256];

				socket_set_nonblocking(c);
				socket_set_nodelay(c);
				ds.client_fd = c;
				ds.in_len = 0;
				ds.in_overflow = 0;
				ds.queue_head = ds.queue_count = 0;
				snprintf(greeting, sizeof(greeting),
				    "Arculator debugger (%s). 'pause' to stop the "
				    "machine, 'h' for debugger commands.\n",
				    machine_config_name);
				ds_send(greeting);
			}
		}
		return;
	}

	pfd.fd = ds.client_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) <= 0) {
		return;
	}
	if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
		closesocket(ds.client_fd);
		ds.client_fd = -1;
		return;
	}
	if (pfd.revents & POLLIN) {
		ds_read_client(running);
	}
}

void
debugsock_poll(void)
{
	ds_service(1);
}

/*
 * Next command line from the client, or 0 if none is waiting.
 *
 * Called from the debugger's command loop, which is why it services the
 * socket itself: while the debugger waits, the emulator thread is here.
 */
int
debugsock_input_get(char *s)
{
	ds_service(0);

	if (ds.client_fd < 0 || ds.queue_count == 0) {
		return 0;
	}
	strcpy(s, ds.queue[ds.queue_head]);
	ds.queue_head = (ds.queue_head + 1) % DS_QUEUE_MAX;
	ds.queue_count--;
	return 1;
}
