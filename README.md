# ZNJJ

基于 `STM32F103RC`、`CC2530`、`ZigBee` 和 `WiFi` 的环境监测与联动网关项目。

当前仓库包含三部分可运行固件：

- `STM32`：网关主控，负责 WiFi 联网、JSON 协议处理、ZigBee 数据转发、本地阈值联动和 OLED 显示
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/GwCoord`：CC2530 ZigBee 协调器，负责组网、节点表维护和 UART 帧协议
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/ZbNode`：CC2530 终端节点，区分温湿度节点和光照节点

项目当前实现的是一个“小型 ZigBee 采集网络 + WiFi 上位机接入 + 本地自动联动”的完整闭环。

## 1. 项目概览

### 1.1 系统角色

| 角色 | 硬件/工程 | 主要职责 |
| --- | --- | --- |
| 网关主控 | `STM32/App/ZNJJ.uvprojx` | 驱动 WiFi、驱动协调器串口链路、维护节点状态、转发 JSON、执行阈值联动、刷新 OLED |
| ZigBee 协调器 | `Samples/GwCoord/CC2530DB/GwCoord.ewp` | 建立 ZigBee 网络、跟踪节点在线状态、把 AF/ZCL 数据转换成 UART 帧发给 STM32 |
| 节点 1 | `Samples/ZbNode` 的 `Node1_EndDeviceEB` 目标 | 采集 DHT11 温湿度，接收 LED/蜂鸣器控制命令 |
| 节点 2 | `Samples/ZbNode` 的 `Node2_EndDeviceEB` 目标 | 采集光敏 ADC，接收 LED/蜂鸣器控制命令 |
| PC 上位机 | `pc_host/` 内置 Python 平台，或用户自备 TCP 服务端 | 接收网关上报的 JSON 行数据，并下发控制/配置命令 |

### 1.2 当前功能

- 温湿度节点周期上报 `temp/hum`
- 光照节点周期上报 `lux`（值越大表示越亮）
- 网关将 ZigBee 数据转换为 JSON 行协议发给 PC
- PC 可远程控制节点 LED 和蜂鸣器
- 网关本地具备自动联动能力
- 光照过低时自动控制 Node2 的 `LED1`
- 温度/湿度越界时自动触发蜂鸣器告警
- 阈值参数支持 Flash 持久化
- 网关 OLED 显示链路状态、节点数据和告警文本
- 协调器和网关都带有链路保活/恢复逻辑

### 1.3 节点语义

当前代码里 `Node1` 和 `Node2` 不是按入网先后顺序绑定，而是按业务角色自动识别：

- 上报 `0x0402/0x0405` 温湿度簇的节点会被识别为 `Node1`
- 上报 `0x0400` 光照簇的节点会被识别为 `Node2`

这意味着即使两个节点重新入网、短地址变化，网关侧仍会按“温湿度节点”和“光照节点”两个角色去映射控制与显示。

## 2. 系统架构

```text
PC TCP Server
    ^
    | JSON lines over TCP
    v
Ai-WB2-01S WiFi module
    ^
    | USART2
    v
STM32F103RC Gateway
    ^
    | UART4 custom frame protocol
    v
CC2530 Coordinator
    ^
    | ZigBee
    +---- Node1: DHT11 + LED + Buzzer
    +---- Node2: LDR   + LED + Buzzer
```

### 2.1 当前链路实现

- `STM32` 到 `WiFi`：`USART2`，Ai-WB2-01S 透传模式
- `WiFi` 到 `PC`：TCP 长连接
- `CC2530` 协调器到 `STM32`：自定义 UART 二进制帧
- `CC2530` 节点到协调器：Z-Stack 2.5.1a + AF/ZCL

### 2.2 当前网络行为

从源码现状看，WiFi 模块当前工作在“主动连接 PC 服务端”的模式，而不是“在板端监听端口”的模式：

