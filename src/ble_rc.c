#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/shell/shell.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/controller.h>

#include "wscli.h"

LOG_MODULE_REGISTER(BLERC, LOG_LEVEL_DBG);

enum {
    BTRC_ST_DISCONNECTE = 0,
    BTRC_ST_CONNECTED,
};

static volatile int g_btrc_st = BTRC_ST_DISCONNECTE;

enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

/* HID 信息 */
struct hids_info {
    uint16_t bcd_hid;
    uint8_t  b_country_code;
    uint8_t  flags;
} __packed;

struct hids_report {
    uint8_t id; /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .bcd_hid = 0x0111,
    .b_country_code = 0x00,
    .flags = 0x01,
};

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_FEATURE = 0x03,
};

static struct hids_report input = {
    .id = 0x01,
    .type = HIDS_INPUT,
};

static uint8_t simulate_input_allow;
static uint8_t ctrl_point;

/* HID 报告描述符: 键盘 */
#if 0
static const uint8_t report_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) - 通用桌面设备 */
    0x09, 0x06,       /* Usage (Keyboard) - 明确是一个键盘 */
    0xa1, 0x01,       /* Collection (Application) - 开始应用集合 */

        /* Byte 0 */
        0x05, 0x07,       /* Usage Page (Key Codes) - 使用按键码页面 */
        0x19, 0xe0,       /* Usage Minimum (224) - 对应左 Ctrl */
        0x29, 0xe7,       /* Usage Maximum (231) - 对应右 GUI (Win键) */
        0x15, 0x00,       /* Logical Minimum (0) - 最小值 0 (没按) */
        0x25, 0x01,       /* Logical Maximum (1) - 最大值 1 (按了) */
        0x75, 0x01,       /* Report Size (1) - 每个按键占 1 bit */
        0x95, 0x08,       /* Report Count (8) - 总共 8 个按键 (刚好 1 字节) */
        0x81, 0x02,       /* Input (Data, Variable, Absolute) - 变量输入 */


        /* Byte 1 */
        0x95, 0x01,       /* Report Count (1) - 1 个单位 */
        0x75, 0x08,       /* Report Size (8) - 占 8 bit (1 字节) */
        0x81, 0x01,       /* Input (Constant) - 常量输入 (填充用) */

        /* Byte 2-7 */
        0x95, 0x06,       /* Report Count (6) - 允许同时按下 6 个键 */
        0x75, 0x08,       /* Report Size (8) - 每个键占 8 bit (1 字节) */
        0x15, 0x00,       /* Logical Minimum (0) */
        0x25, 0xff,       /* Logical Maximum (255) - 允许的最大键值是 255 (0xff) */
        0x05, 0x07,       /* Usage Page (Key Codes) */
        0x19, 0x00,       /* Usage Minimum (0) */
        0x29, 0xff,       /* Usage Maximum (255) - 允许的最大 Usage 也是 255 (0xff) */
        0x81, 0x00,       /* Input (Data, Array) - 数组输入 */

    0xc0              /* End Collection - 结束应用集合 */
};
#endif

/* copyed from miRC :
 * $ adb shell cat /sys/class/input/event2/device/device/report_descriptor > mirc.bin
 * $ hid-decode mirc.bin
 * */
