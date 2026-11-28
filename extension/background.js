// OverLay-Trans 앱의 로컬 서버에서 자막을 받아 와, 방송 페이지에 떠 있는 콘텐츠 스크립트로 전달한다.
//
// 로컬 서버 접속을 여기(서비스 워커)에서 하는 이유: 앱은 일반 웹페이지에서 온 요청을 거부하므로,
// 페이지 안에서 도는 콘텐츠 스크립트가 아니라 확장 프로그램 자신이 요청을 보내야 한다.

const RETRY_DELAY_MS = 2000;
const PAIRING_CODE_PATTERN = /^(\d{2,5})-([0-9A-Z]{16})$/;

const ports = new Set();
const audioPorts = new Map();
let status = "idle"; // idle | unpaired | connected | rejected | disconnected
let polling = false;
// 방송 탭이 마지막으로 알려 준 진단 정보 (찾은 채팅 줄 수, 보낸 채팅 수).
let diagnostics = null;
let captureSession = null;
let startingViewing = false;
const captureReady = chrome.storage.session.get("captureSession").then((saved) => {
  captureSession = saved.captureSession ?? null;
});

async function stopViewing() {
  await captureReady;
  const previous = captureSession;
  captureSession = null;
  await chrome.storage.session.remove("captureSession");
  if (previous) await post("/v1/audio/stop", { session: previous.session });
  broadcast({ type: "captureState", active: false });
}

chrome.tabs.onRemoved.addListener((tabId) => {
  captureReady.then(() => {
    if (tabId === captureSession?.viewerTabId || tabId === captureSession?.sourceTabId) stopViewing();
  });
});

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function parsePairingCode(code) {
  const match = PAIRING_CODE_PATTERN.exec(String(code ?? "").trim().toUpperCase());
  return match ? { port: Number(match[1]), key: match[2] } : null;
}

async function loadPairing() {
  const { pairingCode } = await chrome.storage.local.get("pairingCode");
  return parsePairingCode(pairingCode);
}

async function request(pairing, path) {
  return fetch(`http://127.0.0.1:${pairing.port}${path}`, {
    headers: { Authorization: `Bearer ${pairing.key}` },
  });
}

