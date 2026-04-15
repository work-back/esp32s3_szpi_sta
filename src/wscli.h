#ifndef __WS_CLI_H__
#define __WS_CLI_H__

int sta_tryconnect(void);

#define MAX_RECV_BUF_LEN (1024)

int wscli_init(void);
int wscli_fini(void);
int wscli_getsock(void);
int wscli_recv(int sock, uint8_t *buf, size_t buf_len);
ssize_t wscli_send(int sock, const void *buf, size_t len);

int ble_rc_init(void);

enum {
    EVT_BASE = 0x1000,
    EVT_TIMER_OUT,
    EVT_WIFI_STA_START,
    EVT_WIFI_CONNECTED = 0x2000,
};

int evt_send(unsigned int type, unsigned int len, void *data);

#define MAC_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC_DATA(m) m[0], m[1], m[2], m[3], m[4], m[5]


#endif //__WS_CLI_H__
