const appState = {
  snapshot: null,
  feedback: {
    transport: null,
    command: null,
    threshold: null,
  },
};

let scrollObserver;

document.addEventListener("DOMContentLoaded", () => {
  initPremiumEffects();
  wireTransportActions();
  wireCommandActions();
  wireThresholdActions();
  loadSnapshot();
  connectEventStream();
});

function initPremiumEffects() {
  // 1. Mouse tracking spotlight & Ambient Parallax
  document.addEventListener("mousemove", (e) => {
    requestAnimationFrame(() => {
      const x = e.clientX;
      const y = e.clientY;
      const moveX = (x - window.innerWidth / 2) * 0.05;
      const moveY = (y - window.innerHeight / 2) * 0.05;

      document.documentElement.style.setProperty('--mouse-x', `${x}px`);
      document.documentElement.style.setProperty('--mouse-y', `${y}px`);
      document.documentElement.style.setProperty('--parallax-x', `${moveX}px`);
      document.documentElement.style.setProperty('--parallax-y', `${moveY}px`);
    });
  });

  // 2. Intersection Observer for Smooth Scroll Revealing (Repeatable)
  scrollObserver = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
      if (entry.isIntersecting) {
        // 进入视图时：触发呈现动画
        entry.target.classList.add('in-view');
        
        // 动画结束清理类名，让原生的 Hover 等交互立刻恢复工作
        const onAnimEnd = () => {
          if (entry.target.classList.contains('in-view')) {
            entry.target.classList.remove(entry.target.dataset.revealType, 'reveal-stagger', 'in-view');
            entry.target.style.animationDelay = '';
          }
          entry.target.removeEventListener('animationend', onAnimEnd);
        };
        entry.target.addEventListener('animationend', onAnimEnd);
      } else {
        // 完全滑出视图时：复原状态，使得下次滚回来时可以重新触发动画
        entry.target.classList.remove('in-view');
        entry.target.classList.add(entry.target.dataset.revealType);
        
        if (entry.target.dataset.staggerDelay) {
          entry.target.classList.add('reveal-stagger');
          entry.target.style.animationDelay = entry.target.dataset.staggerDelay;
        }
      }
    });
  }, { rootMargin: '0px 0px 0px 0px', threshold: 0 });

  // Dynamically attach reveal classes to large blocks
  document.querySelectorAll('.glass-card').forEach(el => {
    el.dataset.revealType = 'reveal-card';
    el.classList.add('reveal-card');
  });
  
  // Dynamically attach to intra-card sub-elements
  const staggerSelectors = [
    '.detail-grid > div',
    '.hero-metric',
    '.metric-tile',
    '.control-group',
    '.network-stat'
  ];
  
  document.querySelectorAll(staggerSelectors.join(', ')).forEach(el => {
    el.dataset.revealType = 'reveal-item';
    el.classList.add('reveal-item', 'reveal-stagger');
  });

  // Distribute delays so elements cascade sequentially
  document.querySelectorAll('.glass-card, .hero-summary').forEach(parent => {
    const children = parent.querySelectorAll('.reveal-stagger');
    children.forEach((child, index) => {
      const delay = `${0.05 + index * 0.08}s`;
      child.dataset.staggerDelay = delay;
      child.style.animationDelay = delay;
    });
  });

  document.querySelectorAll('.reveal-card, .reveal-item').forEach(el => {
    scrollObserver.observe(el);
  });
}

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
          successText: `命令已下发，等待节点状态回传: 节点${node} ${target} ${op}`,
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
  renderNodes(snapshot.nodes, snapshot.aliases, snapshot.thresholds);
  renderThresholds(snapshot.thresholds);
  renderNetwork(snapshot.network);
  renderAlarmPanels(snapshot.active_alarms, snapshot.alarm_history);
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