static const uint8_t report_map[] = {
    0x05, 0x01,                    // Usage Page (Generic Desktop)        0
    0x09, 0x06,                    // Usage (Keyboard)                    2
    0xa1, 0x01,                    // Collection (Application)            4
    0x05, 0x07,                    //  Usage Page (Keyboard)              6
    0x09, 0x06,                    //  Usage (c and C)                    8
    0xa1, 0x01,                    //  Collection (Application)           10
    0x85, 0x01,                    //   Report ID (1)                     12
    0x95, 0x03,                    //   Report Count (3)                  14
    0x75, 0x10,                    //   Report Size (16)                  16
    0x15, 0x00,                    //   Logical Minimum (0)               18
    0x25, 0xfe,                    //   Logical Maximum (254)             20
    0x19, 0x00,                    //   Usage Minimum (0)                 22
    0x29, 0xfe,                    //   Usage Maximum (254)               24
    0x81, 0x00,                    //   Input (Data,Arr,Abs)              26
    0xc0,                          //  End Collection                     28
    0x06, 0x00, 0xff,              //  Usage Page (Vendor Defined Page 1) 29
    0x09, 0x00,                    //  Usage (Undefined)                  32
    0xa1, 0x01,                    //  Collection (Application)           34
    0x85, 0x06,                    //   Report ID (6)                     36
    0x75, 0x08,                    //   Report Size (8)                   38
    0x95, 0x78,                    //   Report Count (120)                40
    0x15, 0x00,                    //   Logical Minimum (0)               42
    0x25, 0xff,                    //   Logical Maximum (255)             44
    0x19, 0x00,                    //   Usage Minimum (0)                 46
    0x29, 0xff,                    //   Usage Maximum (255)               48
    0x81, 0x00,                    //   Input (Data,Arr,Abs)              50
    0x85, 0x07,                    //   Report ID (7)                     52
    0x75, 0x08,                    //   Report Size (8)                   54
    0x95, 0x78,                    //   Report Count (120)                56
    0x15, 0x00,                    //   Logical Minimum (0)               58
    0x25, 0xff,                    //   Logical Maximum (255)             60
    0x19, 0x00,                    //   Usage Minimum (0)                 62
    0x29, 0xff,                    //   Usage Maximum (255)               64
    0x81, 0x00,                    //   Input (Data,Arr,Abs)              66
    0x85, 0x08,                    //   Report ID (8)                     68
    0x75, 0x08,                    //   Report Size (8)                   70
    0x95, 0x78,                    //   Report Count (120)                72
    0x15, 0x00,                    //   Logical Minimum (0)               74
    0x25, 0xff,                    //   Logical Maximum (255)             76
    0x19, 0x00,                    //   Usage Minimum (0)                 78
    0x29, 0xff,                    //   Usage Maximum (255)               80
    0x81, 0x00,                    //   Input (Data,Arr,Abs)              82
    0xc0,                          //  End Collection                     84
    0xc0,                          // End Collection                      85
};


static volatile bool is_adv_running;
static struct k_work adv_work;

static ssize_t read_info(struct bt_conn *conn,
              const struct bt_gatt_attr *attr, void *buf,
              uint16_t len, uint16_t offset)
{
    printk("[GATT SRV CB] ----> Handle [%s]\n", __func__);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn,
                   const struct bt_gatt_attr *attr, void *buf,
                   uint16_t len, uint16_t offset)
{
    printk("[GATT SRV CB] ----> Handle [%s]\n", __func__);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map,
                 sizeof(report_map));
}

static ssize_t read_report(struct bt_conn *conn,
               const struct bt_gatt_attr *attr, void *buf,
               uint16_t len, uint16_t offset)
{
    printk("[GATT SRV CB] ----> Handle [%s]\n", __func__);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                 sizeof(struct hids_report));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    printk("[GATT SRV CB] ----> Handle [%s]\n", __func__);

    simulate_input_allow = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static ssize_t read_input_report(struct bt_conn *conn,
                 const struct bt_gatt_attr *attr, void *buf,
                 uint16_t len, uint16_t offset)
{
    printk("[GATT SRV CB] ----> Handle [%s]\n", __func__);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t write_ctrl_point(struct bt_conn *conn,
                const struct bt_gatt_attr *attr,
                const void *buf, uint16_t len, uint16_t offset,
                uint8_t flags)
{
    uint8_t *value = attr->user_data;

    printk("[GATT SRV CB] ----> Handle [%s]\n", __func__);

    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    return len;
}

int g_id = -1;
static volatile bool g_wakeup_adv_mode = false;

/* 定义广播数据 (Advertising Data) */
static const struct bt_data ad[] = {
    // 1. 设置 Flags：一般设为有限发现模式或普通发现模式
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    // 2. 设置外观 (Appearance)：384 表示 Generic Remote Control
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0x80, 0x01), // 小端序: 0x0180 = 384
    // 3. 广播 HID 服务 UUID
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

/* 定义扫描响应数据 (Scan Response Data) */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static uint8_t mfg_data[] = { 0x00, 0x01 };
static uint8_t fe_data[] = { 0x57, 0x11, 0x30 }; // A8:A0:92:30:11:57
static uint8_t uuid_data[] = { 0x00, 0x01, 0x02, 0x01, 0x05, 0x03, 0xff, 0x00, 0x01,
                               0xC8, 0x26, 0xE2, 0x17, 0xDC, 0x52,
                               // 0xA8, 0xA0, 0x92, 0x30, 0x11, 0x57,
							   0x00, };
static const struct bt_data ad_wakeup[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0x80, 0x01),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, 2),
	BT_DATA(0xfe, fe_data, 3),
	// BT_DATA(0x07, uuid_data, 16), // xiaomi Speaker wakeup
};

