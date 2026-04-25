#ifndef __RPC_ROUTE__
#define __RPC_ROUTE__

#include <zephyr/data/json.h>

struct rpc_route {
    const char *func_name;
    void (*handler)(struct json_obj_token *paras);
};

#endif // __RPC_ROUTE__