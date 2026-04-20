#ifndef __CONFIG_H__
#define __CONFIG_H__

/*
 * 全局常量集中地（规范书 gateway_design_spec.md §4.5/§7）。
 * 任何数值调整必须同步更新规范书与 CC2530 侧 gw_types.h。
 */

#include <stdint.h>
#include <stddef.h>

/* ---------- UART 帧协议 §4.5 ---------- */
#define FRAME_SOF1          0xAAu
#define FRAME_SOF2          0x55u
#define FRAME_EOF           0x0Du
#define FRAME_PAYLOAD_MAX   64u                      /* LEN <= 65 → payload <= 64 */
#define FRAME_LEN_MIN       1u
#define FRAME_LEN_MAX       65u
#define FRAME_MAX           (2 + 1 + FRAME_LEN_MAX + 1 + 1) /* 69 */
#define FRAME_IDLE_MS       20u                      /* >20ms 复位解析 */

/* ---------- CMD 表 §4.5.2 ---------- */
#define CMD_ZB_PING         0x01u
#define CMD_ZB_NET_STATUS   0x02u
#define CMD_ZB_ALLOW_JOIN   0x10u
#define CMD_ZB_LIST_NODES   0x11u
#define CMD_ZB_NODE_INFO    0x12u
#define CMD_ZB_REPORT       0x20u
#define CMD_ZB_CMD          0x21u
#define CMD_ZB_ERR          0x7Fu
#define ZB_DEV_UNKNOWN      0x00u
#define ZB_DEV_ROUTER       0x01u
#define ZB_DEV_ENDDEV       0x02u
#define ZB_ROLE_UNKNOWN     0x00u
#define ZB_ROLE_TEMP_HUM    0x01u
#define ZB_ROLE_LUX         0x02u

/* ---------- dtype §4.5.3 ---------- */
#define DTYPE_BOOL          0x01u
#define DTYPE_U8            0x02u
#define DTYPE_I8            0x03u
#define DTYPE_I16           0x10u
#define DTYPE_U16           0x11u
#define DTYPE_I32           0x12u
#define DTYPE_U32           0x20u
#define DTYPE_STRING        0x30u
#define DTYPE_BYTES         0x40u

/* ---------- ZB_ERR.code §4.5.4 ---------- */
#define ERRC_LINK_DOWN      0x01u
#define ERRC_PANID_CONFLICT 0x02u
#define ERRC_NV_FAIL        0x03u
#define ERRC_SEND_FAIL      0x10u
#define ERRC_NODE_OFFLINE   0x11u
#define ERRC_QUEUE_FULL     0x12u
#define ERRC_BAD_FRAME      0x20u
#define ERRC_CRC_FAIL       0x21u
#define ERRC_UNKNOWN        0x7Fu

/* ---------- ZCL Cluster ids §4.4 ---------- */
#define ZCL_CLU_TEMP        0x0402u
#define ZCL_CLU_HUM         0x0405u
#define ZCL_CLU_LUX         0x0400u
#define ZCL_CLU_ONOFF       0x0006u

/* ---------- Zigbee 网络 §7.3 ---------- */
#define ZB_CHANNEL_DEFAULT  15u
#define ZB_NODE_MAX         8u
#define ZB_NODE_OFFLINE_SEC 180u

/* ---------- 定时参数 §7.2 ---------- */
#define PC_PING_MS          5000u
#define TCP_DEAD_MS         30000u
#define ZB_HEARTBEAT_MS     10000u       /* STM32→CC ZB_PING 周期 */
#define ZB_HEARTBEAT_FAILS  3u
#define ZB_ALLOW_JOIN_SEC   60u
#define ZB_BRINGUP_RETRY_MS 5000u
#define ZB_BRINGUP_WINDOW_MS 70000u
#define NODE1_STALE_MS      4000u
#define NODE2_STALE_MS      4000u
#define OLED_REFRESH_MS     500u
#define IWDG_TIMEOUT_MS     2000u
#define JSON_LINE_MAX       512u

/* ---------- 联动阈值默认值 §7.4 ---------- */
#define TH_LUX_LOW_DEFAULT          500u
#define TH_TEMP_HIGH_X100_DEFAULT   3200   /* 32.0 °C */
#define TH_TEMP_LOW_X100_DEFAULT    500    /*  5.0 °C */
#define TH_HUM_HIGH_X100_DEFAULT    8500u  /* 85.0 % */
#define TH_HUM_LOW_X100_DEFAULT     2000u  /* 20.0 % */
#define TH_HYSTERESIS_LUX_DEFAULT   50u
#define TH_HYSTERESIS_TEMP_X100_DEF 50     /* 0.5 °C */
#define TH_DEBOUNCE_MS_DEFAULT      1000u
#define AUTO_OVERRIDE_MS            30000u /* 手动优先窗 §8.4 */

/* ---------- 编译期 WiFi 凭据 / 服务器端点 ----------
 * dev-only：以下默认值是实验室测试网；正式环境请用编译 -D 覆盖，
 * 不要将此默认值推送到公共仓库。
 */
#ifndef WIFI_SSID
#define WIFI_SSID "CMCC-701"
#endif
#ifndef WIFI_PWD
#define WIFI_PWD  "88888888"
#endif
#ifndef SERVER_IP
#define SERVER_IP "192.168.1.85"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT 23333u
#endif

#endif /* __CONFIG_H__ */