/* GATT 属性定义 */
static uint8_t report_val[8]; // 键盘报告数据缓冲区
BT_GATT_SERVICE_DEFINE(hid_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),

    // HID Info (Index 1, 2)
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_info, NULL, &info),

    // Report Map (Index 3, 4)
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_report_map, NULL, NULL),

    // Report Value (Index 5, 6)
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ /* _AUTHEN */ ,
                           /* _AUTHEN 对应的安全等级为 BT_SECURITY_L3, 否则 对端会有: Error Code: Insufficient Authentication (0x05) */
                           read_input_report, NULL, NULL),

    // CCC (Index 7)
    // BT_GATT_CCC(input_ccc_changed, SAMPLE_BT_PERM_READ | SAMPLE_BT_PERM_WRITE),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ /* _AUTHEN */ | BT_GATT_PERM_WRITE /* _AUTHEN */ ),


    // Report Reference (Index 8)
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ,
                       read_report, NULL, &input),

    // Control Point (Index 9, 10)
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, write_ctrl_point, &ctrl_point),
);


static bt_addr_le_t g_rc_last_paired_addr;
#define RC_LAST_PAIRED_ADDR (&g_rc_last_paired_addr)

#define RC_LAST_PAIRED_ADDR_SET_NONE    bt_addr_le_copy(RC_LAST_PAIRED_ADDR, BT_ADDR_LE_NONE)
#define RC_LAST_PAIRED_ADDR_SET(p_addr) bt_addr_le_copy(RC_LAST_PAIRED_ADDR, (p_addr))
#define RC_LAST_PAIRED_ADDR_IS_NONE     bt_addr_le_eq(RC_LAST_PAIRED_ADDR, BT_ADDR_LE_NONE)

static void bond_find(const struct bt_bond_info *info, void *user_data)
{
    // printk("bond_find type:%d mac:["MAC_FMT"]\n", dst->type, dst->a.val);

    char _t[64];
    bt_addr_le_to_str(&(info->addr), _t, sizeof(_t));
    printk("bond_find [%s]\n", _t);

    RC_LAST_PAIRED_ADDR_SET(&(info->addr));

    return;
}

