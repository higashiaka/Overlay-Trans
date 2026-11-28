/* global OverlayTransDelay */
const byId = (id) => document.getElementById(id);
const video = byId("video");
const liveVideo = byId("live-video");
const statusLine = byId("status");
const overlay = globalThis.OverlayTrans.overlay;
let settings = OverlayTransDelay.normalize();
let estimator = new OverlayTransDelay.LatencyEstimator(2);
let capture;
let pairing;
let stream;
let audio;
let pcm;
let recorder;
let mediaSource;
let sourceBuffer;
let objectUrl;
let port;
let timer;
let started = false;
let starting = false;
let closed = false;
let recordingStartedAt = 0;
let mediaEpoch = 0;
let sequence = 0;
let livePlayback = null;
let inFlight = false;
const audioQueue = [];
const mediaQueue = [];
const pendingSubtitles = new Map();
const history = [];
const seenFinals = new Set();

function selected() { return settings[settings.mode]; }
function targetDelay() {
  return selected().policy === "off" ? 0 : selected().policy === "auto" ? estimator.seconds : selected().seconds;
}
function renderSettings() {
  byId("mode").value = settings.mode;
  byId("policy").value = selected().policy;
  byId("delay").value = selected().seconds;
  byId("delay").disabled = selected().policy === "off";
  byId("delay-value").textContent = `${selected().seconds.toFixed(1)}초`;
}
async function saveSettings() {
  await chrome.storage.local.set({ viewingSettings: settings });
  renderSettings();
}
byId("mode").addEventListener("change", async (event) => {
  settings.mode = event.target.value;
  estimator.seconds = selected().seconds;
  await saveSettings();
});
byId("policy").addEventListener("change", async (event) => {
  selected().policy = event.target.value;
  estimator.seconds = selected().seconds;
  await saveSettings();
});
byId("delay").addEventListener("input", (event) => {
  selected().seconds = Number(event.target.value);
  estimator.seconds = selected().seconds;
  renderSettings();
});
byId("delay").addEventListener("change", saveSettings);
byId("show-source").addEventListener("change", async (event) => {
  overlay.setShowSource(event.target.checked);
  await chrome.storage.local.set({ showSource: event.target.checked });
});
byId("source").addEventListener("click", () => capture && chrome.tabs.update(capture.sourceTabId, { active: true }));
byId("fullscreen").addEventListener("click", () => byId("player").requestFullscreen());
byId("stop").addEventListener("click", () => shutdown("보정을 종료했습니다. 원본 탭에서 계속 시청하세요."));
byId("reset-history").addEventListener("click", () => {
  history.length = 0;
  seenFinals.clear();
  estimator = new OverlayTransDelay.LatencyEstimator(selected().seconds);
  byId("history").replaceChildren();
});

async function api(path, body, binary = false, keepalive = false) {
  const response = await fetch(`http://127.0.0.1:${pairing.port}${path}`, {
    method: "POST",
    headers: { Authorization: `Bearer ${pairing.key}`, "Content-Type": binary ? "application/octet-stream" : "application/json" },
    body: binary ? body : JSON.stringify(body),
    signal: keepalive ? undefined : AbortSignal.timeout(3000),
    keepalive,
  });
  if (!response.ok) throw new Error(`앱 음성 연결 실패 (${response.status}). 앱이 준비되어 있는지 확인하고 다시 시작하세요.`);
}

async function sendAudio() {
  if (inFlight || closed) return;
  inFlight = true;
  try {
    while (audioQueue.length && !closed) {
      const chunk = audioQueue.shift();
      await api(`/v1/audio?session=${capture.session}&sequence=${sequence++}&ended_at_ms=${Math.round(chunk.endedAt)}`, chunk.samples, true);
    }
  } catch (error) { await shutdown(error.message); }
  finally { inFlight = false; }
}

