#include "rpc_route.h"

struct rpc_request {
    const char *func;
    struct json_obj_token paras; 
};

static const struct json_obj_descr rpc_request_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct rpc_request, func, JSON_TOK_STRING),
    // 关键点：JSON_TOK_OPAQUE 把 paras 里面的 {} 内容原封不动保留
    JSON_OBJ_DESCR_PRIM(struct rpc_request, paras, JSON_TOK_OPAQUE),
};


// 针对 "start_ble_adv" 的参数
struct ble_adv_paras {
    int continue_time;
};

static const struct json_obj_descr ble_adv_paras_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct ble_adv_paras, continue_time, JSON_TOK_NUMBER),
};

void func_start_ble_adv(struct json_obj_token *paras_token)
{
    struct ble_adv_paras p = {0};
    
    // 第二次解析：只解析 paras 里面的内容
    int ret = json_obj_parse(paras_token->start, paras_token->length,
                             ble_adv_paras_descr, ARRAY_SIZE(ble_adv_paras_descr),
                             &p);
    if (ret < 0) {
        printk("BLE parse failed!\n");
        return;
    }

    printk(">>> start_ble_adv, continue_time %d ms\n", p.continue_time);
}

static const struct rpc_route routes[] = {
    {"start_ble_adv", func_start_ble_adv},
    // {"set_wifi",      handle_set_wifi},
};

void rpc_execute(char *json_str)
{
    struct rpc_request req = {0};

    int ret = json_obj_parse(json_str, strlen(json_str),
                             rpc_request_descr, ARRAY_SIZE(rpc_request_descr),
                             &req);
    
    if (ret < 0 || req.func == NULL) {
        printk("RPC req parse failed. ret:%d\n", ret);
        return;
    }

    printk("RPC func: [%s]\n", req.func);

    for(int i=0; i < ARRAY_SIZE(routes); i++) {
        if (strcmp(req.func, routes[i].func_name) == 0) {
            routes[i].handler(&req.paras);
            break;
        }
    }

    return;
}