// DeepSeek's speech and web APIs. No third-party fallback or SDK dependency.
import {readFile} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';

export async function credentials(env = process.env) {
  const result = {...env};
  let contents = '';
  try { contents = await readFile(join(env.DSH_HOME || join(homedir(), '.dsh'), '.env'), 'utf8'); }
  catch (error) { if (error.code !== 'ENOENT') throw new Error('Cannot read the DSH environment file.'); }
  for (const line of contents.split(/\r?\n/)) {
    const match = /^(?:export\s+)?(DEEPSEEK_API_KEY|HTTPS_PROXY|HTTP_PROXY|ALL_PROXY|NO_PROXY|https_proxy|http_proxy|all_proxy|no_proxy)\s*=\s*(.*?)\s*$/.exec(line.trim());
    if (!match || result[match[1]] !== undefined) continue;
    let value = match[2];
    if (/^(['"]).*\1$/.test(value)) value = value.slice(1, -1);
    else value = value.replace(/\s+#.*$/, '');
    result[match[1]] = value;
  }
  for (const name of ['HTTP_PROXY', 'HTTPS_PROXY', 'NO_PROXY'])
    result[name] ??= result[name.toLowerCase()];
  const proxy = result.ALL_PROXY || result.all_proxy;
  if (proxy && /^https?:\/\//i.test(proxy)) {
    result.HTTPS_PROXY ??= proxy; result.HTTP_PROXY ??= proxy;
  }
  return result;
}

export function safeError(error, key = '') {
  let text = String(error?.message || error || 'Unknown error');
  if (key) text = text.split(key).join('[redacted]');
  return text.replace(/\bsk-[\w-]+/g, '[redacted]').replace(/Bearer\s+\S+/gi, 'Bearer [redacted]').slice(0, 1200);
}

export function inspectWav(buffer, recording = false) {
  if (buffer.length < 44 || buffer.toString('ascii', 0, 4) !== 'RIFF' || buffer.toString('ascii', 8, 12) !== 'WAVE')
    throw new Error('Expected a PCM WAV recording.');
  let format, dataLength;
  for (let offset = 12; offset + 8 <= buffer.length;) {
    const type = buffer.toString('ascii', offset, offset + 4);
    let size = buffer.readUInt32LE(offset + 4);
    // Streaming WAV responses may leave RIFF/data sizes unspecified.
    if (type === 'data' && size === 0xffffffff) size = buffer.length - offset - 8;
    if (offset + 8 + size > buffer.length) throw new Error('Incomplete WAV audio.');
    if (type === 'fmt ' && size >= 16) format = {
      codec: buffer.readUInt16LE(offset + 8), channels: buffer.readUInt16LE(offset + 10),
      rate: buffer.readUInt32LE(offset + 12), bits: buffer.readUInt16LE(offset + 22),
    };
    if (type === 'data') dataLength = size;
    offset += 8 + size + size % 2;
  }
  if (!format || format.codec !== 1 || format.bits !== 16 || ![1, 2].includes(format.channels) || !dataLength)
    throw new Error('Audio must contain 16-bit PCM samples.');
  const duration = dataLength / (format.rate * format.channels * 2);
  if (!Number.isFinite(duration) || duration <= 0) throw new Error('The recording is empty.');
  if (recording && (!new Set([8000, 16000, 22050, 24000, 32000, 44100, 48000]).has(format.rate)
      || duration > 120 || buffer.length > 8 * 1024 * 1024))
    throw new Error('Recordings must be at most 120 seconds and 8 MiB at a supported sample rate.');
  return {...format, duration};
}

export function speechText(text) {
  // Keep the spoken answer short and avoid reading code blocks, URLs and math aloud.
  return text.replace(/```[\s\S]*?(?:```|$)/g, ' Code is available in the transcript. ')
    .replace(/\[([^\]]+)\]\([^)]*\)/g, '$1').replace(/https?:\/\/\S+/g, '')
    .replace(/\$\$[\s\S]*?\$\$|\\\[[\s\S]*?\\\]/g, ' Formula in the transcript. ')
    .replace(/^[\s]*#{1,6}\s+/gm, '').replace(/[*`_]/g, '').trim().slice(0, 9000);
}

export class DeepSeek {
  constructor(key, {base = 'https://api.deepseek.com/v1/tools', fetcher = fetch} = {}) {
    if (!key?.trim()) throw new Error('Set DEEPSEEK_API_KEY in the environment or ~/.dsh/.env.');
    this.key = key.trim(); this.base = base; this.fetcher = fetcher;
  }
  async request(route, body, signal, multipart = false, binary = false) {
    const timeout = AbortSignal.timeout(90000);
    const response = await this.fetcher(`${this.base}/${route}`, {
      method: 'POST', headers: {Authorization: `Bearer ${this.key}`, ...(!multipart && {'Content-Type': 'application/json'})},
      body: multipart ? body : JSON.stringify(body), redirect: 'error',
      signal: signal ? AbortSignal.any([signal, timeout]) : timeout,
    });
    if (!response.ok) {
      // Do not forward response bodies: gateways can echo credentials or uploaded input.
      await response.body?.cancel();
      const hint = {401: 'Check DEEPSEEK_API_KEY.', 402: 'Check the DeepSeek account balance.',
        403: 'The account cannot access this service.', 413: 'The audio exceeds the upload limit.',
        429: 'Rate limit reached; try again later.', 503: 'The service is busy; try again later.'}[response.status] || 'Please try again.';
      throw new Error(`DeepSeek ${route}: HTTP ${response.status}. ${hint}`);
    }
    const limit = binary ? 32 * 1024 * 1024 : 2 * 1024 * 1024;
    const chunks = []; let length = 0;
    for await (const chunk of response.body) {
      length += chunk.length;
      if (length > limit) throw new Error(`DeepSeek ${route}: response exceeds the size limit.`);
      chunks.push(chunk);
    }
    const data = Buffer.concat(chunks);
    if (binary) return data;
    try { return JSON.parse(data.toString('utf8')); }
    catch { throw new Error(`DeepSeek ${route}: invalid JSON response.`); }
  }
  async transcribe(buffer, signal) {
    inspectWav(buffer, true);
    const form = new FormData(); form.append('file', new Blob([buffer], {type: 'audio/wav'}), 'recording.wav');
    const data = await this.request('asr', form, signal, true);
    if (typeof data.text !== 'string' || !data.text.trim()) throw new Error('No speech was recognized. Please record again.');
    return data.text.trim();
  }
  async speak(text, voice, signal) {
    if (!['stella', 'tide'].includes(voice)) throw new Error('Choose Stella or Tide.');
    const spoken = speechText(text);
    if (!spoken) return null;
    const audio = await this.request('tts', {text: spoken, voice_id: voice, format: 'wav', filter_markdown: true}, signal, false, true);
    inspectWav(audio); return audio;
  }
  async search(query, signal) {
    if (typeof query !== 'string' || !query.trim() || [...query].length > 128) throw new Error('Search queries must contain 1–128 characters.');
    const data = await this.request('search', {query, count: 5, need_content: false}, signal);
    if (!Array.isArray(data.results)) throw new Error('DeepSeek search returned no result list.');
    return JSON.stringify(data.results.map(({title, url, summary, snippet}) => ({title, url, text: summary || snippet}))).slice(0, 40000);
  }
  async page(url, signal) {
    const parsed = new URL(url);
    if (!['https:', 'http:'].includes(parsed.protocol) || parsed.username || parsed.password || url.length > 8192)
      throw new Error('Web pages require an HTTP(S) URL without credentials.');
    const data = await this.request('fetch', {url}, signal);
    if (typeof data.content !== 'string') throw new Error('DeepSeek fetch returned no page content.');
    return `${data.title || url}\n${data.content}`.slice(0, 60000);
  }
}