function appendMedia() {
  if (closed || !sourceBuffer || sourceBuffer.updating) return;
  try {
    // Keep enough encoded media for the maximum delay, while bounding long sessions.
    const reference = livePlayback && mediaEpoch ? (Date.now() - mediaEpoch) / 1000 : video.currentTime;
    if (reference > 20 && sourceBuffer.buffered.length && sourceBuffer.buffered.start(0) < reference - 20) {
      sourceBuffer.remove(0, reference - 15);
    } else if (mediaQueue.length) {
      sourceBuffer.appendBuffer(mediaQueue.shift());
    }
  } catch (error) { shutdown(`시청 버퍼를 유지할 수 없습니다: ${error.message}`); }
}

function recordLatency(message) {
  if (!message.final || !message.speech_ended_at_ms || seenFinals.has(message.speech_ended_at_ms)) return;
  seenFinals.add(message.speech_ended_at_ms);
  if (seenFinals.size > 200) seenFinals.delete(seenFinals.values().next().value);
  // Include HTTP and browser delivery, not only inference time in the native app.
  const milliseconds = Date.now() - message.speech_ended_at_ms;
  if (!estimator.add(milliseconds)) return;
  history.unshift({ at: Date.now(), milliseconds });
  if (history.length > 30) history.pop();
  byId("history").replaceChildren(...history.map((entry) => {
    const item = document.createElement("li");
    item.textContent = `${new Date(entry.at).toLocaleTimeString()} · ${(entry.milliseconds / 1000).toFixed(2)}초`;
    return item;
  }));
}

function receive(message) {
  if (message.type === "captureState" && !message.active && stream && !closed) {
    shutdown("원본 탭 연결이 종료되었습니다.");
    return;
  }
  if (message.type !== "subtitle" || message.audio_session !== capture.session || closed) return;
  recordLatency(message);
  // A partial and its final replacement have the same source time. Replace queued text too.
  const previous = pendingSubtitles.get(message.replaces);
  if (previous?.speech_ended_at_ms === message.speech_ended_at_ms) pendingSubtitles.delete(message.replaces);
  pendingSubtitles.set(message.sequence, message);
  if (pendingSubtitles.size > 200) pendingSubtitles.delete(pendingSubtitles.keys().next().value);
}

function tick() {
  if (closed || !stream) return;
  const live = selected().policy === "off";
  if (livePlayback !== live) {
    livePlayback = live;
    video.hidden = live;
    liveVideo.hidden = !live;
    if (live) {
      video.pause();
      video.muted = true;
      liveVideo.muted = false;
      liveVideo.play().catch(() => { statusLine.textContent = "재생·소리 켜기를 눌러 주세요."; });
    } else {
      liveVideo.pause();
      liveVideo.muted = true;
      video.muted = false;
      // Explicit mode changes may reposition once; automatic correction never seeks.
      if (mediaEpoch && sourceBuffer?.buffered.length) {
        const end = sourceBuffer.buffered.end(sourceBuffer.buffered.length - 1);
        video.currentTime = Math.max(sourceBuffer.buffered.start(0), Math.min(end - 0.05, (Date.now() - mediaEpoch) / 1000 - targetDelay()));
      }
    }
    overlay.detach();
    overlay.attach(byId("player"));
  }
  if (live) {
    if (!mediaEpoch && sourceBuffer?.buffered.length) mediaEpoch = recordingStartedAt - sourceBuffer.buffered.start(0) * 1000;
    statusLine.textContent = settings.mode === "chat" ? "채팅 모드 · 실시간 재생" : "시청 모드 · 보정 끔";
    byId("metrics").textContent = `보정 끔 · 실시간 캡처 · 최근 평균 ${estimator.average === null ? "측정 중" : `${estimator.average.toFixed(2)}초`} (${estimator.history.length}건)`;
    if (!liveVideo.paused) showDueSubtitles(Date.now());
    return;
  }
  if (!sourceBuffer?.buffered.length) return;
  if (!mediaEpoch) mediaEpoch = recordingStartedAt - sourceBuffer.buffered.start(0) * 1000;
  const liveTime = (Date.now() - mediaEpoch) / 1000;
  const action = OverlayTransDelay.playbackAction(liveTime, video.currentTime, targetDelay(), started);
  if (action.pause) {
    video.pause();
    statusLine.textContent = "설정한 딜레이만큼 버퍼를 쌓는 중…";
  } else {
    video.playbackRate = action.rate;
    if (video.paused) video.play().catch(() => { statusLine.textContent = "재생·소리 켜기를 눌러 주세요."; });
    if (!video.paused) { started = true; statusLine.textContent = settings.mode === "watch" ? "시청 모드" : "채팅 모드 · 원본 탭에서 채팅할 수 있습니다."; }
  }
  const actual = Math.max(0, liveTime - video.currentTime);
  const average = estimator.average;
  byId("metrics").textContent = `목표 ${targetDelay().toFixed(1)}초 · 실제 ${actual.toFixed(1)}초 · 최근 평균 ${average === null ? "측정 중" : `${average.toFixed(2)}초`} (${estimator.history.length}건)${selected().policy === "auto" && estimator.history.length < 5 ? " · 5건부터 자동 조정" : ""}${targetDelay() >= 10 ? " · 보정 상한 10초" : ""}`;
  if (video.paused) return;
  showDueSubtitles(mediaEpoch + video.currentTime * 1000);
}

