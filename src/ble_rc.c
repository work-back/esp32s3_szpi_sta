/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/settings/settings.h>

#include "touch_hid.h"

LOG_MODULE_REGISTER(touch_hid, LOG_LEVEL_INF);

#define TOUCH_REPORT_ID 1
#define TOUCH_CONTACTS  3

struct hid_info {
	uint16_t bcd_hid;
	uint8_t country_code;
	uint8_t flags;
} __packed;

struct hid_report_ref {
	uint8_t id;
	uint8_t type;
} __packed;

struct touch_contact {
	uint8_t flags;
	uint8_t id;
	uint16_t x;
	uint16_t y;
} __packed;

struct touch_report {
	struct touch_contact contact[TOUCH_CONTACTS];
	uint8_t contact_count;
} __packed;

BUILD_ASSERT(sizeof(struct touch_report) == 19,
	     "The touch report must fit in the default 23-byte ATT MTU");

static const struct hid_info hid_info = {
	.bcd_hid = 0x0111,
	.country_code = 0,
	.flags = 0x03,
};

static const struct hid_report_ref input_report_ref = {
	.id = TOUCH_REPORT_ID,
	.type = 0x01,
};

/*
 * Report ID 1 contains three independent finger collections followed by the
 * total number of active contacts. The resulting 19-byte report fits in the
 * default 23-byte ATT MTU, so it works before a phone requests a larger MTU.
 * Coordinates use the 0..32767 HID range.
 */
static const uint8_t report_map[] = {
	0x05, 0x0d, 0x09, 0x04, 0xa1, 0x01, 0x85, TOUCH_REPORT_ID,
#define FINGER_COLLECTION \
	0x05, 0x0d, \
	0x09, 0x22, 0xa1, 0x02, \
	0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, \
	0x09, 0x32, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, \
	0x75, 0x06, 0x95, 0x01, 0x81, 0x03, \
	0x09, 0x51, 0x15, 0x00, 0x25, TOUCH_CONTACTS - 1, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, \
	0x05, 0x01, 0x09, 0x30, 0x16, 0x00, 0x00, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, \
	0x09, 0x31, 0x16, 0x00, 0x00, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, \
	0xc0
	FINGER_COLLECTION,
	FINGER_COLLECTION,
	FINGER_COLLECTION,
#undef FINGER_COLLECTION
	0x05, 0x0d, 0x09, 0x54, 0x15, 0x00, 0x25, TOUCH_CONTACTS,
	0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xc0,
};

static struct touch_report touch_report;
/* Logical slots retain their Contact IDs; HID reports pack active contacts first. */
static struct touch_contact touch_state[TOUCH_CONTACTS];
static bool notifications_enabled;
static uint8_t control_point;
static struct bt_conn *active_conn;

static void advertising_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(advertising_work, advertising_work_handler);

static ssize_t read_hid_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &hid_info, sizeof(hid_info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map, sizeof(report_map));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &input_report_ref,
				 sizeof(input_report_ref));
}

static ssize_t read_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				  void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &touch_report,
				 sizeof(touch_report));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Touch notifications %s", notifications_enabled ? "enabled" : "disabled");
}

static ssize_t write_control_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				   const void *buf, uint16_t len, uint16_t offset,
				   uint8_t flags)
{
	if (offset != 0 || len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	control_point = ((const uint8_t *)buf)[0];
	return len;
}

BT_GATT_SERVICE_DEFINE(hid_service,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_hid_info, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ_ENCRYPT, read_report_map, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL, NULL),
	BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT,
			   read_report_ref, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE_ENCRYPT, NULL, write_control_point, &control_point)
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xc0, 0x03),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		LOG_ERR("Advertising failed: %d", err);
	} else {
		LOG_INF("Advertising as %s", CONFIG_BT_DEVICE_NAME);
	}
}

static void advertising_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	start_advertising();
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed: 0x%02x", err);
		k_work_reschedule(&advertising_work, K_MSEC(200));
		return;
	}

	active_conn = bt_conn_ref(conn);
	LOG_INF("Host connected");
	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_ERR("Could not request encryption: %d", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	notifications_enabled = false;
	if (active_conn != NULL) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}
	LOG_INF("Host disconnected: 0x%02x", reason);
	/* Let the controller retire the disconnect command before advertising. */
	k_work_reschedule(&advertising_work, K_MSEC(200));
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	ARG_UNUSED(conn);
	if (err) {
		LOG_ERR("Pairing/encryption failed: %s", bt_security_err_to_str(err));
	} else {
		LOG_INF("Link encrypted (security level %u)", level);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

int touch_hid_send(uint8_t slot, bool down, uint16_t x, uint16_t y)
{
	struct touch_contact *contact;
	int err;

	if (slot >= TOUCH_CONTACTS || x > 32767 || y > 32767) {
		return -EINVAL;
	}

	contact = &touch_state[slot];
	contact->flags = down ? BIT(0) | BIT(1) : 0;
	contact->id = slot;
	contact->x = x;
	contact->y = y;

	/*
	 * Android consumes only the first Contact Count finger collections.  The
	 * physical order in this fixed-size report must therefore not be the
	 * logical slot number: an isolated slot 2 contact has to be record 0.
	 */
	memset(&touch_report, 0, sizeof(touch_report));
	touch_report.contact_count = 0;
	for (size_t i = 0; i < TOUCH_CONTACTS; i++) {
		if (touch_state[i].flags & BIT(0)) {
			touch_report.contact[touch_report.contact_count++] = touch_state[i];
		}
	}

	if (active_conn == NULL || !notifications_enabled) {
		return -ENOTCONN;
	}

	/* The input-report value attribute is index 6 in this fixed service. */
	err = bt_gatt_notify(active_conn, &hid_service.attrs[6], &touch_report,
			     sizeof(touch_report));
	return err;
}

static int cmd_touch(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long slot;
	unsigned long down;
	unsigned long x;
	unsigned long y;
	int err;

	if (argc != 5) {
		shell_error(sh, "usage: touch <slot 0-2> <down 0|1> <x 0-32767> <y 0-32767>");
		return -EINVAL;
	}

	slot = strtoul(argv[1], &end, 0); if (*end) return -EINVAL;
	down = strtoul(argv[2], &end, 0); if (*end || down > 1) return -EINVAL;
	x = strtoul(argv[3], &end, 0); if (*end || x > 32767) return -EINVAL;
	y = strtoul(argv[4], &end, 0); if (*end || y > 32767) return -EINVAL;
	err = touch_hid_send(slot, down, x, y);
	if (err) {
		shell_error(sh, "report not sent: %d", err);
	} else {
		shell_print(sh, "slot %lu %s at %lu,%lu", slot, down ? "down" : "up", x, y);
	}
	return err;
}

SHELL_CMD_REGISTER(touch, NULL, "Send touch: touch <slot> <down> <x> <y>", cmd_touch);

int touch_hid_init(void)
{
	int err = bt_enable(NULL);

	if (err) {
		return err;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
		if (err) {
			LOG_ERR("Unable to load Bluetooth settings: %d", err);
			return err;
		}
	}

	for (size_t i = 0; i < TOUCH_CONTACTS; i++) {
		touch_state[i].id = i;
	}

	start_advertising();
	return 0;
}
