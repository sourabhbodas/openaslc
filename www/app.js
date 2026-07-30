const TELEMETRY_PORT = 8081;
const NUM_BYTES = 8; // rows shown per area -- MemoryMap has 512 bytes each, this is an MVP window
const CELL = 34;
const LABEL_W = 46;
const ROW_H = 34;
const AREA_GAP = 24;
const INPUT_TOP = 24;
const OUTPUT_TOP = INPUT_TOP + NUM_BYTES * ROW_H + AREA_GAP;

const canvas = document.getElementById("io-canvas");
const ctx = canvas.getContext("2d");
const statusEl = document.getElementById("status");
const seqEl = document.getElementById("seq");

let lastFrame = null;

function setStatus(connected) {
  statusEl.textContent = connected ? "connected" : "disconnected";
  statusEl.className = "status " + (connected ? "status-connected" : "status-disconnected");
}

function drawArea(label, bytes, top) {
  ctx.fillStyle = "#ccc";
  ctx.font = "12px ui-monospace, monospace";
  ctx.fillText(label, 4, top - 6);

  for (let byteIdx = 0; byteIdx < NUM_BYTES; byteIdx++) {
    const y = top + byteIdx * ROW_H + ROW_H / 2;
    ctx.fillStyle = "#999";
    ctx.fillText(`[${byteIdx}]`, 4, y + 4);

    const value = bytes ? bytes[byteIdx] || 0 : 0;
    for (let bit = 0; bit < 8; bit++) {
      const x = LABEL_W + bit * CELL + CELL / 2;
      const on = ((value >> bit) & 1) === 1;
      ctx.beginPath();
      ctx.arc(x, y, 10, 0, Math.PI * 2);
      ctx.fillStyle = on ? "#2ecc71" : "#555";
      ctx.fill();
    }
  }
}

function render() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  drawArea("%I (click a bit to simulate an input)", lastFrame ? lastFrame.I : null, INPUT_TOP);
  drawArea("%Q (read-only, driven by logic)", lastFrame ? lastFrame.Q : null, OUTPUT_TOP);
}

// %I is the only clickable area -- %Q is computed by the running logic each
// scan cycle, so letting a user "set" it directly would misrepresent it as
// an input rather than a driver-facing output.
canvas.addEventListener("click", (event) => {
  const rect = canvas.getBoundingClientRect();
  const x = event.clientX - rect.left;
  const y = event.clientY - rect.top;

  if (y < INPUT_TOP || y >= INPUT_TOP + NUM_BYTES * ROW_H || x < LABEL_W) {
    return;
  }
  const byteIdx = Math.floor((y - INPUT_TOP) / ROW_H);
  const bit = Math.floor((x - LABEL_W) / CELL);
  if (byteIdx < 0 || byteIdx >= NUM_BYTES || bit < 0 || bit > 7) {
    return;
  }

  const currentByte = (lastFrame && lastFrame.I && lastFrame.I[byteIdx]) || 0;
  const currentValue = (currentByte >> bit) & 1;

  fetch("/api/input", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ byte: byteIdx, bit, value: currentValue === 0 }),
  }).catch((e) => console.error("input toggle failed", e));
  // No local state mutation here -- the next telemetry frame (within
  // ~100ms) is the single source of truth for what actually happened.
});

function connect() {
  const ws = new WebSocket(`ws://${location.hostname}:${TELEMETRY_PORT}/ws/telemetry`);

  ws.onopen = () => setStatus(true);
  ws.onclose = () => {
    setStatus(false);
    setTimeout(connect, 1000);
  };
  ws.onerror = () => ws.close();

  ws.onmessage = (event) => {
    try {
      lastFrame = JSON.parse(event.data);
      seqEl.textContent = `seq ${lastFrame.seq}`;
      render();
    } catch (e) {
      console.error("bad telemetry frame", e);
    }
  };
}

function renderHistory(commits) {
  const list = document.getElementById("history-list");
  list.innerHTML = "";
  for (const commit of commits) {
    const li = document.createElement("li");
    const sha = commit.sha256.slice(0, 8);
    const author = commit.author || "anonymous";
    const message = commit.message ? ` — ${commit.message}` : "";
    li.textContent = `${sha}  ${commit.created_at}  ${author}${message}`;
    list.appendChild(li);
  }
}

async function refreshHistory() {
  try {
    const res = await fetch("/api/history");
    if (!res.ok) {
      return;
    }
    const body = await res.json();
    renderHistory(body.commits || []);
  } catch (e) {
    console.error("history fetch failed", e);
  }
}

document.getElementById("deploy-btn").addEventListener("click", async () => {
  const input = document.getElementById("deploy-input").value;
  const author = document.getElementById("deploy-author").value;
  const message = document.getElementById("deploy-message").value;
  const statusOut = document.getElementById("deploy-status");
  statusOut.textContent = "deploying...";
  statusOut.style.color = "";

  const params = new URLSearchParams({ author, message });

  try {
    const res = await fetch(`/api/deploy?${params}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: input,
    });
    const body = await res.json();
    if (res.ok) {
      const commitPart = body.commit ? ` (commit ${body.commit.sha256.slice(0, 8)})` : "";
      statusOut.textContent = `deployed (${body.rule_count} rules)${commitPart}`;
      statusOut.style.color = "#7ee2a8";
      refreshHistory();
    } else {
      statusOut.textContent = body.error || "deploy failed";
      statusOut.style.color = "#e28080";
    }
  } catch (e) {
    statusOut.textContent = "request failed: " + e.message;
    statusOut.style.color = "#e28080";
  }
});

render();
connect();
refreshHistory();
