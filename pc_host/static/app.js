const appState = {
  snapshot: null,
  feedback: {
    transport: null,
    command: null,
    threshold: null,
  },
};

document.addEventListener("DOMContentLoaded", () => {
  wireTransportActions();
  wireCommandActions();
  wireThresholdActions();
  loadSnapshot();
  connectEventStream();
});

function wireTransportActions() {
  document.getElementById("transport-form").addEventListener("submit", async (event) => {
    event.preventDefault();

    const host = document.getElementById("transport-host").value.trim();
    const port = Number(document.getElementById("transport-port").value);

    try {
      const result = await postJson("/api/transport/open", { host, port });
      if (result.ok) {
        setFeedback("transport", `正在监听 ${host}:${port}`, "success");
      } else {
        setFeedback("transport", result.error || "开启端口失败。", "error");
      }
    } catch (error) {
      setFeedback("transport", String(error), "error");
    }

    await loadSnapshot();
  });

  document.getElementById("transport-close-btn").addEventListener("click", async () => {
    try {
      const result = await postJson("/api/transport/close");
      if (result.ok) {
        setFeedback("transport", "TCP 监听已关闭。", "success");
      } else {
        setFeedback("transport", result.error || "关闭监听失败。", "error");
      }
    } catch (error) {
      setFeedback("transport", String(error), "error");
    }

    await loadSnapshot();
  });
}

function wireCommandActions() {
  document.querySelectorAll(".command-btn").forEach((button) => {
    button.addEventListener("click", async () => {
      const node = Number(button.dataset.node);
      const target = button.dataset.target;
      const op = button.dataset.op;

      await sendGatewayCommand(
        { t: "cmd", node, target, op },
        {
          feedbackKey: "command",
          successText: `命令已发送: 节点${node} ${target} ${op}`,
          failureText: `命令发送失败: 节点${node} ${target} ${op}`,
        },
      );
    });
  });
}

function wireThresholdActions() {
  document.getElementById("threshold-form").addEventListener("submit", async (event) => {
    event.preventDefault();

    const form = new FormData(event.currentTarget);
    const payload = { t: "set_threshold" };

    for (const [key, value] of form.entries()) {
      if (value !== "") {
        payload[key] = key === "lux_low" ? Number(value) : Number(value);
      }
    }

    await sendGatewayCommand(payload, {
      feedbackKey: "threshold",
      successText: "阈值更新已发送至网关。",
      failureText: "阈值更新失败。",
    });
  });
}

async function sendGatewayCommand(payload, options) {
  try {
    const result = await postJson("/api/command", payload);
    if (result.ok) {
      setFeedback(options.feedbackKey, options.successText, "success");
    } else {
      const detail = result.error ? ` ${result.error}` : "";
      setFeedback(options.feedbackKey, `${options.failureText}${detail}`, "error");
    }
  } catch (error) {
    setFeedback(options.feedbackKey, `${options.failureText} ${String(error)}`, "error");
  }

  await loadSnapshot();
}

async function loadSnapshot() {
  const response = await fetch("/api/state", { cache: "no-store" });
  const snapshot = await response.json();
  render(snapshot);
}

function connectEventStream() {
  const stream = new EventSource("/api/events");
  stream.addEventListener("state", (event) => {
    const envelope = JSON.parse(event.data);
    render(envelope.state);
  });
}

async function postJson(url, payload) {
  const response = await fetch(url, {
    method: "POST",
    headers: payload ? { "Content-Type": "application/json" } : {},
    body: payload ? JSON.stringify(payload) : undefined,
  });
  return response.json();
}

function render(snapshot) {
  appState.snapshot = snapshot;
  renderTransport(snapshot.transport);
  renderGateway(snapshot.gateway, snapshot.last_pong);
  renderNodes(snapshot.nodes, snapshot.aliases);
  renderThresholds(snapshot.thresholds);
  renderNetwork(snapshot.network);
  renderAlarms(snapshot.active_alarms);
  renderCommandAvailability(snapshot.gateway.connected);
  renderFeedbackDefaults(snapshot);
}

