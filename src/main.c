#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/eventfd.h>
#include <zephyr/posix/poll.h>

#include <zephyr/misc/lorem_ipsum.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include "wscli.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

static int recv_with_poll(int sock, uint8_t *buf, size_t buf_len)
{
	struct pollfd fds = {
		.fd = sock,
		.events = ZSOCK_POLLIN,
	};
	int ret;

	ret = poll(&fds, 1, 30 * 1000);
	if (ret < 0) {
		return ret;
	}

	if (ret == 0) {
		LOG_DBG("zsock_poll timeout!");
		return -EAGAIN;
	}

	if (fds.revents & ZSOCK_POLLNVAL) {
		return -EBADF;
	}

	if (fds.revents & ZSOCK_POLLERR) {
		return -EIO;
	}

	if (fds.revents & ZSOCK_POLLIN) {
		wscli_recv(sock, buf, buf_len);
	}

	return 0;
}

static bool send_and_wait_msg(int sock, uint8_t *buf, size_t buf_len)
{
	int ret;

	if (sock < 0) {
		return true;
	}

	ret = snprintf(buf, buf_len, "%s", "hello SRV");;

	ret = wscli_send(sock, buf, ret);
	if (ret <= 0) {
		if (ret < 0) {
			LOG_ERR("failed to send data using %s (%d)", "ws API", ret);
		} else {
			LOG_DBG("connection closed");
		}

		return false;
	} else {
		LOG_DBG("sent %d bytes", ret);
	}


	recv_with_poll(sock, buf, buf_len);

	LOG_DBG("receive [%s]", buf);

	return true;
}

int main(void)
{
	k_sleep(K_SECONDS(2));

	int wait_ret = sta_tryconnect();
	if (wait_ret != 0) {
		LOG_ERR("WiFi connection failed.");
		k_sleep(K_FOREVER);
	}

	k_sleep(K_SECONDS(1));
    LOG_INF("WiFi connected, starting WSCLI...");

	wscli_init();

    int websock = wscli_getsock();
    if (websock < 0) {
		LOG_ERR("invalid websock");
		k_sleep(K_FOREVER);
    }

    uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];

	while (1) {
		if (!send_and_wait_msg(websock, recv_buf_ipv4, sizeof(recv_buf_ipv4))) {
			break;
		}

		k_sleep(K_MSEC(250));
	}


	wscli_fini();

	return 0;
}