- WiFi 驱动位于 `STM32/Hardware/WIFI/wifi_link.c`
- 通过编译期宏 `WIFI_SSID`、`WIFI_PWD`、`SERVER_IP`、`SERVER_PORT` 配置接入点和目标服务端
- 连接建立后，网关会先发送一条 `hello` JSON 消息
- 协调器建网后会主动打开入网窗；当网络在线但还没有任何节点时，会周期性自动重开入网窗

## 3. 仓库结构

```text
.
├─ STM32/
│  ├─ App/                  # 网关应用层，入口、路由、联动、OLED 视图、阈值配置
│  ├─ Hardware/             # 外设驱动：WIFI / ZIGBEE / USART / OLED / GPIO / TIM / FLASH
│  ├─ SYSTEM/               # 时钟、日志等 BSP
│  ├─ FWlib/                # STM32F10x StdPeriph Library
│  ├─ Core/                 # 启动文件与 CMSIS
│  └─ OBJ/                  # 已生成的 STM32 构建产物
├─ ZStack-CC2530-2.5.1a/
│  └─ Projects/zstack/Samples/
│     ├─ GwCoord/           # 自定义协调器工程
│     └─ ZbNode/            # 自定义节点工程
├─ references/              # 板级映射、设计约束、协议说明、参考资料
├─ pc_host/                 # Python 上位机平台：TCP 服务端、浏览器仪表盘、mock 网关、测试
├─ docs/                    # 仓库内可跟踪的补充说明文档
└─ doc/                     # 需求/总体设计等历史文档模板与资料
```

### 3.1 建议优先阅读的源码

- `STM32/App/main.c`：网关入口
- `STM32/App/gw_main.c`：主循环、链路调度、OLED 更新、看门狗
- `STM32/App/router.c`：ZigBee 帧到 JSON 的转换、PC 命令下发
- `STM32/App/automation.c`：光照和温湿度联动逻辑
- `STM32/App/config.h`：协议常量、超时、默认阈值、WiFi/服务器参数
- `STM32/Hardware/WIFI/wifi_link.c`：WiFi 状态机
- `STM32/Hardware/ZIGBEE/zb_link.c`：协调器 UART 帧链路和心跳
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/GwCoord/Source/gw_coord.c`：协调器主任务
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/GwCoord/Source/cmd_map.c`：STM32 下行命令分发
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/GwCoord/Source/zb_net.c`：节点表、入网窗、在线状态
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/ZbNode/Source/zb_node.c`：节点主任务、周期上报、控制处理
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/ZbNode/Source/sensor_dht11.c`：DHT11 驱动
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/ZbNode/Source/sensor_ldr.c`：光敏 ADC 采样

## 4. 硬件与软件栈

### 4.1 STM32 侧

- MCU：`STM32F103RC`
- 工程：Keil MDK5 工程，目标名 `ZNJJ`
- 驱动库：`STM32F10x Standard Peripheral Library`
- 运行模型：裸机 `super loop`，无 RTOS
- 持久化：阈值参数保存在 Flash 末页，带 CRC32 校验

### 4.2 CC2530 侧

- MCU：`CC2530F256`
- ZigBee 协议栈：仓库内置 `Z-Stack-CC2530-2.5.1a`
- 工具链：IAR Embedded Workbench for 8051
- 协调器和节点都基于 Z-Stack 样例工程改造

### 4.3 传感与执行器

| 节点 | 传感器 | 执行器 | 上报周期 |
| --- | --- | --- | --- |
| Node1 | `DHT11` | LED + 蜂鸣器 | `2000 ms` |
| Node2 | 光敏 ADC (`AIN7/P0.7`) | LED + 蜂鸣器 | `500 ms` |

## 5. 构建与产物

### 5.1 STM32 网关

- 工程文件：`STM32/App/ZNJJ.uvprojx`
- 工具链：Keil MDK5 / ARMCC5
- 目标：`ZNJJ`
- 已生成产物：
  - `STM32/OBJ/ZNJJ.axf`
  - `STM32/OBJ/ZNJJ.hex`

### 5.2 CC2530 协调器

