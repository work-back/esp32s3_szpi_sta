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


void func_start_ble_adv(struct parameter *paras, size_t num_paras)
{
    for (size_t i = 0; i < num_paras; i++) {
        printk("  [%d] k: %s, v: %s\n", i, paras[i].k, paras[i].v);
    }
}


struct rpc_route {
    const char *func_name;
    void (*handler)(struct parameter *paras, size_t num_paras);
};

static const struct rpc_route routes[] = {
    {"ble_adv", func_start_ble_adv},
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