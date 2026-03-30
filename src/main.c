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

uint8_t recv_buf[MAX_RECV_BUF_LEN];
uint8_t send_buf[MAX_RECV_BUF_LEN];

sys_slist_t evt_list;
struct k_mutex evt_list_lock;
struct evt_msg {
    sys_snode_t node;
    unsigned int type;
    unsigned int len;
    void *data;
};

enum {
    POLLFD_T_SOCKET  = 0,
    POLLFD_T_EVENTFD = 1,

    __POLLFD_T_MAX__,
};

static struct pollfd fds[__POLLFD_T_MAX__];

static void timer_looper_thrd(void);
static bool send_msg(int sock, uint8_t *buf, size_t buf_len);

#if defined(CONFIG_NET_TC_THREAD_PREEMPTIVE)
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#else
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#endif

#define STACK_SIZE 2048

K_THREAD_DEFINE(timer_thread_id, STACK_SIZE,
		timer_looper_thrd, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

enum {
    EVT_BASE = 0x1000,
    EVT_TIMER_OUT,
    EVT_WIFI_CONNECTED = 0x2000,
};

int evt_send(unsigned int type, unsigned int len, void *data) {

    struct evt_msg *msg = k_malloc(sizeof(struct evt_msg));
    msg->type = type;
    msg->len = len;
    msg->data = data;

    k_mutex_lock(&evt_list_lock, K_FOREVER);
    sys_slist_append(&evt_list, &msg->node);
    k_mutex_unlock(&evt_list_lock);

    if (fds[POLLFD_T_EVENTFD].fd >= 0) {
        eventfd_write(fds[POLLFD_T_EVENTFD].fd, 1);
    }
}

void evt_handle(void)
{
    k_mutex_lock(&evt_list_lock, K_FOREVER);

    sys_snode_t *node;
    while ((node = sys_slist_get(&evt_list)) != NULL) {
        struct evt_msg *msg = CONTAINER_OF(node, struct evt_msg, node);

        if (msg) {
            if (msg->type == EVT_TIMER_OUT) {
                send_msg(fds[POLLFD_T_SOCKET].fd, send_buf, sizeof(send_buf));
            }
        }

        k_free(msg);
    }

    k_mutex_unlock(&evt_list_lock);

    return;
}

static void evt_list_init(void)
{
    sys_slist_init(&evt_list);
    k_mutex_init(&evt_list_lock);

    return;
}

static void timer_looper_thrd(void)
{
    LOG_INF("timer_looper_thrd start ...");

    while(1) {
        k_sleep(K_SECONDS(1));

        evt_send(EVT_TIMER_OUT, 0, NULL);

        LOG_INF("Sent signal to main thread\n");
    }

    return;
}

static int poll_loop(void)
{
	int ret;

	ret = poll(fds, 2, 30 * 1000);
	if (ret < 0) {
		return ret;
	}

	if (ret == 0) {
		LOG_DBG("poll timeout!");
		return -EAGAIN;
	}

    LOG_DBG("poll event GOT!");

	if (fds[POLLFD_T_SOCKET].revents & POLLNVAL) {
		return -EBADF;
	}

	if (fds[POLLFD_T_SOCKET].revents & POLLERR) {
		return -EIO;
	}

	if (fds[POLLFD_T_SOCKET].revents & POLLIN) {
		wscli_recv(fds[POLLFD_T_SOCKET].fd, recv_buf, sizeof(recv_buf));
        LOG_DBG("receive [%s]", recv_buf);
	}

	if (fds[POLLFD_T_EVENTFD].revents) {
		eventfd_t value;

		eventfd_read(fds[POLLFD_T_EVENTFD].fd, &value);
		LOG_DBG("Received event.");

        evt_handle();

        //send_msg(fds[POLLFD_T_SOCKET].fd, send_buf, sizeof(send_buf));
	}

	return 0;
}

static bool send_msg(int sock, uint8_t *buf, size_t buf_len)
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

	return true;
}

int main(void)
{
    evt_list_init();

	k_sleep(K_SECONDS(2));

    fds[POLLFD_T_SOCKET].fd = -1;
	fds[POLLFD_T_EVENTFD].fd = -1;

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
		LOG_ERR("invalid websock.");
		k_sleep(K_FOREVER);
    }

    fds[POLLFD_T_SOCKET].fd = websock;
    fds[POLLFD_T_SOCKET].events = POLLIN;

    int evfd = eventfd(0, 0);
    if (evfd < 0) {
        LOG_ERR("eventfd failed: %d, errno: %d", evfd, errno);
		k_sleep(K_FOREVER);
    }
	fds[POLLFD_T_EVENTFD].fd = evfd;
	fds[POLLFD_T_EVENTFD].events = POLLIN;

	k_thread_start(timer_thread_id);

    ble_rc_init();

	while (1) {
        poll_loop();

		k_sleep(K_MSEC(250));
	}

    // k_thread_priority_set(k_current_get(), THREAD_PRIORITY);

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	wscli_fini();

	return 0;
}
