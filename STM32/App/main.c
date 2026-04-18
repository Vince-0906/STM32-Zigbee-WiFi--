/*
 * ZNJJ 网关固件入口。
 * 业务逻辑分层见 App/ 目录；本文件只负责启动。
 */

#include "gw_main.h"

int main(void)
{
    gw_main_init();
    gw_main_run();
    return 0;
}
