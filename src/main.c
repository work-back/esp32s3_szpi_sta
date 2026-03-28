#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wscli.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

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

    while(1) {
        k_sleep(K_SECONDS(5));
    }

	wscli_fini();

	return 0;
}