function showDueSubtitles(displayedAt) {
  for (const [key, subtitle] of pendingSubtitles) {
    if (subtitle.speech_ended_at_ms <= displayedAt) {
      overlay.show(subtitle);
      pendingSubtitles.delete(key);
    }
  }
}

async function start() {
  if (starting || stream || closed || !capture) return;
  starting = true;
  byId("resume").disabled = true;
  // Resume during the button gesture, before any capture/network awaits.
  audio = new AudioContext({ sampleRate: 16000 });
  try {
    await audio.resume();
    // Stream IDs expire quickly. Renew at the explicit start gesture instead of using the popup ID.
    const fresh = await chrome.runtime.sendMessage({ type: "getCaptureStream" });
    if (!fresh?.ok) throw new Error(fresh?.error ?? "원본 탭을 캡처하지 못했습니다.");
    capture.streamId = fresh.streamId;
    await api("/v1/audio/start", { session: capture.session });
    stream = await navigator.mediaDevices.getUserMedia({
      audio: { mandatory: { chromeMediaSource: "tab", chromeMediaSourceId: capture.streamId } },
      video: { mandatory: { chromeMediaSource: "tab", chromeMediaSourceId: capture.streamId, maxWidth: 1280, maxHeight: 720, maxFrameRate: 30 } },
    });
    if (closed) { stream.getTracks().forEach((track) => track.stop()); return; }
    liveVideo.srcObject = stream;
    for (const track of stream.getTracks()) track.addEventListener("ended", () => shutdown("탭 캡처가 종료되었습니다."));
    const mimeType = 'video/webm;codecs=vp8,opus';
    if (!MediaRecorder.isTypeSupported(mimeType) || !MediaSource.isTypeSupported(mimeType)) throw new Error("이 브라우저는 지연 시청에 필요한 WebM 재생을 지원하지 않습니다.");
    mediaSource = new MediaSource();
    objectUrl = URL.createObjectURL(mediaSource);
    const opened = new Promise((resolve) => mediaSource.addEventListener("sourceopen", resolve, { once: true }));
    video.src = objectUrl;
    await opened;
    sourceBuffer = mediaSource.addSourceBuffer(mimeType);
    sourceBuffer.addEventListener("updateend", appendMedia);
    sourceBuffer.addEventListener("error", () => shutdown("캡처한 영상을 재생할 수 없습니다."));
    recorder = new MediaRecorder(stream, { mimeType, videoBitsPerSecond: 2500000, audioBitsPerSecond: 128000 });
    let mediaChain = Promise.resolve();
    recorder.addEventListener("dataavailable", (event) => {
      if (!event.data.size || closed) return;
      mediaChain = mediaChain.then(async () => {
        const data = await event.data.arrayBuffer();
        if (closed) return;
        mediaQueue.push(data);
        if (mediaQueue.length > 100) throw new Error("시청 버퍼 처리가 지연되었습니다.");
        appendMedia();
      }).catch((error) => shutdown(error.message));
    });
    recorder.addEventListener("error", () => shutdown("탭 녹화에 실패했습니다."));
    await audio.audioWorklet.addModule("pcm-worklet.js");
    if (audio.sampleRate !== 16000) throw new Error("16kHz 음성 입력을 초기화하지 못했습니다.");
    pcm = new AudioWorkletNode(audio, "pcm-sender");
    const audioEpoch = Date.now() - audio.currentTime * 1000;
    pcm.port.onmessage = ({ data }) => {
      if (closed) return;
      audioQueue.push({ samples: data.samples, endedAt: audioEpoch + data.endTime * 1000 });
      if (audioQueue.length > 20) { shutdown("원본 음성을 앱으로 보내는 속도가 느립니다. 앱 연결을 확인하세요."); return; }
      sendAudio();
    };
    const source = audio.createMediaStreamSource(stream);
    source.connect(pcm);
    // Processor output is silence: the original is never sent to speakers/WASAPI.
    pcm.connect(audio.destination);
    recordingStartedAt = Date.now();
    recorder.start(100);
    overlay.attach(byId("player"));
    port = chrome.runtime.connect({ name: "subtitles-viewer" });
    port.onMessage.addListener(receive);
    port.onDisconnect.addListener(() => shutdown("확장 프로그램 연결이 끊겼습니다. 시청 창을 다시 열어 주세요."));
    timer = setInterval(tick, 100);
    statusLine.textContent = "영상 버퍼를 준비하는 중…";
    byId("resume").textContent = "재생·소리 켜기";
  } catch (error) { await shutdown(error.message); }
  finally { starting = false; byId("resume").disabled = closed; }
}

