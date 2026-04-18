/*
 * UART 链路层：把 HalUART 的 RX 回调字节喂到 frame_parser。
 *
 * 约束：
 *  - 回调内只做入队 + 置事件位，严禁任何 RF / 分配 / 阻塞（CC2530.md §0.6）。
 *  - 帧解析在任务上下文执行（由 GwCoord_ProcessEvent 收到 GW_EVT_UART_RX 触发）。
 */

#include "uart_link.h"
#include "gw_coord.h"
#include "frame.h"
#include "hal_uart.h"
#include "OSAL.h"
#include "OSAL_Timers.h"

#ifndef GWCOORD_UART_PORT
#define GWCOORD_UART_PORT   HAL_UART_PORT_0
#endif
#ifndef GWCOORD_UART_BAUD
#define GWCOORD_UART_BAUD   HAL_UART_BR_115200
#endif

/* 环形缓冲：容纳 ≥ 2 帧 + 少量抖动 */
#define UL_RX_RING_SZ       256u
static uint8 XDATA_QUAL   s_rx_ring[UL_RX_RING_SZ];
static volatile uint16    s_rx_head;
static volatile uint16    s_rx_tail;

static frame_parser_t XDATA_QUAL s_parser;
static uint8          s_owner_task = 0xFF;

/* HalUART 回调：在中断/驱动层调用 */
static void uart_rx_cb(uint8 port, uint8 event)
{
    uint8 b;
    uint16 next;
    uint16 polled;

    (void)event;
    if (port != GWCOORD_UART_PORT) return;

    /* 读出驱动中所有就绪字节到环形 */
    polled = Hal_UART_RxBufLen(port);
    while (polled--) {
        if (HalUARTRead(port, &b, 1) == 0) break;
        next = (uint16)((s_rx_head + 1) & (UL_RX_RING_SZ - 1));
        if (next == s_rx_tail) {
            /* 溢出：丢字节并计数 */
            s_parser.frame_err++;
            break;
        }
        s_rx_ring[s_rx_head] = b;
        s_rx_head = next;
    }
    if (s_owner_task != 0xFF) {
        osal_set_event(s_owner_task, GW_EVT_UART_RX);
    }
}

void uart_link_init(uint8 owner_task_id)
{
    halUARTCfg_t cfg;

    s_rx_head = 0;
    s_rx_tail = 0;
    frame_parser_init(&s_parser);
    s_owner_task = owner_task_id;

    osal_memset(&cfg, 0, sizeof(cfg));
    cfg.configured           = TRUE;
    cfg.baudRate             = GWCOORD_UART_BAUD;
    cfg.flowControl          = FALSE;
    cfg.flowControlThreshold = 0;
    cfg.rx.maxBufSize        = 128;
    cfg.tx.maxBufSize        = 128;
    cfg.idleTimeout          = 6;
    cfg.intEnable            = TRUE;
    cfg.callBackFunc         = uart_rx_cb;
    (void)HalUARTOpen(GWCOORD_UART_PORT, &cfg);
}

void uart_link_drain(void)
{
    frame_t out;
    uint8   b;
    uint16  head;
    int8    rc;
    uint32  now;

    for (;;) {
        head = s_rx_head;
        if (s_rx_tail == head) return;
        b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16)((s_rx_tail + 1) & (UL_RX_RING_SZ - 1));

        now = osal_GetSystemClock();
        rc = frame_parser_feed(&s_parser, b, now, &out);
        if (rc == 1) {
            GwCoord_OnFrame(&out);
        } else if (rc < 0) {
            /* 错误已在 parser 统计；可选择回 ZB_ERR */
            if (rc == RC_ERR_BAD_CRC) {
                GwCoord_SendErr(ERRC_CRC_FAIL, 0);
            } else if (rc == RC_ERR_BAD_FRAME) {
                GwCoord_SendErr(ERRC_BAD_FRAME, 0);
            }
        }
    }
}

int8 uart_link_tx(const uint8 *data, uint16 n)
{
    uint16 written;
    if (data == 0 || n == 0) return (int8)RC_ERR_PARAM;
    written = HalUARTWrite(GWCOORD_UART_PORT, (uint8 *)data, n);
    return (written == n) ? 0 : (int8)RC_ERR_NO_SPACE;
}

uint32 uart_link_frame_err(void) { return s_parser.frame_err; }
uint32 uart_link_crc_err(void)   { return s_parser.crc_err; }
uint32 uart_link_ok_cnt(void)    { return s_parser.ok_cnt; }
