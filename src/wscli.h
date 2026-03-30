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

#endif //__WS_CLI_H__
