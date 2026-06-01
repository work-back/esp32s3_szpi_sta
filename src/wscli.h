#ifndef __WS_CLI_H__
#define __WS_CLI_H__

#if CONFIG_WIFI_STA_EN
#define STA_NETWORK_EN 1
#endif

int sta_tryconnect(void);

#define MAX_RECV_BUF_LEN (1024)

int wscli_init(void);
int wscli_fini(void);
int wscli_getsock(void);
int wscli_recv(int sock, uint8_t *buf, size_t buf_len);
ssize_t wscli_send(int sock, const void *buf, size_t len);
void wscli_ping(int ws_sock);

int ble_rc_init(void);
int try_advertising_start(bool isWakeup, int time_s);
int advertising_stop(void);

enum {
    EVT_BASE = 0x1000,
    EVT_TIMER_OUT,
    EVT_WIFI_STA_START,
    EVT_WIFI_CONNECTED = 0x2000,
    EVT_BLE_START,
};

int evt_send(unsigned int type, unsigned int len, void *data);

#define MAC_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC_DATA(m) m[0], m[1], m[2], m[3], m[4], m[5]

void rpc_execute(char *json_str, size_t json_str_len);

int build_action_hello(char *buf, int buf_len);
int build_status_ble_ready(char *buf, int buf_len);

int run_queries(void);
int run_get_SK_v(void);
int run_set_SK_v(uint8_t v);

int lfs_run(void);
int fatfs_init(void);
const char * fatfs_get_root_path(void);

void fatfs_fini(void);

int calculate_file_sha256(const char *filepath, unsigned char output_sha256[32]);
void print_sha256_sum(const unsigned char sha256[32], const char *filename);

void perform_ota_upgrade_from_file(const char *bin_file_path);

#endif //__WS_CLI_H__
