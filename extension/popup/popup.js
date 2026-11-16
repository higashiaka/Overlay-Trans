const STATUS_TEXT = {
  idle: "열려 있는 방송 탭이 없습니다.",
  unpaired: "페어링 코드를 입력해 주세요.",
  connected: "앱에 연결되었습니다.",
  rejected: "앱이 페어링 코드를 거부했습니다. 코드를 다시 입력해 주세요.",
  disconnected: "앱에 연결할 수 없습니다. 앱이 실행 중인지 확인해 주세요.",
};

const PAIR_FAILURE_TEXT = {
  format: "코드 형식이 올바르지 않습니다. 예: 47815-XXXXXXXXXXXXXXXX",
  rejected: "앱이 이 코드를 거부했습니다. 앱에 표시된 코드를 다시 확인해 주세요.",
  unreachable: "앱에 연결할 수 없습니다. 앱이 실행 중인지 확인해 주세요.",
  error: "앱이 예상하지 못한 응답을 보냈습니다.",
};

const statusElement = document.getElementById("status");
const codeInput = document.getElementById("code");
const pairButton = document.getElementById("pair");
const resultElement = document.getElementById("result");
const showSourceCheckbox = document.getElementById("show-source");
const diagnosticsElement = document.getElementById("diagnostics");
const sourceLanguage = document.getElementById("source-language");
const targetLanguage = document.getElementById("target-language");
const translator = document.getElementById("translator");
const deeplFields = document.getElementById("deepl-fields");
const deeplKey = document.getElementById("deepl-key");
const keyStatus = document.getElementById("key-status");
const saveButton = document.getElementById("save-settings");
const settingsResult = document.getElementById("settings-result");
let hasDeeplKey = false;
const cliFields = document.getElementById("cli-fields");
const cliStatus = document.getElementById("cli-status");
const cliConnect = document.getElementById("cli-connect");
const cliRefresh = document.getElementById("cli-refresh");
const cliModel = document.getElementById("cli-model");
const cliModels = { codex: "", claude: "" };
let selectedCli = null;
let cliStatusPending = false;

function isCli() { return ["codex", "claude", "codex-hybrid"].includes(translator.value); }
function cliProvider() { return translator.value === "codex-hybrid" ? "codex" : translator.value; }
function usesDeepl() { return ["deepl", "hybrid", "compare", "codex-hybrid"].includes(translator.value); }

// 팝업 파일만 갱신되고 서비스 워커가 이전 버전인 경우에도 계정 연결은 동작해야 한다.
async function requestCli(provider, connect = false) {
  const { pairingCode } = await chrome.storage.local.get("pairingCode");
  const pairing = /^(\d{2,5})-([0-9A-Z]{16})$/.exec(String(pairingCode ?? "").trim().toUpperCase());
  if (!pairing) return { ok: false, reason: "unpaired" };
  const response = await fetch(`http://127.0.0.1:${Number(pairing[1])}/v1/cli/${provider}${connect ? "/connect" : ""}`, {
    method: connect ? "POST" : "GET",
    headers: { Authorization: `Bearer ${pairing[2]}` },
    signal: AbortSignal.timeout(12000),
  });
  if (!response.ok) return { ok: false, reason: response.status === 401 ? "rejected" : response.status === 404 ? "unsupported" : "error" };
  return { ok: true, account: await response.json() };
}

function cliFailureText(reason) {
  if (reason === "unpaired") return "먼저 위의 앱 연결에서 페어링 코드를 입력하고 연결해 주세요.";
  if (reason === "rejected") return "페어링 코드가 맞지 않습니다. 앱의 코드를 복사해 다시 연결해 주세요.";
  if (reason === "unsupported") return "CLI 연결을 지원하는 최신 Windows 앱을 실행해 주세요.";
  return "로컬 앱에 연결할 수 없습니다. Windows 앱이 실행 중인지 확인하고 다시 눌러 주세요.";
}

function updateDeeplFields() {
  if (selectedCli) cliModels[selectedCli] = cliModel.value.trim();
  selectedCli = isCli() ? cliProvider() : null;
  deeplFields.hidden = !usesDeepl();
  cliFields.hidden = !selectedCli;
  if (selectedCli) {
    cliModel.value = cliModels[selectedCli];
    cliStatus.textContent = "계정 연결 확인 중…";
    refreshCliStatus();
  }
}

async function refreshCliStatus(manual = false) {
  if (!isCli() || cliStatusPending) return;
  const mode = translator.value;
  const provider = cliProvider();
  cliStatusPending = true;
  cliRefresh.disabled = true;
  cliRefresh.textContent = "확인 중…";
  cliStatus.textContent = "계정 연결 확인 중…";
  try {
    const response = await requestCli(provider);
    if (mode !== translator.value) return;
    if (!response?.ok) {
      cliStatus.textContent = cliFailureText(response?.reason);
      cliConnect.disabled = false;
      return;
    }
    const account = response.account;
    cliStatus.textContent = account.message + (manual ? ` (${new Date().toLocaleTimeString()} 확인)` : "");
    cliConnect.disabled = account.connecting;
    cliConnect.textContent = account.connecting ? "로그인 진행 중…" : account.logged_in ? "다시 로그인" : account.installed ? "계정 로그인" : "설치·로그인";
  } catch {
    if (mode === translator.value) {
      cliStatus.textContent = cliFailureText();
      cliConnect.disabled = false;
    }
  } finally {
    cliStatusPending = false;
    cliRefresh.disabled = false;
    cliRefresh.textContent = "연결 확인";
    if (mode !== translator.value && isCli()) refreshCliStatus();
  }
}