- 工程文件：`ZStack-CC2530-2.5.1a/Projects/zstack/Samples/GwCoord/CC2530DB/GwCoord.ewp`
- 工具链：IAR EW8051
- 常用目标：`CoordinatorEB`
- 已生成目录：`.../GwCoord/CC2530DB/CoordinatorEB/`
- 常见产物：`ZStack-CC2530-2.5.1a/Projects/zstack/Samples/GwCoord/CC2530DB/CoordinatorEB/Exe/GwCoord.d51`

### 5.3 CC2530 节点

- 工程文件：`ZStack-CC2530-2.5.1a/Projects/zstack/Samples/ZbNode/CC2530DB/ZbNode.ewp`
- 工具链：IAR EW8051
- 常用目标：
  - `Node1_EndDeviceEB`
  - `Node2_EndDeviceEB`
- `NODE_ROLE` 由工程目标区分：
  - `NODE_ROLE=1`：温湿度节点
  - `NODE_ROLE=2`：光照节点
- 常见产物：
  - `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/ZbNode/CC2530DB/Node1_EndDeviceEB/Exe/ZbNode.d51`
  - `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/ZbNode/CC2530DB/Node2_EndDeviceEB/Exe/ZbNode.d51`

### 5.4 推荐构建顺序

1. 编译并烧录 `GwCoord` 的 `CoordinatorEB`
2. 编译并烧录 `ZbNode` 的 `Node1_EndDeviceEB`
3. 编译并烧录 `ZbNode` 的 `Node2_EndDeviceEB`
4. 编译并烧录 `STM32/App/ZNJJ.uvprojx`
5. 启动 PC 侧 TCP 服务端，等待网关连接

### 5.5 PC 上位机平台

仓库已内置一个 Python 标准库实现的 PC 平台，位于 `pc_host/`：

- `run_host.py`：启动 TCP 服务端和浏览器仪表盘
- `run_mock_gateway.py`：无硬件时模拟网关协议链路

启动方式：

```powershell
python .\pc_host\run_host.py --tcp-port 23333 --http-port 8080
```

然后打开：`http://127.0.0.1:8080`

如果需要无硬件演示，可在另一个终端执行：

```powershell
python .\pc_host\run_mock_gateway.py --host 127.0.0.1 --port 23333
```

## 6. 运行流程

### 6.1 上电后流程

1. `STM32` 初始化时钟、串口、OLED、配置和看门狗
2. 网关拉起 WiFi 状态机，连接指定 AP 和 PC 服务端
3. 网关初始化 ZigBee 协调器串口链路，并通过 `PC4` 给 CC2530 复位脉冲
4. `GwCoord` 建网并维护入网窗
5. `Node1/Node2` 入网后按各自周期发送传感器上报
6. `STM32` 将节点数据转为 JSON 上报到 PC
7. 当命中阈值时，本地自动联动并同步发送 `alarm` 消息

### 6.2 本地联动规则

根据 `STM32/App/automation.c` 的当前实现（`lux` 数值越大表示越亮）：

- 当 `Node2.lux < lux_low` 时，网关自动控制 `Node2` 的 `EP1 LED1`
- 当 `Node1` 的温度或湿度超出阈值时，网关自动控制 `Node1` 的 `EP2 蜂鸣器`
- 手动远程控制后存在 `AUTO_OVERRIDE_MS` 手动优先窗口，避免自动联动立刻反向覆盖

## 7. 协议摘要

### 7.1 PC 与网关

协议形态：

- 传输：TCP
- 编码：UTF-8 文本
- 分帧：每条 JSON 以换行 `\n` 结尾

#### 上行消息示例

```json
{"t":"hello","fw":"znjj-0.1","gw_id":1}
{"t":"report","node":257,"kind":"temp_hum","temp":26.40,"hum":58,"ts":123456}
{"t":"report","node":258,"kind":"lux","lux":386,"ts":123789}
{"t":"status","node":257,"led":"off","buzzer":"on","ts":124000}
{"t":"alarm","type":"temp","level":"on","val":3560,"threshold":3200,"ts":124100}
{"t":"net","state":9,"channel":15,"panid":4660,"joined":2}
{"t":"node_info","node":257,"role":"temp_hum","dev":"enddev","rssi":-42,"last_seen_s":0,"online":1}
{"t":"ack","seq":42,"ok":true}
{"t":"pong","ts":125000}
```

