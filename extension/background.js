// OverLay-Trans 앱의 로컬 서버에서 자막을 받아 와, 방송 페이지에 떠 있는 콘텐츠 스크립트로 전달한다.
//
// 로컬 서버 접속을 여기(서비스 워커)에서 하는 이유: 앱은 일반 웹페이지에서 온 요청을 거부하므로,
// 페이지 안에서 도는 콘텐츠 스크립트가 아니라 확장 프로그램 자신이 요청을 보내야 한다.

const RETRY_DELAY_MS = 2000;
const PAIRING_CODE_PATTERN = /^(\d{2,5})-([0-9A-Z]{16})$/;

const ports = new Set();
let status = "idle"; // idle | unpaired | connected | rejected | disconnected
let polling = false;
// 방송 탭이 마지막으로 알려 준 진단 정보 (찾은 채팅 줄 수, 보낸 채팅 수).
let diagnostics = null;

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
    }
  }
}

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
  if (port.name !== "subtitles") {
    return;
  }
  ports.add(port);
  port.postMessage({ type: "status", status });
  // 콘텐츠 스크립트가 주기적으로 보내는 메시지는 서비스 워커가 잠들지 않게 하는 역할도 한다.
  port.onMessage.addListener((message) => {
    if (message?.type === "context") {
      post("/v1/context", message.context);
    } else if (message?.type === "chat") {
      post("/v1/chat", { messages: message.messages });
    } else if (message?.type === "diagnostics") {
      diagnostics = message.diagnostics;
    }
  });
  port.onDisconnect.addListener(() => ports.delete(port));
  pollSubtitles();
});

// 팝업에서 오는 요청을 처리한다.
chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
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

  return false;
});
