#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wscli.h"
#include "rpc_route.h"


/*
{
    "func":"ble_adv",
    "paras": [
        {"k":"duration", "v":"100"},
        {"k":"op", "v":"start"}
    ]
}
*/

#define MAX_PARAS 5 

struct parameter {
    const char *k;
    const char *v;
};

struct rpc_func {
    const char *func;
    struct parameter paras[MAX_PARAS]; // 静态数组
    size_t num_paras;                  // 用于保存实际解析出的数组元素个数 
};

static const struct json_obj_descr param_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct parameter, k, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct parameter, v, JSON_TOK_STRING),
};


static const struct json_obj_descr rpc_req_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct rpc_func, func, JSON_TOK_STRING),
    /* 解析对象数组：指定最大容量、实际数量变量、以及内层描述符 */
    JSON_OBJ_DESCR_OBJ_ARRAY(struct rpc_func, paras, MAX_PARAS, num_paras, 
                             param_descr, ARRAY_SIZE(param_descr)),
};


static /* inline */ const char* get_v_by_k(struct parameter *paras, size_t num_paras, const char *key) {
    for (size_t i = 0; i < num_paras; i++) {
        if (strcmp(paras[i].k, key) == 0) {
            return paras[i].v;
        }
    }
    return NULL;
}

#define FUNC_ARGS_LIST struct parameter *paras, size_t num

#define DEBUG_PRINT_PARAMS do { \
    for (size_t _i = 0; _i < (num); _i++) { \
        printk("  [%zu] k: %s, v: %s\n", _i, (paras)[_i].k, (paras)[_i].v); \
    } \
} while(0)

#define GET_PARAM_REQUIRED(var_name, key) \
    const char *var_name = get_v_by_k(paras, num, key); \
    if (!var_name) { \
        printk("Error: Missing required parameter '%s'\n", key); \
        return; \
    }

#define GET_PARAM_OPTIONAL(var_name, key, default_val) \
    const char *var_name = get_v_by_k(paras, num, key); \
    if (!var_name) { \
        var_name = default_val; \
    }

int get_k_idx(struct parameter *paras, size_t num_paras, const char *key)
{
    for (size_t i = 0; i < num_paras; i++) {
        if (0 == strncmp(paras[i].k, key, strlen(key))) {
            return i;
        }
    }

    printk("get_k_idx: Cant not get [%s]!\n", key);
    return -1;
}

void func_ble_adv(FUNC_ARGS_LIST)
{
    /*
        {"k":"duration", "v":"100"},
        {"k":"op", "v":"start"},
        {"k":"mode", "v":"normal"} // normal / wakeup
    */

    DEBUG_PRINT_PARAMS;

    GET_PARAM_REQUIRED(op_str, "op");

    if (0 == strncmp(op_str, "start", 5)) {
        GET_PARAM_REQUIRED(duration_str, "duration");
        GET_PARAM_REQUIRED(mode_str, "mode");
        printk("start ble adv duration:%s, op:%s, mode:%s\n", duration_str, op_str, mode_str);
        bool isWakeup = false;
        if (0 == strncmp(mode_str, "wakeup", 6)) {
            isWakeup = true;
        }
        try_advertising_start(isWakeup, atoi(duration_str));
    } else if (0 == strncmp(op_str, "stop", 5)) {
        printk("stop ble adv\n");
        advertising_stop();
    }
}


struct rpc_route {
    const char *func_name;
    void (*handler)(struct parameter *paras, size_t num_paras);
};

static const struct rpc_route routes[] = {
    {"ble_adv", func_ble_adv},
    // {"set_wifi",      handle_set_wifi},
};

/* 
    * 【重要警告】: 
    * Zephyr 的 json_obj_parse() 在解析字符串时会修改原字符串（插入 '\0'），
    * 因此 JSON 数据必须存放在可读写的 RAM 中 (char [])，绝对不能是 const char * 
    */
void rpc_execute(char *json_str, size_t json_str_len)
{
    struct rpc_func req = {0}; // 初始化清零

    int ret = json_obj_parse(json_str, json_str_len, 
                             rpc_req_descr, ARRAY_SIZE(rpc_req_descr), 
                             &req);
    
    if (ret < 0 || req.func == NULL) {
        printk("RPC req parse failed. ret:%d\n", ret);
        return;
    }

    printk("RPC func: [%s]\n", req.func);

    for(int i=0; i < ARRAY_SIZE(routes); i++) {
        if (strcmp(req.func, routes[i].func_name) == 0) {
            routes[i].handler(req.paras, req.num_paras);
            break;
        }
    }

    return;
}