// 방송 페이지에서 읽은 내용(방송 정보, 채팅)을 앱으로 보낸다.
// 앱이 꺼져 있으면 실패하지만, 지나간 채팅을 나중에 보내 봐야 쓸모가 없으므로 다시 보내지 않는다.
async function post(path, body) {
  const pairing = await loadPairing();
  if (!pairing) {
    return;
  }
  try {
    await fetch(`http://127.0.0.1:${pairing.port}${path}`, {
      method: "POST",
      headers: { Authorization: `Bearer ${pairing.key}`, "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
  } catch {
    // 연결 상태는 자막을 받아 오는 쪽에서 관리한다.
  }
}

function broadcast(message) {
  for (const port of ports) {
    try {
      port.postMessage(message);
    } catch {
      ports.delete(port);
      audioPorts.delete(port);
    }
  }
}

chrome.tabs.onUpdated.addListener((tabId, { audible, mutedInfo }, tab) => {
  if (audible === undefined && mutedInfo === undefined) return;
  for (const [port, audioTabId] of audioPorts) {
    if (tabId !== audioTabId) continue;
    try {
      port.postMessage({ type: "tabAudio", audible: !!tab.audible && !tab.mutedInfo?.muted });
    } catch {
      ports.delete(port);
      audioPorts.delete(port);
    }
  }
});

function setStatus(next) {
  if (status !== next) {
    status = next;
    broadcast({ type: "status", status });
  }
}

// 방송 탭이 하나라도 열려 있는 동안 자막을 계속 받아 온다.
// 앱은 새 자막이 없으면 응답을 최대 20초 붙잡아 두므로, 응답을 받는 즉시 다시 요청하면 된다.
async function pollSubtitles() {
  if (polling) {
    return;
  }
  polling = true;

  let after = null; // 마지막으로 받은 자막 번호. null이면 지금부터 나오는 자막만 받는다.
  while (ports.size > 0) {
    const pairing = await loadPairing();
    if (!pairing) {
      setStatus("unpaired");
      await sleep(RETRY_DELAY_MS);
      continue;
    }

    try {
      const response = await request(pairing, after === null ? "/v1/subtitles" : `/v1/subtitles?after=${after}`);
      if (response.status === 401) {
        setStatus("rejected");
        await sleep(RETRY_DELAY_MS);
        continue;
      }
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const body = await response.json();
      setStatus("connected");
      for (const subtitle of body.subtitles) {
        broadcast({
          type: "subtitle",
          sequence: subtitle.sequence,
          replaces: subtitle.replaces ?? 0,
          source: subtitle.source,
          translation: subtitle.translation,
          audio_session: subtitle.audio_session ?? "",
          speech_ended_at_ms: subtitle.speech_ended_at_ms ?? 0,
          latency_ms: subtitle.latency_ms ?? 0,
          final: subtitle.final ?? true,
        });
      }
      after = body.latest;
    } catch {
      // 앱이 꺼져 있거나 다시 시작된 경우. 번호가 처음부터 다시 매겨지므로 위치도 초기화한다.
      setStatus("disconnected");
      after = null;
      await sleep(RETRY_DELAY_MS);
    }
  }

  polling = false;
  status = "idle";
}

chrome.runtime.onConnect.addListener((port) => {
  const audioOnly = port.name === "subtitles-audio";
  const viewer = port.name === "subtitles-viewer";
  if (port.name !== "subtitles" && !audioOnly && !viewer) {
    return;
  }
  ports.add(port);
  port.postMessage({ type: "status", status });
  captureReady.then(() => {
    try { port.postMessage({ type: "captureState", active: !!captureSession }); } catch { /* Tab closed during startup. */ }
  });
  if (audioOnly && port.sender?.tab) {
    const tab = port.sender.tab;
    audioPorts.set(port, tab.id);
    port.postMessage({ type: "tabAudio", audible: !!tab.audible && !tab.mutedInfo?.muted });
  }
  // 콘텐츠 스크립트가 주기적으로 보내는 메시지는 서비스 워커가 잠들지 않게 하는 역할도 한다.
  port.onMessage.addListener((message) => {
    if (audioOnly || viewer) return;
    if (message?.type === "context") {
      post("/v1/context", message.context);
    } else if (message?.type === "chat") {
      post("/v1/chat", { messages: message.messages });
    } else if (message?.type === "diagnostics") {
      diagnostics = message.diagnostics;
    }
  });
  port.onDisconnect.addListener(() => {
    ports.delete(port);
    audioPorts.delete(port);
  });
  pollSubtitles();
});

// 팝업에서 오는 요청을 처리한다.
chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type === "startViewing") {
    if (startingViewing) {
      sendResponse({ ok: false, error: "시청 창을 여는 중입니다." });
      return false;
    }
    startingViewing = true;
    (async () => {
      await captureReady;
      if (captureSession) {
        await chrome.tabs.update(captureSession.viewerTabId, { active: true });
        return { ok: true };
      }
      const pairing = await loadPairing();
      if (!pairing) throw new Error("먼저 Windows 앱과 연결해 주세요.");
      const ping = await request(pairing, "/v1/ping");
      if (!ping.ok || !(await ping.json()).browser_audio) throw new Error("원본 음성 분리를 지원하는 최신 Windows 앱이 필요합니다.");
      const [source] = await chrome.tabs.query({ active: true, currentWindow: true });
      if (!source?.id || !/^https?:/.test(source.url ?? "")) throw new Error("방송 탭에서 확장 프로그램을 열어 시작하세요.");
      const viewer = await chrome.tabs.create({ url: chrome.runtime.getURL("viewer/viewer.html"), active: false });
      try {
        const streamId = await chrome.tabCapture.getMediaStreamId({ targetTabId: source.id, consumerTabId: viewer.id });
        captureSession = { session: crypto.randomUUID(), sourceTabId: source.id, viewerTabId: viewer.id, streamId };
        await chrome.storage.session.set({ captureSession });
        broadcast({ type: "captureState", active: true });
        await chrome.tabs.update(viewer.id, { active: true });
        return { ok: true };
      } catch (error) {
        await chrome.tabs.remove(viewer.id);
        throw error;
      }
    })().then(sendResponse).catch((error) => sendResponse({ ok: false, error: error.message }))
      .finally(() => { startingViewing = false; });
    return true;
  }
  if (message?.type === "getCaptureSession") {
    captureReady.then(() => sendResponse({
      session: _sender.tab?.id === captureSession?.viewerTabId ? captureSession : null,
    }));
    return true;
  }
  if (message?.type === "getCaptureStream") {
    (async () => {
      await captureReady;
      if (_sender.tab?.id !== captureSession?.viewerTabId) throw new Error("시청 세션이 종료되었습니다.");
      const streamId = await chrome.tabCapture.getMediaStreamId({
        targetTabId: captureSession.sourceTabId, consumerTabId: captureSession.viewerTabId,
      });
      return { ok: true, streamId };
    })().then(sendResponse).catch((error) => sendResponse({ ok: false, error: error.message }));
    return true;
  }
  if (message?.type === "stopViewing") {
    captureReady.then(async () => {
      if (_sender.tab?.id !== captureSession?.viewerTabId) return { ok: false };
      await stopViewing();
      return { ok: true };
    }).then(sendResponse);
    return true;
  }
  if (message?.type === "getStatus") {
    sendResponse({ status, diagnostics });
    return false;
  }

  if (message?.type === "pair") {
    const pairing = parsePairingCode(message.code);
    if (!pairing) {
      sendResponse({ ok: false, reason: "format" });
      return false;
    }

    request(pairing, "/v1/ping")
      .then(async (response) => {
        if (response.ok) {
          await chrome.storage.local.set({ pairingCode: `${pairing.port}-${pairing.key}` });
          sendResponse({ ok: true });
        } else {
          sendResponse({ ok: false, reason: response.status === 401 ? "rejected" : "error" });
        }
      })
      .catch(() => sendResponse({ ok: false, reason: "unreachable" }));
    return true; // 응답을 나중에 보낸다.
  }

  if (message?.type === "getCliStatus" || message?.type === "connectCli") {
    if (!["codex", "claude"].includes(message.provider)) {
      sendResponse({ ok: false, reason: "invalid_provider" });
      return false;
    }
    loadPairing().then(async (pairing) => {
      if (!pairing) return { ok: false, reason: "unpaired" };
      try {
        const connecting = message.type === "connectCli";
        const response = await fetch(`http://127.0.0.1:${pairing.port}/v1/cli/${message.provider}${connecting ? "/connect" : ""}`, {
          method: connecting ? "POST" : "GET",
          headers: { Authorization: `Bearer ${pairing.key}` },
          signal: AbortSignal.timeout(12000),
        });
        if (!response.ok) return { ok: false, reason: response.status === 404 ? "unsupported" : "error" };
        return { ok: true, account: await response.json() };
      } catch {
        return { ok: false, reason: "unreachable" };
      }
    }).then(sendResponse);
    return true;
  }

  if (message?.type === "getSettings" || message?.type === "saveSettings") {
    loadPairing().then(async (pairing) => {
      if (!pairing) return { ok: false, reason: "unpaired" };
      try {
        const response = await fetch(`http://127.0.0.1:${pairing.port}/v1/settings`, {
          method: message.type === "saveSettings" ? "POST" : "GET",
          headers: { Authorization: `Bearer ${pairing.key}`, "Content-Type": "application/json" },
          ...(message.type === "saveSettings" ? { body: JSON.stringify(message.settings) } : {}),
        });
        if (!response.ok) return { ok: false, reason: response.status === 401 ? "rejected" : response.status === 409 ? "cli_not_ready" : "error" };
        return { ok: true, settings: await response.json() };
      } catch {
        return { ok: false, reason: "unreachable" };
      }
    }).then(sendResponse);
    return true;
  }

  return false;
});
