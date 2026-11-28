const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const sandbox = {};
vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../viewer/delay.js"), "utf8"), sandbox);
const { normalize, LatencyEstimator, playbackAction } = sandbox.OverlayTransDelay;

test("watch and chat keep separate policies and clamp persisted slider values", () => {
  const settings = normalize({ mode: "chat", watch: { policy: "auto", seconds: 99 }, chat: { policy: "invalid", seconds: -1 } });
  assert.equal(settings.mode, "chat");
  assert.equal(settings.watch.policy, "auto");
  assert.equal(settings.watch.seconds, 10);
  assert.equal(settings.chat.policy, "off");
  assert.equal(settings.chat.seconds, 0);
  assert.equal(normalize().watch.seconds, 2);
  assert.equal(normalize({ watch: { seconds: NaN } }).watch.seconds, 2);
});

test("automatic correction warms up, rejects invalid records, excludes spikes and moves gradually", () => {
  const estimator = new LatencyEstimator(2);
  for (const value of [-1, NaN, Infinity, 61000]) assert.equal(estimator.add(value), false);
  for (let i = 0; i < 4; i++) estimator.add(2000);
  assert.equal(estimator.seconds, 2);
  estimator.add(2000);
  assert.ok(estimator.seconds > 2 && estimator.seconds < 2.1);
  const previous = estimator.seconds;
  estimator.add(45000);
  assert.ok(estimator.seconds - previous <= 0.1);
  assert.ok(estimator.seconds < 2.2);
  for (let i = 0; i < 200; i++) estimator.add(3000);
  assert.equal(estimator.history.length, 30);
  assert.ok(Math.abs(estimator.seconds - 3.3) < 0.01);
});

test("playback buffers increases and catches up decreases without seeking", () => {
  assert.equal(playbackAction(1, 0, 2, false).pause, true);
  assert.equal(playbackAction(3, 0, 2, false).pause, false);
  assert.equal(playbackAction(30, 28, 3, true).pause, true);
  assert.equal(playbackAction(30, 28, 2, true).rate, 1);
  assert.equal(playbackAction(30, 25, 2, true).rate, 1.05);
  assert.equal(playbackAction(30, 29.8, 0, true).pause, false);
});

test("PCM worklet averages stereo into bounded 16 kHz chunks with sample-clock timestamps", () => {
  const chunks = [];
  let Processor;
  const context = {
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage: (chunk) => chunks.push(chunk) }; } },
    registerProcessor: (_name, value) => { Processor = value; },
    sampleRate: 16000, currentFrame: 0,
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../viewer/pcm-worklet.js"), "utf8"), context);
  const processor = new Processor();
  for (let i = 0; i < 25; i++) {
    context.currentFrame = i * 128;
    processor.process([[new Float32Array(128).fill(0.75), new Float32Array(128).fill(0.25)]]);
  }
  assert.equal(chunks.length, 2);
  assert.equal(chunks[0].samples.byteLength, 6400);
  assert.equal(new Float32Array(chunks[0].samples)[0], 0.5);
  assert.equal(chunks[0].endTime, 0.1);
  assert.equal(chunks[1].endTime, 0.2);
});
