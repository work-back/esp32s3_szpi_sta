/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(HTTP_CLIENT, LOG_LEVEL_DBG);

#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/arpa/inet.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>

#define HTTP_PORT 80

#if defined(CONFIG_NET_CONFIG_HTTP_SRV_ADDR)
#define SERVER_ADDR4  CONFIG_NET_CONFIG_HTTP_SRV_ADDR
#else
#define SERVER_ADDR4 "192.168.10.111"
#endif

#define MAX_RECV_BUF_LEN 512

static __attribute__ ((section (".ext_ram.bss"))) uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];

static int setup_socket(const char *server, int port,
			            int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret = 0;

	memset(addr, 0, addr_len);

	net_sin(addr)->sin_family = AF_INET;
	net_sin(addr)->sin_port = htons(port);
	inet_pton(AF_INET, server, &net_sin(addr)->sin_addr);

	*sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (*sock < 0) {
		LOG_ERR("Failed to create IPv4 HTTP socket (%d)", -errno);
	}

	return ret;
}

#if 0
static int payload_cb(int sock, struct http_request *req, void *user_data)
{
	const char *content[] = {
		"foobar",
		"chunked",
		"last"
	};
	char tmp[64];
	int i, pos = 0;

	for (i = 0; i < ARRAY_SIZE(content); i++) {
		pos += snprintk(tmp + pos, sizeof(tmp) - pos,
				"%x\r\n%s\r\n",
				(unsigned int)strlen(content[i]),
				content[i]);
	}

	pos += snprintk(tmp + pos, sizeof(tmp) - pos, "0\r\n\r\n");

	(void)send(sock, tmp, pos, 0);

	return pos;
}
#endif

static int response_cb(struct http_response *rsp,
		       enum http_final_call final_data,
		       void *user_data)
{
	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %d:%s", rsp->http_status_code, rsp->http_status);
    rsp->recv_buf[rsp->data_len] = '\0';
    LOG_INF("Response data %s", (const char *)rsp->recv_buf);

	return 0;
}

static int connect_socket(const char *server, int port,
			              int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret;

	ret = setup_socket(server, port, sock, addr, addr_len);
	if (ret < 0 || *sock < 0) {
		return -1;
	}

    LOG_INF("try to connect to remote %s:%d", server, port);

	ret = connect(*sock, addr, addr_len);
	if (ret < 0) {
		LOG_ERR("Cannot connect to remote (%d)", -errno);
		close(*sock);
		*sock = -1;
		ret = -errno;
	}

	return ret;
}

int run_http_get_req(struct http_request *req, const char *server, int port)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 3 * MSEC_PER_SEC;
	int ret = 0;

	(void)connect_socket(server, port,
				&sock4, (struct sockaddr *)&addr4,
				sizeof(addr4));
	if (sock4 < 0) {
		LOG_ERR("Cannot create HTTP IPv4 connection.");
		return -ECONNABORTED;
	}

	ret = http_client_req(sock4, req, timeout, "IPv4 GET");
	if (ret < 0) {
		LOG_ERR("Client error %d", ret);
	}

	close(sock4);
	sock4 = -1;

    return 0;
}

int run_get_SK_v(void)
{
    struct http_request req;

    memset(&req, 0, sizeof(req));

	req.method = HTTP_GET;
	req.url = "/get?value=Relay";
	req.host = SERVER_ADDR4;
	req.protocol = "HTTP/1.1";
	req.response = response_cb;
	req.recv_buf = recv_buf_ipv4;
	req.recv_buf_len = sizeof(recv_buf_ipv4);

    run_http_get_req(&req, SERVER_ADDR4, HTTP_PORT);

    return 0;
}

int run_set_SK_v(uint8_t v)
{
    struct http_request req;

    memset(&req, 0, sizeof(req));

	req.method = HTTP_GET;
    if (v == 0) {
        req.url = "/set?Relay=0";
    } else {
        req.url = "/set?Relay=1";
    }
	req.host = SERVER_ADDR4;
	req.protocol = "HTTP/1.1";
	req.response = response_cb;
	req.recv_buf = recv_buf_ipv4;
	req.recv_buf_len = sizeof(recv_buf_ipv4);

    run_http_get_req(&req, SERVER_ADDR4, HTTP_PORT);

    return 0;
}

int run_queries(void)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 3 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTP_PORT;
	struct http_request req;

	/* Do a GET request */

	(void)connect_socket(SERVER_ADDR4, port,
				&sock4, (struct sockaddr *)&addr4,
				sizeof(addr4));
	if (sock4 < 0) {
		LOG_ERR("Cannot create HTTP IPv4 connection.");
		return -ECONNABORTED;
	}

	memset(&req, 0, sizeof(req));

	req.method = HTTP_GET;
	req.url = "/get?value=Relay";
	req.host = SERVER_ADDR4;
	req.protocol = "HTTP/1.1";
	req.response = response_cb;
	req.recv_buf = recv_buf_ipv4;
	req.recv_buf_len = sizeof(recv_buf_ipv4);

	ret = http_client_req(sock4, &req, timeout, "IPv4 GET");
	if (ret < 0) {
		LOG_ERR("Client error %d", ret);
	}

	close(sock4);
	sock4 = -1;

     #if 0

	/* Do a POST request */

	(void)connect_socket(SERVER_ADDR4, port,
				&sock4, (struct sockaddr *)&addr4,
				sizeof(addr4));
	if (sock4 < 0) {
		LOG_ERR("Cannot create HTTP IPv4 connection.");
		return -ECONNABORTED;
	}

	memset(&req, 0, sizeof(req));

	req.method = HTTP_POST;
	req.url = "/foobar";
	req.host = SERVER_ADDR4;
	req.protocol = "HTTP/1.1";
	req.payload = "foobar";
	req.payload_len = strlen(req.payload);
	req.response = response_cb;
	req.recv_buf = recv_buf_ipv4;
	req.recv_buf_len = sizeof(recv_buf_ipv4);

	ret = http_client_req(sock4, &req, timeout, "IPv4 POST");
	if (ret < 0) {
		LOG_ERR("Client error %d", ret);
	}

	close(sock4);
	sock4 = -1;

   
	/* Do a chunked POST request */

	const char *headers[] = {
		"Transfer-Encoding: chunked\r\n",
		NULL
	};

	(void)connect_socket(SERVER_ADDR4, port,
				&sock4, (struct sockaddr *)&addr4,
				sizeof(addr4));
	if (sock4 < 0) {
		LOG_ERR("Cannot create HTTP IPv4 connection.");
		return -ECONNABORTED;
	}

	memset(&req, 0, sizeof(req));

	req.method = HTTP_POST;
	req.url = "/chunked-test";
	req.host = SERVER_ADDR4;
	req.protocol = "HTTP/1.1";
	req.payload_cb = payload_cb;
	req.header_fields = headers;
	req.response = response_cb;
	req.recv_buf = recv_buf_ipv4;
	req.recv_buf_len = sizeof(recv_buf_ipv4);

	ret = http_client_req(sock4, &req, timeout, "IPv4 POST");
	if (ret < 0) {
		LOG_ERR("Client error %d", ret);
	}

	close(sock4);
    sock4 = -1;
    #endif

	return ret;
}
