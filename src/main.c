/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "touch_hid.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int err;

	LOG_INF("BLE multi-touch simulator starting");
	err = touch_hid_init();
	if (err) {
		LOG_ERR("Unable to start touch HID service: %d", err);
		return err;
	}

	return 0;
}
