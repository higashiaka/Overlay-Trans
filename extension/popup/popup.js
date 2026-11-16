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

function updateDeeplFields() {
  deeplFields.hidden = translator.value === "local";
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
saveButton.addEventListener("click", async () => {
  if (translator.value !== "local" && !deeplKey.value.trim() && !hasDeeplKey) {
    settingsResult.textContent = "DeepL 번역을 사용하려면 API 키를 입력하세요.";
    return;
  }
  saveButton.disabled = true;
  settingsResult.textContent = "저장 중…";
  const enteredNewKey = Boolean(deeplKey.value.trim());
  const response = await chrome.runtime.sendMessage({
    type: "saveSettings",
    settings: {
      source_language: sourceLanguage.value,
      target_language: targetLanguage.value,
      translator: translator.value,
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
    settingsResult.textContent = "저장하지 못했습니다. 앱 연결과 입력값을 확인하세요.";
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