function renderNodes(nodes, aliases, thresholds) {
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
    valueConfigs: [
      { id: "node1-temp-value", text: formatNumber(node1?.temp, 1), type: "temp" },
      { id: "node1-hum-value", text: formatNumber(node1?.hum, 0), type: "hum" },
    ],
    thresholds: thresholds,
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
    valueConfigs: [
      { id: "node2-lux-value", text: formatNumber(node2?.lux, 0), type: "lux" }
    ],
    thresholds: thresholds,
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
    config.valueConfigs.forEach((vc) => {
      updateMetricTile(vc.id, "--", vc.type, config.thresholds);
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
  config.valueConfigs.forEach((vc) => {
    updateMetricTile(vc.id, vc.text, vc.type, config.thresholds);
  });
}

function updateMetricTile(id, text, type, thresholds) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = text;
  
  const tile = el.closest(".metric-tile");
  if (!tile) return;

  if (text === "--") {
    tile.style.setProperty("--progress-width", "0%");
    tile.style.setProperty("--progress-color", "var(--muted)");
    el.style.setProperty("--value-color", "var(--ink)");
    tile.classList.remove("alarm-glow");
    return;
  }

  const num = Number(text);
  if (Number.isNaN(num)) return;

  let progress = 0;
  let color = "var(--ink)";
  let isAlarm = false;

  if (type === "temp") {
    progress = Math.min(Math.max((num / 50) * 100, 0), 100);
    if (num < 15) color = "#007aff"; // Cold (Blue)
    else if (num <= 28) color = "#34c759"; // Comfortable (Green)
    else color = "#ff3b30"; // Hot (Red)
    
    if (thresholds && thresholds.temp_high !== undefined && num > thresholds.temp_high) isAlarm = true;
  } else if (type === "hum") {
    progress = Math.min(Math.max(num, 0), 100);
    if (num < 30) color = "#ff9500"; // Dry (Orange)
    else if (num <= 60) color = "#34c759"; // Comfortable (Green)
    else color = "#007aff"; // Wet (Blue)

    if (thresholds && thresholds.hum_high !== undefined && num > thresholds.hum_high) isAlarm = true;
  } else if (type === "lux") {
    progress = Math.min(Math.max((num / 2000) * 100, 0), 100);
    if (num < 500) color = "#86868b"; // Dark (Gray)
    else if (num <= 1500) color = "#ff9500"; // Normal (Orange)
    else color = "#ffcc00"; // Bright (Yellow)

    if (thresholds && thresholds.lux_low !== undefined && num < thresholds.lux_low) isAlarm = true;
  }

  tile.style.setProperty("--progress-width", `${progress}%`);
  tile.style.setProperty("--progress-color", color);
  el.style.setProperty("--value-color", color);
  
  if (isAlarm) {
    tile.classList.add("alarm-glow");
  } else {
    tile.classList.remove("alarm-glow");
  }
}

function renderThresholds(thresholds) {
  syncInputValue("threshold-lux-low", thresholds.lux_low);
  syncInputValue("threshold-temp-high", thresholds.temp_high);
  syncInputValue("threshold-temp-low", thresholds.temp_low);
  syncInputValue("threshold-hum-high", thresholds.hum_high);
  syncInputValue("threshold-hum-low", thresholds.hum_low);
  syncInputValue("threshold-debounce-ms", thresholds.debounce_ms);
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

function renderAlarmPanels(activeAlarms, alarmHistory) {
  const root = document.getElementById("alarm-stack");
  root.replaceChildren();

  if (activeAlarms.length) {
    root.appendChild(buildAlarmSectionTitle("当前报警"));
    activeAlarms.forEach((alarm) => {
      root.appendChild(buildAlarmCard(alarm));
    });
    return;
  }

  if (alarmHistory.length) {
    root.appendChild(buildAlarmSectionTitle("最近事件"));
    alarmHistory.slice(0, 4).forEach((alarm) => {
      root.appendChild(buildAlarmCard(alarm, true));
    });
    return;
  }

  const empty = document.createElement("div");
  empty.className = "alarm-empty";
  empty.textContent = "暂无报警。";
  root.appendChild(empty);
}

function buildAlarmSectionTitle(text) {
  const title = document.createElement("p");
  title.className = "alarm-section-title";
  title.textContent = text;
  return title;
}

function buildAlarmCard(alarm, isHistory = false) {
  const row = document.createElement("article");
  row.className = "alarm-item";
  if (alarm.level !== "on" || isHistory) {
    row.classList.add("alarm-item-history");
  }

  const copy = document.createElement("div");
  copy.className = "alarm-copy";

  const head = document.createElement("div");
  head.className = "alarm-copy-head";

  const title = document.createElement("strong");
  title.textContent = alarmLabel(alarm.type);

  const state = document.createElement("span");
  state.className = `alarm-state ${alarm.level === "on" ? "active" : "recovered"}`;
  state.textContent = alarm.level === "on" ? "报警中" : "已恢复";

  head.append(title, state);

  const body = document.createElement("span");
  body.textContent = `异常值 ${formatAlarmValue(alarm.type, alarm.val)} / 阈值 ${formatAlarmValue(alarm.type, alarm.threshold)}`;

  copy.append(head, body);

  const meta = document.createElement("div");
  meta.className = "alarm-meta";
  meta.textContent = formatTimestamp(alarm.ts);

  row.append(copy, meta);
  return row;
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

function formatAlarmValue(type, value) {
  if (value == null || value === "") {
    return "--";
  }

  const number = Number(value);
  if (Number.isNaN(number)) {
    return "--";
  }

  if (type === "light") {
    return `${Math.round(number)} lux`;
  }
  if (type === "temp") {
    return `${(number / 100).toFixed(2)} °C`;
  }
  if (type === "hum") {
    return `${(number / 100).toFixed(2)} %`;
  }
  return String(value);
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
