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

#include "wscli.h"

#define MAX_DL_RECV_BUF_LEN 1024

static __attribute__ ((section (".ext_ram.bss"))) uint8_t dl_recv_buf[MAX_DL_RECV_BUF_LEN];

struct download_ctx {
    struct fs_file_t file;       // 文件句柄
    const struct shell *sh;      // Shell 指针（用于在终端中输出）
    int64_t last_print_time;     // 上一次打印进度的时间戳
    size_t total_written;        // 已下载并写入的数据大小
    size_t total_size;           // 文件总大小（从 Content-Length 中获取）
    bool has_size;               // 是否成功获取到文件总大小
};

static int http_dl_socket_init(const char *hostname, const char *port,
			                   int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret = 0;
	struct addrinfo hints= {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};

	struct addrinfo *res = NULL;

	memset(addr, 0, addr_len);

	ret = getaddrinfo(hostname, port, &hints, &res);
	if (ret != 0) {
		LOG_ERR("Failed to getaddrinfo of %s:%s (%d)", hostname, port, -errno);
		return -1;
	}

	memcpy(addr, res->ai_addr, addr_len);

	*sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (*sock < 0) {
		LOG_ERR("Failed to create IPv4 HTTP socket (%d)", -errno);
	}

	return ret;
}

#if 0
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
		// LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);
	}

	if (rsp->http_status_code != 200) {
		LOG_INF("Response Error: %d:%s", rsp->http_status_code, rsp->http_status);
	}
	

	return 0;
}

#else

static int http_dl_response_cb(struct http_response *rsp,
                               enum http_final_call final_data,
                               void *user_data)
{
    struct download_ctx *ctx = (struct download_ctx *)user_data;

    // 1. 尝试获取文件大小
    if (rsp->cl_present && !ctx->has_size) {
        ctx->total_size = rsp->content_length;
        ctx->has_size = true;
    }

    // 2. 写入文件并累加计数器
    if (rsp->body_found && rsp->body_frag_len > 0) {
        ssize_t written = fs_write(&ctx->file, rsp->body_frag_start, rsp->body_frag_len);
        if (written < 0) {
            return written;
        }
        ctx->total_written += rsp->body_frag_len;
    }

    // 3. 时间节流逻辑
    int64_t now = k_uptime_get(); // 获取当前系统毫秒数
    bool is_final = (final_data == HTTP_DATA_FINAL);

    // 限制：仅在【时间超过 1 秒】或【最后一次完成包】时才执行打印
    if (is_final || (now - ctx->last_print_time >= 1000)) {
        ctx->last_print_time = now;

        if (ctx->has_size && ctx->total_size > 0) {
            // 计算百分比
            int percent = (int)((uint64_t)ctx->total_written * 100 / ctx->total_size);
            if (percent > 100) percent = 100;

            // 构建一个简易的 ASCII 进度条 (宽度为 20 个字符)
            char bar[21];
            int filled = percent / 5; // 20格 * 5% = 100%
            for (int i = 0; i < 20; i++) {
                bar[i] = (i < filled) ? '=' : ' ';
            }
            bar[20] = '\0';

            // \r 使光标回到行首实现原地刷新，并在末尾留几个空格擦除可能存在的多余字符
            shell_fprintf(ctx->sh, SHELL_NORMAL, 
                          "\r[%s] %3d%% (%zu/%zu B)   ", 
                          bar, percent, ctx->total_written, ctx->total_size);
        } else {
            // 分块传输等未知大小的情况
            shell_fprintf(ctx->sh, SHELL_NORMAL, 
                          "\rDownloaded: %zu B...   ", 
                          ctx->total_written);
        }

        // 如果是最后一包，强制换行，让输出保持整齐
        if (is_final) {
            shell_fprintf(ctx->sh, SHELL_NORMAL, "\n[INFO] 下载完成。\n");
        }
    }

    return 0;
}

#endif

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
	int32_t timeout = 5 * 60 * MSEC_PER_SEC;
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

int http_dl_file(const struct shell *sh, const char *hostname, const char *port, const char *path, const char *save_path)
{
	// struct fs_file_t file;
	struct download_ctx ctx = {
        .sh = sh,
        .last_print_time = 0,
        .total_written = 0,
        .total_size = 0,
        .has_size = false
    };
    fs_file_t_init(&(ctx.file));

	char full_path[128];
	const char * root_path = fatfs_get_root_path();

	snprintf(full_path, sizeof(full_path), "%s/%s", root_path, save_path);

    int ret = fs_open(&(ctx.file), full_path, FS_O_CREATE | FS_O_WRITE);
    if (ret < 0) {
        LOG_ERR("can not open %s: %d", full_path, ret);
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

    http_dl_get_req(&req, hostname, port, &ctx);

	fs_close(&(ctx.file));

    return 0;
}

static int cmd_http_dl(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 5) return -EINVAL;

    const char *hostname  = argv[1];
    const char *port      = argv[2];
    const char *path      = argv[3];
    const char *save_path = argv[4];

    shell_print(sh, "download ...");
    shell_print(sh, "server: %s:%s", hostname, port);
    shell_print(sh, "URL Path: %s", path);
    shell_print(sh, "Save Path: %s", save_path);

    /* 调用下载引擎 */
    int ret = http_dl_file(sh, hostname, port, path, save_path);
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

