// Shared pure policy: playback, settings normalization and robust gradual latency estimation.
globalThis.OverlayTransDelay = (() => {
  const clamp = (value, low, high) => Math.min(high, Math.max(low, value));
  const defaults = () => ({ mode: "watch", watch: { policy: "manual", seconds: 2 }, chat: { policy: "off", seconds: 2 } });
  function normalize(value) {
    const result = defaults();
    if (value?.mode === "chat") result.mode = "chat";
    for (const mode of ["watch", "chat"]) {
      if (["off", "manual", "auto"].includes(value?.[mode]?.policy)) result[mode].policy = value[mode].policy;
      if (Number.isFinite(value?.[mode]?.seconds)) result[mode].seconds = clamp(value[mode].seconds, 0, 10);
    }
    return result;
  }
  class LatencyEstimator {
    constructor(initial = 2) { this.seconds = initial; this.history = []; }
    add(milliseconds) {
      if (!Number.isFinite(milliseconds) || milliseconds < 0 || milliseconds > 60000) return false;
      this.history.push(milliseconds / 1000);
      if (this.history.length > 30) this.history.shift();
      if (this.history.length < 5) return true;
      const sorted = [...this.history].sort((a, b) => a - b);
      const median = sorted[Math.floor(sorted.length / 2)];
      const filtered = sorted.filter((value) => Math.abs(value - median) <= Math.max(0.5, median * 0.5));
      const mean = filtered.reduce((sum, value) => sum + value, 0) / filtered.length;
      const target = clamp(mean + 0.3, 0, 10);
      this.seconds = clamp(this.seconds + clamp((target - this.seconds) * 0.15, -0.1, 0.1), 0, 10);
      return true;
    }
    get average() {
      return this.history.length ? this.history.reduce((sum, value) => sum + value, 0) / this.history.length : null;
    }
  }
  function playbackAction(liveTime, currentTime, delay, started) {
    const behind = liveTime - currentTime;
    if (!started && liveTime < delay + 0.15) return { pause: true, rate: 1 };
    if (behind < delay - 0.12) return { pause: true, rate: 1 };
    return { pause: false, rate: behind > delay + 0.2 ? 1.05 : 1 };
  }
  return { normalize, LatencyEstimator, playbackAction };
})();