static void advertising_continue(void)
{
    struct bt_le_adv_param adv_param;

    int err;

    if (is_adv_running) {
        return;
    }

    if (g_wakeup_adv_mode == false) {
        if (RC_LAST_PAIRED_ADDR_IS_NONE) {
            adv_param = *BT_LE_ADV_CONN_FAST_1;
            adv_param.options |= BT_LE_ADV_OPT_USE_IDENTITY;
            if (g_id >= 0) {
                printk("Use mac form g_id!\n");
                adv_param.id = g_id;
            }

            printk("Wait Pair advertising start ...\n");
            err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
        } else {
            adv_param = *BT_LE_ADV_CONN_DIR(RC_LAST_PAIRED_ADDR);
            adv_param.options |= BT_LE_ADV_OPT_USE_IDENTITY;
            if (g_id >= 0) {
                printk("Use mac form g_id!\n");
                adv_param.id = g_id;
            }

            char addr_buf[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(RC_LAST_PAIRED_ADDR, addr_buf, BT_ADDR_LE_STR_LEN);
            printk("Direct advertising to %s started\n", addr_buf);

            err = bt_le_adv_start(&adv_param, NULL, 0, NULL, 0);
        }

    } else {
        adv_param = *BT_LE_ADV_NCONN;
        adv_param.options |= BT_LE_ADV_OPT_USE_IDENTITY;
        // adv_param = *BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY, BT_GAP_ADV_FAST_INT_MIN_2, \
        // 		BT_GAP_ADV_FAST_INT_MAX_2, NULL)
        if (g_id >= 0) {
            printk("Use mac form g_id!\n");
            adv_param.id = g_id;
        }
        printk("Wakeup advertising start ...\n");
        err = bt_le_adv_start(&adv_param, ad_wakeup, ARRAY_SIZE(ad_wakeup), sd, ARRAY_SIZE(sd));
    }

    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Regular advertising started [%s]\n", g_wakeup_adv_mode ? "Wakeup adv" : "Pair adv");

    is_adv_running = true;
}

static void advertising_start(void)
{
    RC_LAST_PAIRED_ADDR_SET_NONE;
    if (g_id < 0) {
        bt_foreach_bond(BT_ID_DEFAULT, bond_find, NULL);
    } else {
        bt_foreach_bond(g_id, bond_find, NULL);
    }

    if (is_adv_running) {
        printk("Advertising is alreay started, skip.\n");
        return;
    }

    k_work_submit(&adv_work);
}

int advertising_stop(void)
{
    int err = -1;
    if (!is_adv_running) {
        return 0;
    }

    err = bt_le_adv_stop();
    if (err) {
        printk("Advertising failed to stop (err %d)\n", err);
        return -1;
    }

    is_adv_running = false;

    return 0;
}

static void advertising_process(struct k_work *work)
{
    advertising_continue();
}

struct bt_le_conn_param param = {
    .interval_min = 12,  // 15ms (12 * 1.25)
    .interval_max = 24,  // 30ms (24 * 1.25)
    .latency = 29,       // 允许忽略29个心跳包
    .timeout = 300,      // 超时 3000ms (300 * 10ms)
};

static void update_fe_data(bt_addr_le_t *dst_addr)
{
    if (!update_fe_data) {
        return;
    }
    fe_data[0] = dst_addr->a.val[0];
    fe_data[1] = dst_addr->a.val[1];
    fe_data[2] = dst_addr->a.val[2];
}

/* 蓝牙连接回调 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    is_adv_running = false;

    bt_addr_le_t *dst_addr = bt_conn_get_dst(conn);
    if (dst_addr) {
        update_fe_data(dst_addr);
        bt_addr_le_to_str(dst_addr, addr, sizeof(addr));
    } else {
        snprintf(addr, BT_ADDR_LE_STR_LEN, "%s", "unkown");
    }


    if (err) {
        if (err == BT_HCI_ERR_ADV_TIMEOUT) {
            printk("Direct advertising to %s timed out\n", addr);
            k_work_submit(&adv_work);
        } else {
            printk("Failed to connect to %s 0x%02x %s\n", addr, err,
                   bt_hci_err_to_str(err));
        }
        return;
    }

    printk("Connected %s\n", addr);

    if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
        printk("Failed to set security.\n");
    } else {
        printk("Set security Successed.\n");
    }

    #if 1
    /* 防止 GATT 超时 断开
     * https://aistudio.google.com/prompts/1GJsbMHOL_QnUtfOqwx9DFhbDy58Q81Nv
     */
    if (bt_conn_le_param_update(conn, &param)) {
        printk("Conn param update failed (err %d).\n", err);
    } else {
        printk("Conn param update requested!\n");
    }
    #endif

    g_btrc_st = BTRC_ST_CONNECTED;

    return;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    printk("Disconnected from %s, reason 0x%02x %s\n", addr,
           reason, bt_hci_err_to_str(reason));

    advertising_start();

    g_btrc_st = BTRC_ST_DISCONNECTE;
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	if (err == 0) {
		LOG_INF("Set security Successed! level: %s(%u)", bt_security_err_to_str(err), err);
	} else {
		LOG_ERR("Failed to set security level: %s(%u)", bt_security_err_to_str(err), err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed_cb,
};

static struct bt_gatt_attr *report_decl = NULL;
static inline struct bt_gatt_attr * get_attrs(void)
{
    if (!report_decl) {
        report_decl = bt_gatt_find_by_uuid(hid_svc.attrs, hid_svc.attr_count, BT_UUID_HIDS_REPORT);
        if (!report_decl) {
            printk("Error: HID Report Characteristic not found\n");
            return NULL;
        }
    }

    return report_decl + 1;
}

static int do_send_key(uint8_t key)
{
    struct bt_gatt_attr * rpt_val_att = get_attrs();
    if (!report_decl) {
        printk("Error: HID Report Characteristic not found\n");
        return -1;
    }

    // 1. 按下按键
    memset(report_val, 0, 8);
    report_val[2] = key;
    //bt_gatt_notify(NULL, rpt_val_att, report_val, 8);
    bt_gatt_notify(NULL, &hid_svc.attrs[6], report_val, sizeof(report_val));

    k_msleep(50);

    // 2. 松开按键
    memset(report_val, 0, 8);
    //bt_gatt_notify(NULL, rpt_val_att, report_val, 8);
    bt_gatt_notify(NULL, &hid_svc.attrs[6], report_val, sizeof(report_val));

    return 0;
}

static int cmd_send_key(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) return -EINVAL;

    uint8_t key = (uint8_t)strtol(argv[1], NULL, 16);

    do_send_key(key);

    shell_print(sh, "Sent keycode 0x%02x", key);

    return 0;
}

SHELL_CMD_REGISTER(send, NULL, "Send keycode: send <hex>\n\r"
                                "- Left:   0x50, Right:  0x4f\n\r"
                                "- UP:     0x52, Down:   0x51\n\r"
                                "- OK:     0x28\n\r"
                                "- Return: 0xf1\n\r"
                                "- Power:  0x66\n\r"
                    , cmd_send_key);

#if defined(CONFIG_NET_TC_THREAD_PREEMPTIVE)
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#else
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#endif
#define BT_CK_STACK_SIZE 2048
K_SEM_DEFINE(bt_ck_run_sem, 0, 1); // 定义一个初始值为0的信号量

static volatile bool g_bt_ck_running = false;

static void bt_ck_k_sleep(int time_s)
{
    do {
        k_sleep(K_SECONDS(1));
        printk("bt_ck_k_sleep [%d]\n", time_s);
    } while(time_s-- && g_bt_ck_running);
}

static void _bt_ck_looper(void)
{
    g_bt_ck_running = true;

    while(g_bt_ck_running) {
        printk("--> Power\n");
        do_send_key(0x66);
        bt_ck_k_sleep(1); if (!g_bt_ck_running) break;
        printk("--> Power\n");
        do_send_key(0x66);

        bt_ck_k_sleep(120); if (!g_bt_ck_running) break;
        
        printk("--> wakeup_adv_mode\n");
        g_wakeup_adv_mode = true;
        try_advertising_start(true, 20);

        bt_ck_k_sleep(120); if (!g_bt_ck_running) break;

    }

    return;
}

static void bt_ck_looper_thrd(void)
{
    LOG_INF("bt_ck_looper_thrd start ...");
    
    while(1) {
         k_sem_take(&bt_ck_run_sem, K_FOREVER);

         LOG_INF("bt_ck_looper start ++++++");
        _bt_ck_looper();
         LOG_INF("bt_ck_looper Stop ++++++");

    }

    LOG_INF("bt_ck_looper_thrd exit ...");

    return;
}

K_THREAD_DEFINE(bt_ck_thread_id, BT_CK_STACK_SIZE,
		bt_ck_looper_thrd, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

static int cmd_auto_reboot_start(const struct shell *sh, size_t argc, char **argv)
{
    // k_thread_start(bt_ck_thread_id);
    k_sem_give(&bt_ck_run_sem);

    return 0;
}

static int cmd_auto_reboot_stop(const struct shell *sh, size_t argc, char **argv)
{
    g_bt_ck_running = false;

    return 0;
}

SHELL_CMD_REGISTER(auto_reboot_start, NULL, "auto_reboot_start\n\r" , cmd_auto_reboot_start);
SHELL_CMD_REGISTER(auto_reboot_stop, NULL, "auto_reboot_stop\n\r" , cmd_auto_reboot_stop);


struct k_work_delayable adv_mode_switch_work;

int try_advertising_start(bool isWakeup, int time_s)
{
    g_wakeup_adv_mode = isWakeup;

    advertising_stop();

    k_sleep(K_MSEC(100));

    advertising_start();

    if (time_s) {
        printk("advertising will continue %d s\n", time_s);
        k_work_reschedule(&adv_mode_switch_work, K_MSEC(time_s * 1000));
    }

    return 0;
}

static int cmd_start_adv(const struct shell *sh, size_t argc, char **argv)
{
    int continue_time_s = 5;

    if (argc >= 2) {
        continue_time_s = atoi(argv[1]);
    }

    try_advertising_start(false, continue_time_s);

    return 0;
}

SHELL_CMD_REGISTER(start_adv, NULL, "Start ble adv", cmd_start_adv);

static int cmd_wakeup(const struct shell *sh, size_t argc, char **argv)
{
    int continue_time_s = 5;

    if (argc >= 2) {
        continue_time_s = atoi(argv[1]);
    }

    g_wakeup_adv_mode = true;

    try_advertising_start(true, continue_time_s);

    return 0;
}

SHELL_CMD_REGISTER(start_wakeup, NULL, "Send wakeup adv", cmd_wakeup);

void adv_mode_switch_handler(struct k_work *work)
{
    int err = -1;

    printk("adv mode switch handle.\n");

    advertising_stop();

    k_sleep(K_MSEC(100));

    g_wakeup_adv_mode = false;

    if (g_btrc_st == BTRC_ST_DISCONNECTE) {
        advertising_start();
    }
}

static void __swap_addr(uint8_t *a)
{
	uint8_t t0 = 0, t1 = 0, t2 = 0;

	t0 = a[0];
	t1 = a[1];
	t2 = a[2];

	a[0] = a[5];
	a[1] = a[4];
	a[2] = a[3];

	a[3] = t2;
	a[4] = t1;
	a[5] = t0;

	return;
}

static void set_public_addr(void)
{
    // bt_addr_le_t addr = {BT_ADDR_LE_RANDOM, {{0xF4, 0x72, 0x57, 0x54, 0xCC, 0x6E}}};
    bt_addr_le_t addr = {BT_ADDR_LE_RANDOM, {{0xF4, 0x72, 0x57, 0x66, 0x66, 0x66}}};

	__swap_addr(addr.a.val);

    g_id = bt_id_create(&addr, NULL);
	if (g_id < 0) {
		printk("ID create Failed, (err %d)\n", g_id);
	} else {
		printk("ID create SUCCESS, ID: %d\n", g_id);
	}
}

static void fix_uuid_data(void)
{
	__swap_addr(&(uuid_data[9]));

	return;
}

int ble_rc_init(void)
{
    int err;

    printk("BLE RC INIT start ...\n");

    RC_LAST_PAIRED_ADDR_SET_NONE;

    fix_uuid_data();
    set_public_addr();

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("bt enable success.\n");

    k_work_init(&adv_work, advertising_process);
    k_work_init_delayable(&adv_mode_switch_work, adv_mode_switch_handler);

    // advertising_start();

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    k_thread_start(bt_ck_thread_id);

    return 0;
}