async function shutdown(message) {
  if (closed) return;
  closed = true;
  clearInterval(timer);
  video.pause();
  liveVideo.pause();
  liveVideo.srcObject = null;
  if (recorder?.state !== "inactive" && recorder) recorder.stop();
  stream?.getTracks().forEach((track) => track.stop());
  pcm?.disconnect();
  audio?.close().catch(() => {});
  video.removeAttribute("src");
  video.load();
  if (objectUrl) URL.revokeObjectURL(objectUrl);
  overlay.detach();
  pendingSubtitles.clear();
  audioQueue.length = mediaQueue.length = 0;
  statusLine.textContent = message;
  byId("resume").disabled = true;
  byId("stop").disabled = true;
  await chrome.runtime.sendMessage({ type: "stopViewing" }).catch(() => {});
}
byId("resume").addEventListener("click", async () => {
  if (!stream) { await start(); return; }
  await audio?.resume();
  const active = livePlayback ? liveVideo : video;
  active.muted = false;
  active.play().catch((error) => { statusLine.textContent = error.message; });
});
window.addEventListener("pagehide", () => {
  stream?.getTracks().forEach((track) => track.stop());
  if (capture && pairing) api("/v1/audio/stop", { session: capture.session }, false, true).catch(() => {});
});

(async () => {
  const saved = await chrome.storage.local.get({ viewingSettings: null, pairingCode: "", showSource: true });
  settings = OverlayTransDelay.normalize(saved.viewingSettings);
  estimator = new OverlayTransDelay.LatencyEstimator(selected().seconds);
  renderSettings();
  overlay.setShowSource(saved.showSource);
  byId("show-source").checked = saved.showSource;
  const match = /^(\d{2,5})-([0-9A-Z]{16})$/.exec(saved.pairingCode);
  if (!match) { statusLine.textContent = "먼저 앱을 연결하고 방송 탭에서 시청 창을 열어 주세요."; return; }
  pairing = { port: Number(match[1]), key: match[2] };
  // The new tab may load before getMediaStreamId finishes in the worker.
  for (let attempt = 0; attempt < 50; attempt++) {
    capture = (await chrome.runtime.sendMessage({ type: "getCaptureSession" }))?.session;
    if (capture) break;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  if (!capture) { statusLine.textContent = "방송 탭에서 확장 프로그램의 시청 창 열기를 눌러 주세요."; return; }
  statusLine.textContent = "시청 시작을 누르면 원본 음성을 분리하고 보정 재생을 시작합니다.";
  byId("resume").textContent = "시청 시작";
})().catch((error) => shutdown(error.message));
