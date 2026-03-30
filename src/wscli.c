/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(WSCLI, LOG_LEVEL_DBG);

#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/eventfd.h>
#include <zephyr/posix/poll.h>

#include <zephyr/misc/lorem_ipsum.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/websocket.h>
#include <zephyr/random/random.h>
#include <zephyr/shell/shell.h>

#include "wscli.h"

#define SERVER_PORT 9001


#if defined(CONFIG_NET_CONFIG_PEER_IPV4_ADDR)
#define SERVER_ADDR4  CONFIG_NET_CONFIG_PEER_IPV4_ADDR
#else
#define SERVER_ADDR4 "192.168.66.155"
#endif

/* We need to allocate bigger buffer for the websocket data we receive so that
 * the websocket header fits into it.
 */
#define EXTRA_BUF_SPACE 30

static uint8_t temp_recv_buf_ipv4[MAX_RECV_BUF_LEN + EXTRA_BUF_SPACE];

static int setup_socket(sa_family_t family, const char *server, int port,
			int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	const char *family_str = family == AF_INET ? "IPv4" : "IPv6";
	int ret = 0;

	memset(addr, 0, addr_len);

	if (family == AF_INET) {
		net_sin(addr)->sin_family = AF_INET;
		net_sin(addr)->sin_port = htons(port);
		inet_pton(family, server, &net_sin(addr)->sin_addr);
	} else {
		net_sin6(addr)->sin6_family = AF_INET6;
		net_sin6(addr)->sin6_port = htons(port);
		inet_pton(family, server, &net_sin6(addr)->sin6_addr);
	}

	*sock = socket(family, SOCK_STREAM, IPPROTO_TCP);


	if (*sock < 0) {
		LOG_ERR("Failed to create %s HTTP socket (%d)", family_str, -errno);
	}

	return ret;
}

static int connect_socket(sa_family_t family, const char *server, int port,
			              int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret;

	ret = setup_socket(family, server, port, sock, addr, addr_len);
	if (ret < 0 || *sock < 0) {
		return -1;
	}

	ret = connect(*sock, addr, addr_len);
	if (ret < 0) {
		LOG_ERR("socket cannot connect to %s:%d remote, errno: (%d)\n", server, port, errno);
		ret = -1;
	}

	return ret;
}

static int connect_cb(int sock, struct http_request *req, void *user_data)
{
	LOG_INF("Websocket %d for %s connected.", sock, (char *)user_data);

	return 0;
}

#if 0
static ssize_t sendall_with_ws_api(int sock, const void *buf, size_t len)
{
	return websocket_send_msg(sock, buf, len, WEBSOCKET_OPCODE_DATA_TEXT,
				  true, true, SYS_FOREVER_MS);
}
#endif

ssize_t wscli_send(int sock, const void *buf, size_t len)
{
	return send(sock, buf, len, 0);
}

int wscli_recv(int sock, uint8_t *buf, size_t buf_len)
{
	int ret, read_pos;

	read_pos = 0;

	while (1) {
		ret = recv(sock, buf + read_pos, buf_len - read_pos, 0);
		if (ret <= 0) {
			if (errno == EAGAIN || errno == ETIMEDOUT) {
				k_sleep(K_MSEC(50));
				continue;
			}

			LOG_DBG("connection closed while waiting (%d/%d)", ret, errno);
			break;
		}

		read_pos += ret;
		break;
	}

    return read_pos;
}

int g_sock4 = -1;
int g_websock4 = -1;

int wscli_init(void)
{
	/* Just an example how to set extra headers */
	const char *extra_headers[] = {
		"Origin: http://foobar\r\n",
		NULL
	};

	int32_t timeout = 3 * MSEC_PER_SEC;
	struct sockaddr_in addr4;

	if (!IS_ENABLED(CONFIG_NET_IPV4)) {
		LOG_ERR("Ipv4 is not enabled.");
		return -1;
	}

	if (connect_socket(AF_INET, SERVER_ADDR4, SERVER_PORT,
                       &g_sock4, (struct sockaddr *)&addr4, sizeof(addr4))) {
		LOG_ERR("connect_socket failed.");
		return -1;
    }

	if (g_sock4 < 0) {
		LOG_ERR("connect_socket failed.");
		return -1;
	}


	char buf[128];
	struct websocket_request req;

	memset(&req, 0, sizeof(req));

	snprintk(buf, sizeof(buf), "%s:%u", SERVER_ADDR4, SERVER_PORT);

	req.host = buf;
	req.url = "/";
	req.optional_headers = extra_headers;
	req.cb = connect_cb;
	req.tmp_buf = temp_recv_buf_ipv4;
	req.tmp_buf_len = sizeof(temp_recv_buf_ipv4);

	LOG_INF("req.host : %s", req.host);
	LOG_INF("req.url : %s", req.url);
	LOG_INF("req.tmp_buf_len : %d", req.tmp_buf_len);

	g_websock4 = websocket_connect(g_sock4, &req, timeout, "IPv4");
	if (g_websock4 < 0) {
		LOG_ERR("Cannot connect to %s:%d, websocket:%d", SERVER_ADDR4, SERVER_PORT, g_websock4);
		close(g_sock4);
		g_sock4 = -1;
		return -1;
	}

	LOG_INF("Websocket IPv4 %d", g_websock4);

return 0;
}

int wscli_fini(void)
{
	if (g_websock4 >= 0) {
		close(g_websock4);
		g_websock4 = -1;
	}

    if (g_sock4 >= 0) {
		close(g_sock4);
		g_sock4 = -1;
	}

	return 0;
}

int wscli_getsock(void)
{
    return g_websock4;
}
