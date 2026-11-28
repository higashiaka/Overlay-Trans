// The AudioContext runs at 16 kHz; Chrome resamples the captured source before this processor.
class PcmSender extends AudioWorkletProcessor {
  constructor() { super(); this.samples = new Float32Array(1600); this.offset = 0; }
  process(inputs) {
    const channels = inputs[0];
    const length = channels[0]?.length ?? 128;
    for (let i = 0; i < length; i++) {
      let sample = 0;
      for (const channel of channels) sample += channel[i] ?? 0;
      this.samples[this.offset++] = Math.max(-1, Math.min(1, channels.length ? sample / channels.length : 0));
      if (this.offset === this.samples.length) {
        this.port.postMessage({ samples: this.samples.buffer, endTime: (currentFrame + i + 1) / sampleRate }, [this.samples.buffer]);
        this.samples = new Float32Array(1600);
        this.offset = 0;
      }
    }
    return true;
  }
}
registerProcessor("pcm-sender", PcmSender);
