/*
 * Copyright (c) 2024 Muhammad Haziq
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>

#include "wscli.h"

LOG_MODULE_REGISTER(STA, LOG_LEVEL_DBG);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define NET_EVENT_WIFI_MASK (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_if *sta_iface;

static struct wifi_connect_req_params sta_config;

static struct net_mgmt_event_callback cb;

/* Check necessary definitions */


BUILD_ASSERT(sizeof(CONFIG_WIFI_SAMPLE_SSID) > 1,
	     "CONFIG_WIFI_SAMPLE_SSID is empty. Please set it in conf file.");

#ifdef WIFI_WAIT_WITH_SEM
/* 1. 定义 WiFi 连接成功的信号量 */
/* 初始值为 0，表示初始状态为“未连接”，需要等待事件发生 */
K_SEM_DEFINE(wifi_connected_sem, 0, 1);
#endif


static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		LOG_INF("Connected to %s", CONFIG_WIFI_SAMPLE_SSID);
#ifdef WIFI_WAIT_WITH_SEM
		k_sem_give(&wifi_connected_sem);
#else
        evt_send(EVT_WIFI_CONNECTED, 0, NULL);
#endif
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		LOG_INF("Disconnected from %s", CONFIG_WIFI_SAMPLE_SSID);
		break;
	}
	default:
		break;
	}
}

static int connect_to_wifi(void)
{
	if (!sta_iface) {
		LOG_INF("STA: interface no initialized");
		return -EIO;
	}

	sta_config.ssid = (const uint8_t *)CONFIG_WIFI_SAMPLE_SSID;
	sta_config.ssid_length = sizeof(CONFIG_WIFI_SAMPLE_SSID) - 1;
	sta_config.psk = (const uint8_t *)CONFIG_WIFI_SAMPLE_PSK;
	sta_config.psk_length = sizeof(CONFIG_WIFI_SAMPLE_PSK) - 1;
	sta_config.security = WIFI_SECURITY_TYPE_PSK;
	sta_config.channel = WIFI_CHANNEL_ANY;
	sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("Connecting to SSID: %s\n", sta_config.ssid);

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_config,
			   sizeof(struct wifi_connect_req_params));
	if (ret) {
		LOG_ERR("Unable to Connect to (%s)", CONFIG_WIFI_SAMPLE_SSID);
	}

	return ret;
}

int sta_tryconnect(void)
{
	net_mgmt_init_event_callback(&cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
	net_mgmt_add_event_callback(&cb);

	sta_iface = net_if_get_wifi_sta();

	if (connect_to_wifi() != 0) {
		LOG_ERR("Unable to Connect to wifi!");
        return -1;
	}

#ifdef WIFI_WAIT_WITH_SEM
	/* K_FOREVER 表示一直等待，也可以设置超时时间如 K_SECONDS(300) */
	int wait_ret = k_sem_take(&wifi_connected_sem, K_SECONDS(300));
	if (wait_ret == 0) {
		LOG_INF("WiFi connected");
        return 0;
	} else {
		LOG_ERR("WiFi connection wait timed out or interrupted.");
        return -1;
	}
#else
    return 0;
#endif
}