#### 下行消息示例

```json
{"t":"cmd","seq":1,"node":1,"target":"buzzer","op":"on"}
{"t":"cmd","seq":2,"node":2,"target":"led","op":"toggle"}
{"t":"set_threshold","seq":3,"lux_low":500,"temp_high":32.0,"temp_low":5.0,"hum_high":85.0,"hum_low":20.0}
{"t":"allow_join","seq":4,"sec":60}
{"t":"list_nodes","seq":5}
{"t":"ping","seq":6}
```

说明：

- 上行消息中的 `node` 字段是节点当前 ZigBee 短地址
- 下行消息中的 `node` 可以传业务角色编号 `1/2`，也可以传实际短地址
- `target` 当前支持 `led` 和 `buzzer`
- `op` 当前支持 `on`、`off`、`toggle`
- 当前源码还会额外上报 `node_info`，用于同步节点角色、设备类型、RSSI 和在线状态

### 7.2 STM32 与协调器

`STM32` 与 `GwCoord` 之间使用自定义 UART 二进制帧：

```text
AA 55 LEN CMD PAYLOAD CRC8 0D
```

当前关键命令定义在：

- `STM32/App/config.h`
- `ZStack-CC2530-2.5.1a/Projects/zstack/Samples/GwCoord/Source/gw_types.h`

关键命令包括：

- `CMD_ZB_PING`
- `CMD_ZB_NET_STATUS`
- `CMD_ZB_ALLOW_JOIN`
- `CMD_ZB_LIST_NODES`
- `CMD_ZB_NODE_INFO`
- `CMD_ZB_REPORT`
- `CMD_ZB_CMD`
- `CMD_ZB_ERR`

## 8. 调试建议

### 8.1 网关侧

- `STM32` 调试串口为 `USART1`，适合查看日志
- 关键观察点：
  - WiFi 是否成功进入透传
  - 是否发送 `hello`
  - 是否收到 `CMD_ZB_NET_STATUS` / `CMD_ZB_REPORT`
  - OLED 上的 `ZB/WF/P` 状态是否正常

### 8.2 节点侧

`ZbNode` 当前源码里已经打开了节点串口调试输出：

- UART0：`P0.2/P0.3`
- 波特率：`115200`

可用于确认：

- 是否从 NV 恢复入网状态
- 是否重新加入网络
- 是否采样成功
- 第一帧上报是否收到 `AF_DATA_CONFIRM`

## 9. 配置项

当前最需要关注的编译期配置位于 `STM32/App/config.h`：

- `WIFI_SSID`
- `WIFI_PWD`
- `SERVER_IP`
- `SERVER_PORT`
- `TH_LUX_LOW_DEFAULT`
- `TH_TEMP_HIGH_X100_DEFAULT`
- `TH_TEMP_LOW_X100_DEFAULT`
- `TH_HUM_HIGH_X100_DEFAULT`
- `TH_HUM_LOW_X100_DEFAULT`

建议：

- 部署前替换默认测试网络参数
- 不要把生产环境 WiFi 凭据继续保留在源码默认宏里


## 10. 当前实现特点

- 代码整体按“协议层、驱动层、业务层”分层，便于继续扩展
- 网关侧使用静态缓冲和轮询主循环，适合资源受限场景
- 协调器侧已实现节点表、离线标记、自动开入网窗和 UART 帧错误回传
- 节点侧已实现角色区分、周期采样、执行器控制、首帧确认判定和本地状态显示
- `pc_host/` 提供了答辩和联调可直接使用的 TCP 服务端、浏览器仪表盘和 mock 网关

## 11. 答辩分工说明

仓库内新增了 5 人答辩型分工落地说明，可直接用于汇报：

- [docs/defense_split_5_people.md](/D:/ZNJJ/docs/defense_split_5_people.md)
