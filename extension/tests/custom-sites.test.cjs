const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

test("custom sites display subtitles on playing video without sending chat or context", () => {
  const sent = [];
  const shown = [];
  const attached = [];
  const timers = new Map();
  const video = {
    paused: false,
    ended: false,
    readyState: 2,
    getBoundingClientRect: () => ({ left: 0, top: 0, right: 640, bottom: 360 }),
  };
  let handleMessage;
  const sandbox = vm.createContext({
    document: { querySelectorAll: () => [video], addEventListener() {} },
    window: { addEventListener() {} },
    innerWidth: 800,
    innerHeight: 600,
    getComputedStyle: () => ({ visibility: "visible" }),
    setInterval: (callback, delay) => timers.set(delay, callback),
    chrome: {
      runtime: {
        connect: () => ({
          onMessage: { addListener: (callback) => { handleMessage = callback; } },
          onDisconnect: { addListener() {} },
          postMessage: (message) => sent.push(message.type),
        }),
      },
      storage: {
        local: { get: async () => ({ showSource: true }) },
        onChanged: { addListener() {} },
      },
    },
  });
  function load(file) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, "..", "content", file), "utf8"), sandbox);
  }
  load("sites/custom.js");
  sandbox.OverlayTrans.overlay = {
    attachToVideo: (element) => attached.push(element),
    setShowSource() {},
    show: (message) => shown.push(message.translation),
  };
  load("main.js");

  assert.deepEqual(attached, [video]);
  for (const callback of timers.values()) callback();
  assert.deepEqual(sent, ["keepalive"]);
  handleMessage({ type: "subtitle", translation: "첫 자막" });
  handleMessage({ type: "subtitle", audio_session: "delayed-viewer", translation: "캡처 화면에 들어가면 안 되는 자막" });
  video.paused = true;
  timers.get(1000)();
  handleMessage({ type: "subtitle", translation: "재생 중지 후 자막" });
  assert.equal(attached.at(-1), null);
  assert.deepEqual(shown, ["첫 자막"]);
});

test("audio-only tabs display subtitles without a video and allow time for the last translation", () => {
  const shown = [];
  let detached = 0;
  let receive;
  let stop;
  const sandbox = vm.createContext({
    setInterval(_callback, delay) {
      assert.equal(delay, 20000);
    },
    setTimeout(callback, delay) {
      assert.equal(delay, 12000);
      stop = callback;
      return 1;
    },
    clearTimeout() { stop = undefined; },
    chrome: {
      runtime: {
        connect({ name }) {
          assert.equal(name, "subtitles-audio");
          return {
            onMessage: { addListener(callback) { receive = callback; } },
            onDisconnect: { addListener() {} },
          };
        },
      },
      storage: {
        local: { get: async () => ({ showSource: true }) },
        onChanged: { addListener() {} },
      },
    },
  });
  const content = path.join(__dirname, "..", "content");
  vm.runInContext(fs.readFileSync(path.join(content, "sites", "audio.js"), "utf8"), sandbox);
  sandbox.OverlayTrans.overlay = {
    attachToPage() {},
    detach() { detached++; },
    setShowSource() {},
    show: ({ translation }) => shown.push(translation),
  };
  vm.runInContext(fs.readFileSync(path.join(content, "main.js"), "utf8"), sandbox);

  receive({ type: "subtitle", translation: "다른 탭의 자막" });
  receive({ type: "tabAudio", audible: true });
  receive({ type: "subtitle", translation: "재생 중 자막" });
  receive({ type: "tabAudio", audible: false });
  receive({ type: "subtitle", translation: "마지막 자막" });
  stop();
  receive({ type: "subtitle", translation: "다시 조용한 상태" });
  assert.deepEqual(shown, ["재생 중 자막", "마지막 자막"]);
  assert.equal(detached, 1);
});
