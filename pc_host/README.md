# ZNJJ PC Host

`pc_host/` 提供仓库内置的上位机与联调展示平台，使用 Python 标准库实现，不依赖第三方包。

包含两部分：

- `run_host.py`：启动 TCP 服务端与浏览器仪表盘
- `run_mock_gateway.py`：启动一个模拟网关，在没有硬件时演示完整协议链路

## 1. 功能

- 监听 `STM32` 网关主动建立的 TCP 连接，兼容当前仓库的 JSON 行协议
- 在浏览器展示 `hello / report / status / alarm / net / ack / pong / node_info`
- 提供节点 LED、蜂鸣器控制
- 提供阈值配置、入网窗口控制、`ping`、`list_nodes`
- 保留最近日志，适合答辩联调演示

## 2. 启动上位机

在仓库根目录执行：

```powershell
python .\pc_host\run_host.py --tcp-port 23333 --http-port 8080
```

启动后：

- 网关 TCP 服务端：`0.0.0.0:23333`
- 浏览器仪表盘：`http://127.0.0.1:8080`

如果需要和固件默认配置对齐，`--tcp-port` 应与 [config.h](/D:/ZNJJ/STM32/App/config.h) 中的 `SERVER_PORT` 一致，当前默认值为 `23333`。

## 3. 无硬件演示

先启动上位机，再在另一个终端执行：

```powershell
python .\pc_host\run_mock_gateway.py --host 127.0.0.1 --port 23333
```

模拟网关会：

- 周期发送 `Node1` 温湿度和 `Node2` 光照上报
- 根据阈值模拟本地联动告警
- 响应浏览器下发的 `cmd / set_threshold / allow_join / list_nodes / ping`

## 4. 测试

```powershell
python -m unittest discover -s .\pc_host\tests
```

