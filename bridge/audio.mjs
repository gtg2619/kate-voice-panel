import {execFile} from 'node:child_process';
import {inspectWav} from './deepseek.mjs';

export function speechSpeed(value = 1) {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0.5 || value > 2)
    throw new Error('Speech speed must be between 0.5× and 2×.');
  return value;
}

// DeepSeek synthesizes the voice; local tempo adjustment keeps its pitch intact.
export async function adjustSpeed(audio, value = 1, signal, env = process.env) {
  const speed = speechSpeed(value);
  signal?.throwIfAborted();
  inspectWav(audio);
  if (speed === 1) return audio;
  const output = await new Promise((resolve, reject) => {
    const child = execFile('ffmpeg', [
      '-hide_banner', '-loglevel', 'error', '-nostdin', '-protocol_whitelist', 'pipe',
      '-f', 'wav', '-i', 'pipe:0', '-af', `atempo=${speed}`, '-c:a', 'pcm_s16le', '-f', 'wav', 'pipe:1',
    ], {
      encoding: 'buffer', maxBuffer: 64 * 1024 * 1024, timeout: 30000, killSignal: 'SIGKILL', signal,
      // This local converter needs neither provider credentials nor proxy configuration.
      env: {PATH: env.PATH ?? process.env.PATH, LANG: 'C'},
    }, (error, stdout) => {
      if (signal?.aborted) reject(signal.reason);
      else if (error?.code === 'ENOENT') reject(new Error('Install FFmpeg for adjustable speech speed, or select 1×.'));
      else if (error) reject(new Error('Speech speed adjustment failed. Check FFmpeg or select 1×.'));
      else resolve(stdout);
    });
    // Spawn/exit failures are reported by execFile; a closed input pipe must not crash the bridge.
    child.stdin.on('error', () => {});
    child.stdin.end(audio);
  });
  signal?.throwIfAborted();
  inspectWav(output);
  return output;
}