function renderTransport(transport) {
  syncInputValue("transport-host", transport.host);
  syncInputValue("transport-port", transport.port);

  const pill = document.getElementById("transport-pill");
  const openButton = document.querySelector("#transport-form .primary-btn");
  const closeButton = document.getElementById("transport-close-btn");

  if (transport.listening) {
    setPill(pill, "端口开启", "good");
    openButton.textContent = "重新绑定端口";
    closeButton.disabled = false;
  } else {
    setPill(pill, "端口关闭", transport.last_error ? "warn" : "neutral");
    openButton.textContent = "开启端口";
    closeButton.disabled = true;
  }

  document.getElementById("transport-endpoint").textContent = `${transport.host}:${transport.port}`;
  document.getElementById("transport-listener").textContent = transport.listening ? "监听中" : "已停止";
  document.getElementById("transport-action").textContent = formatTimestamp(transport.last_action_ms);
  document.getElementById("transport-kind").textContent = transport.kind ?? "tcp_server";

  if (transport.last_error) {
        setFeedback("transport", transport.last_error, "error", "default");
  }
}

function renderGateway(gateway, lastPong) {
  const gatewayPill = document.getElementById("gateway-pill");
  const connectionText = gateway.connected ? "已连接" : "离线";

  setPill(
    gatewayPill,
    gateway.connected ? "网关在线" : "网关离线",
    gateway.connected ? "good" : "neutral",
  );

  document.getElementById("fw-text").textContent = gateway.fw ?? "--";
  document.getElementById("gwid-text").textContent = gateway.gw_id ?? "--";
  document.getElementById("peer-text").textContent = gateway.peer ?? "--";
  document.getElementById("pong-text").textContent = lastPong?.ts ? formatTimestamp(lastPong.ts) : "--";

  document.getElementById("gateway-connection-text").textContent = connectionText;
  document.getElementById("gateway-seen-text").textContent = formatTimestamp(gateway.last_seen_ms);
  document.getElementById("gateway-fw-text").textContent = gateway.fw ?? "--";
  document.getElementById("gateway-id-text").textContent = gateway.gw_id ?? "--";
}

function renderNodes(nodes, aliases) {
  const node1 = resolveNode(nodes, aliases.node1, "temp_hum");
  const node2 = resolveNode(nodes, aliases.node2, "lux");

  renderNodeCard({
    node: node1,
    pillId: "node1-pill",
    addressId: "node1-address",
    statusId: "node1-status",
    updatedId: "node1-updated",
    rssiId: "node1-rssi",
    ledId: "node1-led",
    buzzerId: "node1-buzzer",
    valueIds: [
      ["node1-temp-value", formatNumber(node1?.temp, 1)],
      ["node1-hum-value", formatNumber(node1?.hum, 0)],
    ],
    waitingText: "等待温湿度数据",
  });

  renderNodeCard({
    node: node2,
    pillId: "node2-pill",
    addressId: "node2-address",
    statusId: "node2-status",
    updatedId: "node2-updated",
    rssiId: "node2-rssi",
    ledId: "node2-led",
    buzzerId: "node2-buzzer",
    valueIds: [["node2-lux-value", formatNumber(node2?.lux, 0)]],
    waitingText: "等待光照数据",
  });
}

function renderNodeCard(config) {
  const pill = document.getElementById(config.pillId);
  const node = config.node;
  const hasData = Boolean(node);

  if (!hasData) {
    setPill(pill, "等待中", "neutral");
    document.getElementById(config.addressId).textContent = "--";
    document.getElementById(config.statusId).textContent = config.waitingText;
    document.getElementById(config.updatedId).textContent = "--";
    document.getElementById(config.rssiId).textContent = "--";
    document.getElementById(config.ledId).textContent = "--";
    document.getElementById(config.buzzerId).textContent = "--";
    config.valueIds.forEach(([id]) => {
      document.getElementById(id).textContent = "--";
    });
    return;
  }

  setPill(pill, node.online ? "在线" : "离线", node.online ? "good" : "bad");
  document.getElementById(config.addressId).textContent = formatNodeAddress(node.node);
  document.getElementById(config.statusId).textContent = node.online ? "正在向网关上报" : "已知但已离线";
  document.getElementById(config.updatedId).textContent = formatTimestamp(node.last_update_ts);
  document.getElementById(config.rssiId).textContent = node.rssi ?? "--";
  document.getElementById(config.ledId).textContent = formatState(node.led);
  document.getElementById(config.buzzerId).textContent = formatState(node.buzzer);
  config.valueIds.forEach(([id, value]) => {
    document.getElementById(id).textContent = value;
  });
}

function renderThresholds(thresholds) {
  syncInputValue("threshold-lux-low", thresholds.lux_low);
  syncInputValue("threshold-temp-high", thresholds.temp_high);
  syncInputValue("threshold-temp-low", thresholds.temp_low);
  syncInputValue("threshold-hum-high", thresholds.hum_high);
  syncInputValue("threshold-hum-low", thresholds.hum_low);
}

