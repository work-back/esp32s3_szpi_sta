#include <zephyr/kernel.h>
#include <zephyr/data/json.h>
#include <stdio.h>

/* 定义 paras 数组的最大可能长度 */
#define MAX_PARAS_COUNT 10 

/* 1. 定义内部的键值对结构体 (对应 paras 里面的对象) */
struct para_item {
    const char *k;
    const char *v;
};

/* 描述 para_item 的 JSON 映射关系 */
static const struct json_obj_descr para_item_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct para_item, k, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct para_item, v, JSON_TOK_STRING),
};

/* 2. 定义外层的数据结构 */
struct msg_payload {
    const char *func;
    struct para_item paras[MAX_PARAS_COUNT]; /* 静态分配最大数组 */
    size_t num_paras;                        /* 关键：记录当前数组实际包含多少个元素 */
};

/* 描述 msg_payload 的 JSON 映射关系 */
static const struct json_obj_descr msg_payload_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct msg_payload, func, JSON_TOK_STRING),
    /* 使用 JSON_OBJ_DESCR_OBJ_ARRAY 映射对象数组 */
    /* 参数依次为：结构体名, 数组字段名, 最大长度, 实际长度变量名, 内部描述符, 内部描述符长度 */
    JSON_OBJ_DESCR_OBJ_ARRAY(struct msg_payload, paras, MAX_PARAS_COUNT, num_paras,
                             para_item_descr, ARRAY_SIZE(para_item_descr)),
};

static int payload_paras_add(struct msg_payload *payload_p, const char *k, const char *v)
{
    if (payload_p->num_paras >= MAX_PARAS_COUNT) {
        printk("Too many Paras!\n");
        return -1;
    }

    payload_p->paras[payload_p->num_paras].k = k;
    payload_p->paras[payload_p->num_paras].v = v;
    payload_p->num_paras++;

    return 0;
}

int build_action_hello(char *buf, int buf_len)
{
    struct msg_payload payload = {0};

    if (!buf || !buf_len) {
        return -1;
    }
    
    payload.func = "hello";
    payload.num_paras = 0; /* 初始数组为空 */

    // payload_paras_add(&payload, "action", "hello");
    payload_paras_add(&payload, "mac", "11:22:33:44:55:66");
    
    ssize_t ret = json_obj_encode_buf(msg_payload_descr, 
                                      ARRAY_SIZE(msg_payload_descr),
                                      &payload, 
                                      buf, 
                                      buf_len);

    if (ret < 0) {
        printk("JSON encode failed, ret: %zd\n", ret);
        return -1;
    }

    ret = strlen(buf);

    printk("JSON encoded:[%d]:\n%s\n", ret, buf);

    return ret;
}
