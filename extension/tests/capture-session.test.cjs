const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

test("capture startup, stream renewal, ownership, source close and single-session enforcement", async () => {
  let receive;
  let removed;
  let saved = {};
  let streamCount = 0;
  let created = 0;
  const requests = [];
  const updates = [];
  const sandbox = {
    setTimeout, crypto: { randomUUID: () => "test-session" },
    chrome: {
      runtime: {
        onConnect: { addListener() {} },
        onMessage: { addListener(listener) { receive = listener; } },
        getURL: (file) => `chrome-extension://test/${file}`,
      },
      storage: {
        local: { get: async () => ({ pairingCode: "47815-ABCDEFGHIJKLMNOP" }) },
        session: { get: async () => saved, set: async (value) => { saved = value; }, remove: async () => { saved = {}; } },
      },
      tabCapture: { getMediaStreamId: async ({ targetTabId, consumerTabId }) => {
        assert.equal(targetTabId, 1); assert.equal(consumerTabId, 2);
        return `stream-${++streamCount}`;
      } },
      tabs: {
        onUpdated: { addListener() {} }, onRemoved: { addListener(listener) { removed = listener; } },
        query: async () => [{ id: 1, url: "https://www.youtube.com/watch?v=test" }],
        create: async () => { created++; return { id: 2 }; },
        update: async (tabId, options) => { updates.push({ tabId, options }); }, remove: async () => {},
      },
    },
    fetch: async (url, options) => {
      requests.push({ url, options });
      return { ok: true, json: async () => ({ browser_audio: true }) };
    },
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../background.js"), "utf8"), sandbox);
  const send = (message, tabId) => new Promise((resolve) => receive(message, tabId ? { tab: { id: tabId } } : {}, resolve));
  assert.equal((await send({ type: "startViewing" })).ok, true);
  assert.equal(created, 1);
  assert.equal(saved.captureSession.sourceTabId, 1);
  assert.equal((await send({ type: "getCaptureSession" }, 1)).session, null);
  assert.equal((await send({ type: "getCaptureSession" }, 2)).session.session, "test-session");
  assert.equal((await send({ type: "getCaptureStream" }, 1)).ok, false);
  assert.equal((await send({ type: "getCaptureStream" }, 2)).streamId, "stream-2");
  assert.equal((await send({ type: "startViewing" })).ok, true);
  assert.equal(created, 1);
  assert.equal(updates.at(-1).tabId, 2);
  assert.equal((await send({ type: "stopViewing" }, 1)).ok, false);
  removed(1);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal((await send({ type: "getCaptureSession" }, 2)).session, null);
  assert.equal(requests.at(-1).url, "http://127.0.0.1:47815/v1/audio/stop");
  assert.equal(JSON.parse(requests.at(-1).options.body).session, "test-session");
});