function renderNetwork(network) {
  document.getElementById("network-state").textContent = network.state ?? "--";
  document.getElementById("network-channel").textContent = network.channel ?? "--";
  document.getElementById("network-panid").textContent = network.panid ?? "--";
  document.getElementById("network-joined").textContent = network.joined ?? "--";
}

function renderAlarms(activeAlarms) {
  const root = document.getElementById("alarm-stack");
  root.replaceChildren();

  if (!activeAlarms.length) {
    const empty = document.createElement("div");
    empty.className = "alarm-empty";
    empty.textContent = "暂无报警。";
    root.appendChild(empty);
    return;
  }

  activeAlarms.forEach((alarm) => {
    const row = document.createElement("article");
    row.className = "alarm-item";

    const copy = document.createElement("div");
    copy.className = "alarm-copy";

    const title = document.createElement("strong");
    title.textContent = alarmLabel(alarm.type);

    const body = document.createElement("span");
    body.textContent = `Current ${alarm.val ?? "--"} / Threshold ${alarm.threshold ?? "--"}`;

    copy.append(title, body);

    const meta = document.createElement("div");
    meta.className = "alarm-meta";
    meta.textContent = formatTimestamp(alarm.ts);

    row.append(copy, meta);
    root.appendChild(row);
  });
}

function renderCommandAvailability(gatewayConnected) {
  document.querySelectorAll(".command-btn").forEach((button) => {
    button.disabled = !gatewayConnected;
  });
  document.getElementById("threshold-submit-btn").disabled = !gatewayConnected;
}

function renderFeedbackDefaults(snapshot) {
  if (!appState.feedback.transport || appState.feedback.transport.source === "default") {
    if (snapshot.transport.last_error) {
      setFeedback("transport", snapshot.transport.last_error, "error", "default");
    } else {
      setFeedback("transport", "通信状态反馈将显示在此处。", "neutral", "default");
    }
  }

  if (!appState.feedback.command || appState.feedback.command.source === "default") {
    setFeedback(
      "command",
      snapshot.gateway.connected ? "网关已就绪，可发送直接控制命令。" : "连接网关以启用直接控制功能。",
      snapshot.gateway.connected ? "neutral" : "error",
      "default",
    );
  }

  if (!appState.feedback.threshold || appState.feedback.threshold.source === "default") {
    setFeedback(
      "threshold",
      snapshot.gateway.connected ? "现在可以下发更新阈值。" : "连接网关以下发阈值。",
      snapshot.gateway.connected ? "neutral" : "error",
      "default",
    );
  }
}

function resolveNode(nodes, aliasId, role) {
  if (aliasId != null) {
    const direct = nodes.find((node) => node.node === aliasId);
    if (direct) {
      return direct;
    }
  }
  return nodes.find((node) => node.role === role) ?? null;
}

function setFeedback(key, message, tone, source = "manual") {
  appState.feedback[key] = { message, tone, source };

  const element = document.getElementById(`${key}-feedback`);
  if (!element) {
    return;
  }

  element.textContent = message;
  element.className = tone === "neutral" ? "inline-feedback" : `inline-feedback ${tone}`;
}

function setPill(element, text, tone) {
  element.textContent = text;
  element.className = `status-pill ${tone}`;
}

function syncInputValue(id, value) {
  const input = document.getElementById(id);
  if (document.activeElement !== input) {
    input.value = String(value ?? "");
  }
}

function formatNumber(value, digits) {
  if (value == null || value === "") {
    return "--";
  }
  const number = Number(value);
  if (Number.isNaN(number)) {
    return "--";
  }
  return number.toFixed(digits);
}

function formatNodeAddress(value) {
  if (value == null) {
    return "--";
  }
  const hex = Number(value).toString(16).toUpperCase().padStart(4, "0");
  return `${value} (0x${hex})`;
}

function formatTimestamp(ts) {
  if (!ts) {
    return "--";
  }
  return new Date(ts).toLocaleString("zh-CN", {
    hour12: false,
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function formatState(value) {
  if (!value) {
    return "--";
  }
  return String(value).toUpperCase();
}

function alarmLabel(type) {
  if (type === "light") {
    return "光照报警";
  }
  if (type === "temp") {
    return "温度报警";
  }
  if (type === "hum") {
    return "湿度报警";
  }
  return type || "未知报警";
}