async function loadSettings() {
  const response = await chrome.runtime.sendMessage({ type: "getSettings" });
  if (!response?.ok) {
    saveButton.disabled = true;
    settingsResult.textContent = "앱을 연결하면 번역 설정을 변경할 수 있습니다.";
    return;
  }
  saveButton.disabled = false;
  settingsResult.textContent = "";
  sourceLanguage.value = response.settings.source_language;
  targetLanguage.value = response.settings.target_language;
  translator.value = response.settings.translator;
  hasDeeplKey = response.settings.has_deepl_key;
  selectedCli = null;
  cliModels.codex = response.settings.codex_model ?? "";
  cliModels.claude = response.settings.claude_model ?? "";
  keyStatus.textContent = hasDeeplKey ? "기존 API 키가 앱에 저장되어 있습니다." : "저장된 API 키가 없습니다.";
  updateDeeplFields();
}

async function refreshStatus() {
  const { status, diagnostics } = await chrome.runtime.sendMessage({ type: "getStatus" });
  diagnosticsElement.textContent = diagnostics
    ? `화면에서 찾은 채팅 ${diagnostics.chatLines}줄 · 앱으로 보낸 채팅 ${diagnostics.sentChat}개`
    : "";
  statusElement.textContent = STATUS_TEXT[status] ?? status;
  statusElement.className = "status";
  if (status === "connected") {
    statusElement.classList.add("ok");
  } else if (status === "rejected" || status === "disconnected") {
    statusElement.classList.add("bad");
  }
}

async function pair() {
  pairButton.disabled = true;
  resultElement.textContent = "확인 중…";

  const response = await chrome.runtime.sendMessage({ type: "pair", code: codeInput.value });
  resultElement.textContent = response.ok ? "연결되었습니다." : PAIR_FAILURE_TEXT[response.reason];
  pairButton.disabled = false;
  refreshStatus();
  if (response.ok) loadSettings();
}

pairButton.addEventListener("click", pair);
translator.addEventListener("change", updateDeeplFields);
cliRefresh.addEventListener("click", () => refreshCliStatus(true));
document.getElementById("open-settings").addEventListener("click", async () => {
  try {
    await chrome.tabs.create({ url: chrome.runtime.getURL("popup/popup.html") });
  } catch {
    cliStatus.textContent = "설정 창을 열지 못했습니다. 확장앱 관리에서 이 확장 프로그램을 새로고침해 주세요.";
  }
});
cliConnect.addEventListener("click", async () => {
  const mode = translator.value;
  const provider = cliProvider();
  if (!isCli()) return;
  cliConnect.disabled = true;
  cliStatus.textContent = "로그인 창을 여는 중…";
  try {
    const response = await requestCli(provider, true);
    if (mode !== translator.value) return;
    cliStatus.textContent = response?.ok
      ? "열린 창에서 설치·로그인을 마친 뒤 연결 확인을 누르세요."
      : cliFailureText(response?.reason);
  } catch {
    if (mode === translator.value) cliStatus.textContent = cliFailureText();
  } finally {
    cliConnect.disabled = false;
  }
});
saveButton.addEventListener("click", async () => {
  if (usesDeepl() && !deeplKey.value.trim() && !hasDeeplKey) {
    settingsResult.textContent = "DeepL 번역을 사용하려면 API 키를 입력하세요.";
    return;
  }
  saveButton.disabled = true;
  settingsResult.textContent = "저장 중…";
  const enteredNewKey = Boolean(deeplKey.value.trim());
  if (selectedCli) cliModels[selectedCli] = cliModel.value.trim();
  const response = await chrome.runtime.sendMessage({
    type: "saveSettings",
    settings: {
      source_language: sourceLanguage.value,
      target_language: targetLanguage.value,
      translator: translator.value,
      codex_model: cliModels.codex,
      claude_model: cliModels.claude,
      ...(deeplKey.value.trim() ? { deepl_key: deeplKey.value.trim() } : {}),
    },
  });
  deeplKey.value = "";
  saveButton.disabled = false;
  if (response?.ok) {
    hasDeeplKey = enteredNewKey || hasDeeplKey;
    keyStatus.textContent = hasDeeplKey ? "기존 API 키가 앱에 저장되어 있습니다." : "저장된 API 키가 없습니다.";
    settingsResult.textContent = "저장했습니다. 다음 발화부터 적용됩니다.";
  } else {
    settingsResult.textContent = response?.reason === "cli_not_ready"
      ? "선택한 CLI의 설치·로그인을 마치고 연결을 확인한 뒤 다시 저장하세요."
      : "저장하지 못했습니다. 앱 연결과 입력값을 확인하세요.";
  }
});
codeInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    pair();
  }
});

showSourceCheckbox.addEventListener("change", () => {
  chrome.storage.local.set({ showSource: showSourceCheckbox.checked });
});

chrome.storage.local.get({ pairingCode: "", showSource: true }).then(({ pairingCode, showSource }) => {
  codeInput.value = pairingCode;
  showSourceCheckbox.checked = showSource;
});

refreshStatus();
loadSettings();
setInterval(refreshStatus, 1000);
setInterval(() => { if (!cliFields.hidden) refreshCliStatus(); }, 5000);
