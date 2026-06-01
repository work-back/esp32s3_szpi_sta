/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(HTTP_DL, LOG_LEVEL_DBG);

#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/netdb.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>

#include <zephyr/shell/shell.h>

#define HTTP_PORT 80

#if defined(CONFIG_NET_CONFIG_HTTP_SRV_ADDR)
#define SERVER_ADDR4  CONFIG_NET_CONFIG_HTTP_SRV_ADDR
#else
#define SERVER_ADDR4 "192.168.10.111"
#endif

#define MAX_DL_RECV_BUF_LEN 1024

static __attribute__ ((section (".ext_ram.bss"))) uint8_t dl_recv_buf[MAX_DL_RECV_BUF_LEN];

static int http_dl_socket_init(const char *hostname, const char *port,
			                   int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret = 0;
	struct addrinfo hints;
	struct addrinfo *res = NULL;

	memset(addr, 0, addr_len);

	ret = getaddrinfo(hostname, port, &hints, &res);
	if (ret != 0) {
		return -1;
	}

	memcpy(addr, res->ai_addr, addr_len);

	*sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	if (*sock < 0) {
		LOG_ERR("Failed to create IPv4 HTTP socket (%d)", -errno);
	}

	return ret;
}

static int http_dl_response_cb(struct http_response *rsp,
		       enum http_final_call final_data,
		       void *user_data)
{
	if (rsp->body_found && rsp->body_frag_len > 0) {
		struct fs_file_t *file = (struct fs_file_t *)user_data;
		ssize_t written = fs_write(file, rsp->body_frag_start, rsp->body_frag_len);
		if (written < 0) {
            LOG_ERR("wirte to file failed: %d", (int)written);
            return written;
        }
        LOG_DBG("write : %zu bytes", rsp->body_frag_len);
	}

	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %d:%s", rsp->http_status_code, rsp->http_status);

	return 0;
}

static int http_dl_socket_connect(const char *hostname, const char *port,
			                      int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret;

	ret = http_dl_socket_init(hostname, port, sock, addr, addr_len);
	if (ret < 0 || *sock < 0) {
		return -1;
	}

    LOG_INF("try to connect to remote %s:%s", hostname, port);

	ret = connect(*sock, addr, addr_len);
	if (ret < 0) {
		LOG_ERR("Cannot connect to remote (%d)", -errno);
		close(*sock);
		*sock = -1;
		ret = -errno;
	}

	return ret;
}

static int http_dl_get_req(struct http_request *req, const char *hostname, const char *port, void *user_data)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 3 * MSEC_PER_SEC;
	int ret = 0;

	(void)http_dl_socket_connect(hostname, port, &sock4, (struct sockaddr *)&addr4, sizeof(addr4));
	if (sock4 < 0) {
		LOG_ERR("Cannot create HTTP IPv4 connection.");
		return -ECONNABORTED;
	}

	ret = http_client_req(sock4, req, timeout, user_data);
	if (ret < 0) {
		LOG_ERR("Client error %d", ret);
	}

	close(sock4);
	sock4 = -1;

    return 0;
}

int http_dl_file(const char *hostname, const char *port, const char *path, const char *save_path)
{
	struct fs_file_t file;
    fs_file_t_init(&file);
    int ret = fs_open(&file, save_path, FS_O_CREATE | FS_O_WRITE);
    if (ret < 0) {
        LOG_ERR("can not open %s: %d", save_path, ret);
        return ret;
    }

    struct http_request req;
    memset(&req, 0, sizeof(req));

	req.method = HTTP_GET;
    req.url = path;
	req.host = hostname;
	req.protocol = "HTTP/1.1";
	req.response = http_dl_response_cb;
	req.recv_buf = dl_recv_buf;
	req.recv_buf_len = sizeof(dl_recv_buf);

    http_dl_get_req(&req, hostname, port, &file);

	fs_close(&file);

    return 0;
}

static int cmd_http_dl(const struct shell *sh, size_t argc, char **argv)
{
    /* 
     * argv[0] = "download"
     * argv[1] = <hostname>
     * argv[2] = <port>
     * argv[3] = <path>
     * argv[4] = <save_path>
     */
    const char *hostname  = argv[1];
    const char *port      = argv[2];
    const char *path      = argv[3];
    const char *save_path = argv[4];

    shell_print(sh, "download ...");
    shell_print(sh, "server: %s:%s", hostname, port);
    shell_print(sh, "URL Path: %s", path);
    shell_print(sh, "Save Path: %s", save_path);

    /* 调用下载引擎 */
    int ret = http_dl_file(hostname, port, path, save_path);
    if (ret < 0) {
        shell_error(sh, "download failed : %d", ret);
        return ret;
    }

    shell_print(sh, "Download Success!");
    return 0;
}


SHELL_CMD_REGISTER(http_dl, NULL, 
	        		"Download file over HTTP\n"
        			"Usage: http_dl <hostname> <port> <path> <save_path>"
					, cmd_http_dl);

