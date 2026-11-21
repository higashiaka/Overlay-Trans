const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const flush = () => new Promise((resolve) => setImmediate(resolve));

test("CLI settings hide DeepL fields, retain provider models, and show login errors", async () => {
  const elements = new Map();
  function element(id) {
    if (!elements.has(id)) elements.set(id, {
      value: "", textContent: "", disabled: false, hidden: false, classList: { add() {} },
      events: {}, addEventListener(name, handler) { this.events[name] = handler; },
    });
    return elements.get(id);
  }
  const messages = [];
  const cliRequests = [];
  const openedTabs = [];
  let saveError = null;
  let cliOffline = false;
  const context = {
    AbortSignal,
    async fetch(url, options) {
      cliRequests.push({ url, options });
      if (cliOffline) throw new Error("App offline");
      return { ok: true, status: 200, json: async () => ({
        installed: true, logged_in: true, connecting: false, message: "계정이 연결되었습니다.",
      }) };
    },
    document: { getElementById: element }, setInterval() {},
    chrome: {
      tabs: { create: async (options) => openedTabs.push(options) },
      storage: { local: { get: async () => ({ pairingCode: "47815-ABCDEFGHIJKLMNOP", showSource: true }), set() {} } },
      runtime: {
        getURL: (path) => `chrome-extension://test/${path}`,
        async sendMessage(message) {
          messages.push(message);
          if (message.type === "getStatus") return { status: "connected" };
          if (message.type === "getSettings") return { ok: true, settings: {
            translator: "local", source_language: "auto", target_language: "Korean", has_deepl_key: false,
            codex_model: "gpt-saved", claude_model: "claude-saved",
          } };
          // 예전 서비스 워커는 새 CLI 메시지에 응답하지 않는다.
          if (message.type === "getCliStatus" || message.type === "connectCli") return undefined;
          if (message.type === "saveSettings") return saveError ? { ok: false, reason: saveError } : { ok: true };
        },
      },
    },
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../popup/popup.js"), "utf8"), context);
  await flush();
  const translator = element("translator");
  translator.value = "codex";
  translator.events.change();
  await flush();
  assert.equal(element("deepl-fields").hidden, true);
  assert.equal(element("cli-fields").hidden, false);
  assert.equal(element("cli-model").value, "gpt-saved");
  assert.equal(element("cli-status").textContent, "계정이 연결되었습니다.");
  assert.equal(cliRequests[0].url, "http://127.0.0.1:47815/v1/cli/codex");
  assert.equal(cliRequests[0].options.headers.Authorization, "Bearer ABCDEFGHIJKLMNOP");
  await element("cli-refresh").events.click();
  assert.match(element("cli-status").textContent, /확인\)/);
  element("cli-model").value = "gpt-new";
  translator.value = "claude";
  translator.events.change();
  await flush();
  assert.equal(element("cli-model").value, "claude-saved");
  await element("save-settings").events.click();
  const saved = messages.findLast((message) => message.type === "saveSettings");
  assert.equal(saved.settings.codex_model, "gpt-new");
  assert.equal(saved.settings.claude_model, "claude-saved");
  assert.equal("deepl_key" in saved.settings, false);
  saveError = "cli_not_ready";
  await element("save-settings").events.click();
  assert.match(element("settings-result").textContent, /설치·로그인/);
  await element("cli-connect").events.click();
  assert.equal(cliRequests.at(-1).url, "http://127.0.0.1:47815/v1/cli/claude/connect");
  assert.equal(cliRequests.at(-1).options.method, "POST");
  assert.equal(element("cli-connect").disabled, false);
  await element("open-settings").events.click();
  assert.equal(openedTabs[0].url, "chrome-extension://test/popup/popup.html");
  cliOffline = true;
  await element("cli-refresh").events.click();
  assert.equal(element("cli-refresh").disabled, false);
  assert.equal(element("cli-connect").disabled, false);
  assert.match(element("cli-status").textContent, /Windows 앱이 실행 중/);
  translator.value = "deepl";
  translator.events.change();
  assert.equal(element("deepl-fields").hidden, false);
  assert.equal(element("cli-fields").hidden, true);
  await element("save-settings").events.click();
  assert.match(element("settings-result").textContent, /API 키/);
  cliOffline = false;
  saveError = null;
  translator.value = "codex-hybrid";
  translator.events.change();
  await flush();
  assert.equal(element("deepl-fields").hidden, false);
  assert.equal(element("cli-fields").hidden, false);
  assert.equal(element("cli-model").value, "gpt-new");
  assert.equal(cliRequests.at(-1).url, "http://127.0.0.1:47815/v1/cli/codex");
  element("deepl-key").value = "test-key";
  await element("save-settings").events.click();
  assert.equal(messages.findLast((message) => message.type === "saveSettings").settings.translator, "codex-hybrid");
});

test("background validates providers and pairs account requests to the app", async () => {
  let listener;
  let responseStatus = 200;
  const requests = [];
  const context = {
    AbortSignal, setTimeout,
    chrome: {
      runtime: { onConnect: { addListener() {} }, onMessage: { addListener(value) { listener = value; } } },
      tabs: { onUpdated: { addListener() {} } },
      storage: { local: { get: async () => ({ pairingCode: "47815-ABCDEFGHIJKLMNOP" }) } },
    },
    async fetch(url, options) {
      requests.push({ url, options });
      return { ok: responseStatus === 200, status: responseStatus, json: async () => ({ logged_in: true }) };
    },
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../background.js"), "utf8"), context);
  const send = (message) => new Promise((resolve) => listener(message, {}, resolve));
  assert.equal((await send({ type: "getCliStatus", provider: "../../anything" })).ok, false);
  assert.equal(requests.length, 0);
  assert.equal((await send({ type: "getCliStatus", provider: "codex" })).account.logged_in, true);
  assert.equal(requests[0].url, "http://127.0.0.1:47815/v1/cli/codex");
  assert.equal(requests[0].options.headers.Authorization, "Bearer ABCDEFGHIJKLMNOP");
  await send({ type: "connectCli", provider: "claude" });
  assert.equal(requests[1].url, "http://127.0.0.1:47815/v1/cli/claude/connect");
  assert.equal(requests[1].options.method, "POST");
  responseStatus = 409;
  assert.equal((await send({ type: "saveSettings", settings: {} })).reason, "cli_not_ready");
  responseStatus = 404;
  assert.equal((await send({ type: "getCliStatus", provider: "claude" })).reason, "unsupported");
});
