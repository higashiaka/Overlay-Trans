// Optional real Chromium smoke test. Install playwright-core outside the extension and set
// PLAYWRIGHT_MODULE and CHROMIUM_PATH, then run: node extension/tests/viewer-browser.cjs
// Only tabCapture/getUserMedia are replaced with a synthetic A/V stream; encoding, MSE,
// AudioWorklet, PCM HTTP transport, subtitle timing, controls and teardown run unchanged.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const http = require("node:http");
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || "playwright-core");

(async () => {
  let session;
  let audioCount = 0;
  let latest = 0;
  let stopped = false;
  const subtitles = [];
  const server = http.createServer(async (request, response) => {
    response.setHeader("Access-Control-Allow-Origin", "*");
    response.setHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
    if (request.method === "OPTIONS") { response.writeHead(204).end(); return; }
    const parts = [];
    for await (const chunk of request) parts.push(chunk);
    const body = Buffer.concat(parts);
    const url = new URL(request.url, "http://localhost");
    if (url.pathname === "/v1/audio/start") session = JSON.parse(body).session;
    if (url.pathname === "/v1/audio/stop") stopped = true;
    if (url.pathname === "/v1/audio") {
      assert.equal(url.searchParams.get("session"), session);
      assert.equal(Number(url.searchParams.get("sequence")), audioCount++);
      assert.equal(body.length, 6400);
      for (let offset = 0; offset < body.length; offset += 4) assert.ok(Number.isFinite(body.readFloatLE(offset)));
      if (audioCount % 10 === 0) {
        const end = Number(url.searchParams.get("ended_at_ms"));
        setTimeout(() => subtitles.push({ sequence: ++latest, replaces: 0, source: "test", translation: "동기화 테스트", audio_session: session, speech_ended_at_ms: end, latency_ms: 1000, final: true }), 1000);
      }
    }
    if (url.pathname === "/v1/subtitles") {
      await new Promise((resolve) => setTimeout(resolve, 100));
      const after = url.searchParams.get("after");
      response.end(JSON.stringify({ latest, subtitles: after === null ? [] : subtitles.filter((entry) => entry.sequence > Number(after)) }));
      return;
    }
    response.end(JSON.stringify({ ok: true, browser_audio: true }));
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const extension = path.resolve(__dirname, "..");
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), "overlay-viewer-test-"));
  let context;
  try {
    context = await chromium.launchPersistentContext(profile, {
      executablePath: process.env.CHROMIUM_PATH,
      headless: true, viewport: { width: 1280, height: 1000 },
      args: [`--disable-extensions-except=${extension}`, `--load-extension=${extension}`, "--autoplay-policy=no-user-gesture-required"],
    });
    const worker = context.serviceWorkers()[0] || await context.waitForEvent("serviceworker");
    const extensionId = new URL(worker.url()).host;
    await context.addInitScript(() => {
      if (!location.pathname.endsWith("viewer.html")) return;
      const original = chrome.runtime.sendMessage.bind(chrome.runtime);
      chrome.runtime.sendMessage = (message) => message.type === "getCaptureStream" ? Promise.resolve({ ok: true, streamId: "synthetic" }) : original(message);
      navigator.mediaDevices.getUserMedia = async () => {
        const canvas = document.createElement("canvas");
        canvas.width = 1280; canvas.height = 720;
        const ctx = canvas.getContext("2d");
        setInterval(() => {
          ctx.fillStyle = "#142540"; ctx.fillRect(0, 0, 1280, 720);
          ctx.fillStyle = "#abc4ef"; ctx.font = "48px sans-serif";
          ctx.fillText(`Original stream ${(performance.now() / 1000).toFixed(1)}s`, 80, 330);
        }, 32);
        const stream = canvas.captureStream(30);
        const audio = new AudioContext();
        const oscillator = audio.createOscillator();
        const destination = audio.createMediaStreamDestination();
        oscillator.connect(destination); oscillator.start();
        await audio.resume();
        stream.addTrack(destination.stream.getAudioTracks()[0]);
        return stream;
      };
    });
    await worker.evaluate(async ({ port }) => {
      await chrome.storage.local.set({ pairingCode: `${port}-ABCDEFGHIJKLMNOP` });
    }, { port: server.address().port });
    const page = await context.newPage();
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    const viewerId = await worker.evaluate(async () => {
      const tabs = await chrome.tabs.query({});
      return tabs.at(-1).id;
    });
    await worker.evaluate(async ({ viewerId }) => {
      await chrome.storage.session.set({ captureSession: { session: "synthetic-session", sourceTabId: -1, viewerTabId: viewerId, streamId: "synthetic" } });
      // Reload the worker's in-memory record via a fresh worker after setup isn't needed:
      // the tests seed the same lexical variable in this isolated test profile.
      captureSession = { session: "synthetic-session", sourceTabId: -1, viewerTabId: viewerId, streamId: "synthetic" };
    }, { viewerId });
    await page.goto(`chrome-extension://${extensionId}/viewer/viewer.html`);
    await page.getByRole("button", { name: "시청 시작", exact: true }).click();
    await page.waitForFunction(() => document.querySelector("video").currentTime > 1, { timeout: 20000 });
    await page.waitForFunction(() => document.querySelectorAll("#history li").length >= 5, { timeout: 20000 });
    assert.ok(audioCount >= 50);
    const initial = await page.evaluate(() => ({ time: video.currentTime, width: video.videoWidth, delay: byId("metrics").textContent, text: document.querySelector(".overlay-trans-translation")?.textContent }));
    assert.equal(initial.width, 1280);
    assert.match(initial.delay, /목표 2.0초/);
    assert.equal(initial.text, "동기화 테스트");
    await page.selectOption("#mode", "chat");
    assert.equal(await page.inputValue("#policy"), "off");
    await page.waitForFunction(() => !document.querySelector("#live-video").paused);
    assert.equal(await page.evaluate(() => video.muted && video.paused && liveVideo.srcObject === stream), true);
    await page.selectOption("#policy", "manual");
    await page.waitForFunction(() => livePlayback === false && !video.paused);
    await page.locator("#delay").fill("4");
    await page.locator("#delay").dispatchEvent("change");
    await page.waitForTimeout(300);
    assert.equal(await page.evaluate(() => video.paused), true);
    await page.selectOption("#mode", "watch");
    assert.equal(await page.inputValue("#delay"), "2");
    await page.selectOption("#policy", "auto");
    await page.waitForTimeout(1500);
    if (process.env.VIEWER_LONG_TEST) {
      await page.selectOption("#mode", "chat");
      await page.selectOption("#policy", "off");
      await page.waitForTimeout(25000);
      const buffer = await page.evaluate(() => ({
        start: sourceBuffer.buffered.start(0),
        end: sourceBuffer.buffered.end(sourceBuffer.buffered.length - 1),
      }));
      assert.ok(buffer.start > 0, JSON.stringify(buffer));
      assert.ok(buffer.end - buffer.start < 24, JSON.stringify(buffer));
      await page.selectOption("#mode", "watch");
      await page.waitForFunction(() => !video.paused && video.currentTime > 20);
    }
    const screenshot = process.env.VIEWER_SCREENSHOT;
    if (screenshot) await page.screenshot({ path: screenshot, fullPage: true });
    await page.getByRole("button", { name: "보정 종료", exact: true }).click();
    await page.waitForFunction(() => document.querySelector("#status").textContent.includes("보정을 종료"));
    await page.waitForTimeout(300);
    assert.equal(stopped, true);
    assert.equal(await page.evaluate(() => stream.getTracks().every((track) => track.readyState === "ended")), true);
    assert.deepEqual(errors, []);
    console.log(JSON.stringify({ passed: true, audioChunks: audioCount, media: initial, errors }));
  } finally {
    await context?.close();
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
    // Only remove the absolute temporary profile created by mkdtemp in this test.
    fs.rmSync(profile, { recursive: true, force: true });
  }
})().catch((error) => { console.error(error); process.exitCode = 1; });
