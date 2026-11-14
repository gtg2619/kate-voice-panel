import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, writeFile, readFile, symlink, rm, stat} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {DeepSeek, credentials, inspectWav, safeError, speechText} from '../bridge/deepseek.mjs';
import {Workspace} from '../bridge/readonly.mjs';
import {VoiceBridge} from '../bridge/main.mjs';
import {Dsh} from '../bridge/dsh.mjs';
import {createProfile, SYSTEM_PROMPT} from '../bridge/profile.mjs';
import {apply as registerTools} from '../bridge/tools.mjs';
import {adjustSpeed} from '../bridge/audio.mjs';

function wav(seconds = .1, rate = 16000) {
  const data = Buffer.alloc(Math.round(seconds * rate) * 2 + 44);
  data.write('RIFF'); data.writeUInt32LE(data.length - 8, 4); data.write('WAVEfmt ', 8);
  data.writeUInt32LE(16, 16); data.writeUInt16LE(1, 20); data.writeUInt16LE(1, 22);
  data.writeUInt32LE(rate, 24); data.writeUInt32LE(rate * 2, 28); data.writeUInt16LE(2, 32);
  data.writeUInt16LE(16, 34); data.write('data', 36); data.writeUInt32LE(data.length - 44, 40); return data;
}
async function directory(t) {
  const dir = await mkdtemp(join(tmpdir(), 'kate-voice-test-')); t.after(() => rm(dir, {recursive: true, force: true})); return dir;
}
const fake = fileURLToPath(new URL('./fake_dsh.mjs', import.meta.url));
const json = body => new Response(JSON.stringify(body), {headers: {'Content-Type': 'application/json'}});

test('credentials read DSH .env without executing shell content; environment wins', async t => {
  const dir = await directory(t);
  await writeFile(join(dir, '.env'), 'DEEPSEEK_API_KEY="file-key"\nHTTPS_PROXY=http://127.0.0.1:9999\nUNRELATED=secret\nexport NO_PROXY=localhost # comment\n');
  const env = await credentials({DSH_HOME: dir, DEEPSEEK_API_KEY: 'environment-key'});
  assert.equal(env.DEEPSEEK_API_KEY, 'environment-key'); assert.equal(env.UNRELATED, undefined);
  assert.equal(env.NO_PROXY, 'localhost'); assert.equal(env.HTTPS_PROXY, 'http://127.0.0.1:9999');
});
test('HTTP all_proxy is usable for both DeepSeek service routes', async t => {
  const env = await credentials({DSH_HOME: await directory(t), all_proxy: 'http://localhost:8080'});
  assert.equal(env.HTTP_PROXY, env.all_proxy); assert.equal(env.HTTPS_PROXY, env.all_proxy);
});
test('WAV validation rejects empty, truncated, binary and oversized audio', () => {
  assert.equal(inspectWav(wav()).duration, .1);
  for (const buffer of [Buffer.from('not wave'), wav().subarray(0, 45), wav(0), wav(121)])
    assert.throws(() => inspectWav(buffer, true));
  const floating = wav(); floating.writeUInt16LE(3, 20); assert.throws(() => inspectWav(floating));
  const unsupported = wav(.1, 96000); assert.throws(() => inspectWav(unsupported, true));
});
test('streaming WAV placeholder size is accepted without accepting truncated chunks', () => {
  const streaming = wav(); streaming.writeUInt32LE(0xffffffff, 4); streaming.writeUInt32LE(0xffffffff, 40);
  assert.equal(inspectWav(streaming).duration, .1);
});
test('ASR uploads a WAV multipart file with bearer auth and no redirect', async () => {
  const api = new DeepSeek('test-key', {fetcher: async (url, options) => {
    assert.equal(url, 'https://api.deepseek.com/v1/tools/asr');
    assert.equal(options.headers.Authorization, 'Bearer test-key'); assert.equal(options.redirect, 'error');
    const file = options.body.get('file'); assert.equal(file.type, 'audio/wav'); assert.deepEqual(Buffer.from(await file.arrayBuffer()), wav());
    return json({text: '  What does this do?  ', duration_ms: 100});
  }});
  assert.equal(await api.transcribe(wav()), 'What does this do?');
});
test('TTS sends supported voice and format; only final prose is spoken', async () => {
  const api = new DeepSeek('test-key', {fetcher: async (url, options) => {
    assert.ok(url.endsWith('/tts')); const body = JSON.parse(options.body);
    assert.equal(body.voice_id, 'tide'); assert.equal(body.format, 'wav'); assert.equal(body.filter_markdown, true);
    assert.ok(!('speed' in body)); assert.doesNotMatch(body.text, /https:|```|rm -rf/);
    return new Response(wav());
  }});
  assert.ok(await api.speak('Read [source](https://example.com).\n```bash\nrm -rf /\n```', 'tide'));
  await assert.rejects(api.speak('hi', 'unknown'), /Stella or Tide/);
});
test('normal speed needs no FFmpeg and invalid speeds fail before running a converter', async () => {
  const audio = wav();
  assert.equal(await adjustSpeed(audio, 1, undefined, {PATH: '/nonexistent'}), audio);
  for (const speed of [0, 0.4, 2.1, NaN, Infinity, '1.5', null])
    await assert.rejects(adjustSpeed(audio, speed), /between 0.5× and 2×/);
  await assert.rejects(adjustSpeed(audio, 1.5, undefined, {PATH: '/nonexistent'}), /Install FFmpeg/);
});
test('real FFmpeg changes duration without shifting the tone or sample rate', async () => {
  const audio = wav(2, 24000);
  for (let i = 0; i < 48000; ++i) audio.writeInt16LE(Math.round(12000 * Math.sin(2 * Math.PI * 440 * i / 24000)), 44 + 2 * i);
  for (const speed of [0.5, 1.5, 2]) {
    const result = await adjustSpeed(audio, speed);
    const info = inspectWav(result);
    assert.equal(info.rate, 24000); assert.equal(info.channels, 1);
    assert.ok(Math.abs(info.duration - 2 / speed) < 0.1, `Speed ${speed}: duration ${info.duration}`);
    let offset = 12;
    while (result.toString('ascii', offset, offset + 4) !== 'data') {
      const size = result.readUInt32LE(offset + 4); offset += 8 + size + size % 2;
    }
    const pcm = result.subarray(offset + 8); let crossings = 0;
    // Exclude the filter's leading/trailing overlap from the frequency estimate.
    const start = 2400, end = pcm.length / 2 - 2400;
    for (let i = start + 1; i < end; ++i)
      if ((pcm.readInt16LE(i * 2) >= 0) !== (pcm.readInt16LE((i - 1) * 2) >= 0)) crossings++;
    const frequency = crossings * info.rate / (2 * (end - start));
    assert.ok(Math.abs(frequency - 440) < 5, `Speed ${speed}: frequency ${frequency}`);
  }
});
test('cancelling speed adjustment kills the converter and strips credentials', async t => {
  const dir = await directory(t), marker = join(dir, 'started.json');
  await writeFile(join(dir, 'ffmpeg'), `#!${process.execPath}\nrequire('fs').writeFileSync(${JSON.stringify(marker)}, JSON.stringify({pid:process.pid, key:process.env.DEEPSEEK_API_KEY, proxy:process.env.HTTPS_PROXY}));setInterval(()=>{},1000);`, {mode: 0o755});
  const controller = new AbortController();
  const result = adjustSpeed(wav(), 1.5, controller.signal, {PATH: dir, DEEPSEEK_API_KEY: 'private', HTTPS_PROXY: 'private'});
  const rejected = assert.rejects(result, {name: 'AbortError'});
  let started;
  for (let i = 0; i < 200 && !started; ++i) {
    try { started = JSON.parse(await readFile(marker, 'utf8')); } catch { await new Promise(resolve => setTimeout(resolve, 10)); }
  }
  controller.abort(); await rejected;
  assert.ok(started, 'converter started'); assert.equal(started.key, undefined); assert.equal(started.proxy, undefined);
  for (let i = 0; i < 100; ++i) {
    try { process.kill(started.pid, 0); } catch (error) { assert.equal(error.code, 'ESRCH'); return; }
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  assert.fail('converter survived cancellation');
});
test('web tools use DeepSeek endpoints with bounded queries and credential-free URLs', async () => {
  const calls = [];
  const api = new DeepSeek('test-key', {fetcher: async (url, options) => {
    calls.push([url, JSON.parse(options.body)]);
    return url.endsWith('search') ? json({results: [{title: 'Kate', url: 'https://kate-editor.org', summary: 'An editor'}]}) : json({title: 'Kate', content: 'Page text'});
  }});
  assert.match(await api.search('Kate'), /An editor/); assert.match(await api.page('https://kate-editor.org/'), /Page text/);
  assert.equal(calls[0][1].count, 5); assert.equal(calls[0][1].need_content, false);
  await assert.rejects(api.search('x'.repeat(129))); await assert.rejects(api.page('file:///etc/passwd'));
  await assert.rejects(api.page('https://user:password@example.org/'));
  assert.equal(calls.length, 2);
});
test('HTTP error bodies and keys never appear in a user-facing error', async () => {
  for (const status of [401, 402, 403, 429, 503]) {
    const api = new DeepSeek('test-key', {fetcher: async () => new Response('test-key and private input', {status})});
    await assert.rejects(api.search('Kate'), error => error.message.includes(`HTTP ${status}`) && !error.message.includes('test-key'));
  }
  assert.equal(safeError(new Error('Oops sk-private Bearer another-secret')), 'Oops [redacted] Bearer [redacted]');
});
test('malformed, empty and oversized API responses fail clearly', async () => {
  await assert.rejects(new DeepSeek('k', {fetcher: async () => new Response('bad')}).search('x'), /invalid JSON/);
  await assert.rejects(new DeepSeek('k', {fetcher: async () => json({text: ''})}).transcribe(wav()), /No speech/);
  await assert.rejects(new DeepSeek('k', {fetcher: async () => json({})}).search('x'), /no result/);
  await assert.rejects(new DeepSeek('k', {fetcher: async () => new Response('a'.repeat(3 * 1024 * 1024))}).search('x'), /size limit/);
});
test('request cancellation reaches the HTTP transport', async () => {
  const controller = new AbortController(); controller.abort();
  const api = new DeepSeek('key', {fetcher: async (_, options) => { options.signal.throwIfAborted(); }});
  await assert.rejects(api.search('test', controller.signal), {name: 'AbortError'});
});
test('workspace reading is bounded and rejects path traversal, symlinks and binaries', async t => {
  const dir = await directory(t), outside = await directory(t);
  await writeFile(join(dir, 'code.txt'), 'one\ntwo\nthree'); await writeFile(join(outside, 'secret'), 'hidden');
  await symlink(join(outside, 'secret'), join(dir, 'escape')); await writeFile(join(dir, 'binary'), Buffer.from([0, 1, 2]));
  const files = new Workspace(dir);
  assert.equal(await files.read('code.txt', 2, 2), '2: two\n3: three');
  assert.match(await files.list(), /code.txt/);
  await assert.rejects(files.read('escape'), /outside/); await assert.rejects(files.read('../outside'), /ENOENT|outside/);
  await assert.rejects(files.read('binary'), /Binary/); await assert.rejects(files.read('code.txt', 0));
});
test('Bash can inspect files but cannot write, delete, reach the host home or use networking', async t => {
  const dir = await directory(t); const source = join(dir, 'code.txt'); await writeFile(source, 'original');
  const files = new Workspace(dir);
  const read = await files.bash('pwd; cat code.txt; printf "\\n"; printenv DEEPSEEK_API_KEY || true');
  assert.match(read, /\/workspace\noriginal/); assert.doesNotMatch(read, /sk-/);
  const write = await files.bash('printf changed > code.txt; rm code.txt; touch added');
  assert.match(write, /Read-only file system/); assert.equal(await readFile(source, 'utf8'), 'original');
  await assert.rejects(stat(join(dir, 'added')), {code: 'ENOENT'});
  const net = await files.bash('timeout 2 bash -c "echo test > /dev/tcp/1.1.1.1/80"');
  assert.match(net, /Network is unreachable|Operation not permitted/);
  const home = await files.bash('ls /home; env'); assert.match(home, /No such file/); assert.match(home, /HOME=\/tmp/);
});
test('Bash stops on cancellation and excessive output', async t => {
  const files = new Workspace(await directory(t));
  assert.match(await files.bash('yes test'), /output exceeded/);
  const abort = new AbortController(); const task = files.bash('sleep 60', abort.signal);
  setTimeout(() => abort.abort(), 50); assert.match(await task, /stopped/);
});
test('runtime composition includes exactly the intended tools and no inherited profile', async t => {
  const dir = await directory(t); await createProfile(dir, dir);
  const profile = join(dir, 'profiles/kate-voice');
  assert.deepEqual(JSON.parse(await readFile(join(profile, 'package.json'), 'utf8')).dsh.profile.bundles, []);
  const rows = JSON.parse(await readFile(join(profile, 'cordis.patch.yml'), 'utf8'))[0].insert;
  assert.ok(rows.some(row => row.id === 'voice-tools'));
  assert.ok(!rows.some(row => /tool-bash|tool-fs|mcp|terminal|persona|workspace-instructions/.test(row.name)));
  const prompt = rows.find(row => row.id === 'system-prompt').config.personaPrefix;
  assert.equal(prompt, SYSTEM_PROMPT); assert.match(prompt, /hard ceiling of 60 English words or 100 Chinese characters/);
  assert.match(prompt, /ONE or TWO short sentences/); assert.match(prompt, /Never edit/);
  const tools = []; const previous = process.env.DEEPSEEK_API_KEY; process.env.DEEPSEEK_API_KEY = 'test-key';
  try { registerTools({tools: {register: tool => tools.push(tool)}}, {workspace: dir}); }
  finally { if (previous === undefined) delete process.env.DEEPSEEK_API_KEY; else process.env.DEEPSEEK_API_KEY = previous; }
  assert.deepEqual(tools.map(tool => tool.name), ['read_file', 'list_directory', 'bash', 'web_search', 'web_fetch']);
});
test('DSH protocol isolates sessions, extracts only the final answer and removes its private home', async t => {
  const dir = await directory(t), events = [];
  const dsh = new Dsh(process.env, event => events.push(event), {executable: process.execPath, args: [fake]});
  t.after(() => dsh.close()); await dsh.start(dir, 'deepseek-flash', 'high');
  assert.equal(await dsh.prompt('Explain', {selection: 'code'}), 'The answer.');
  const home = dsh.home; await dsh.close(); await assert.rejects(stat(home), {code: 'ENOENT'});
  assert.equal(events.length, 2);
});
test('DSH errors preserve the cause and redact authentication details', async t => {
  const dsh = new Dsh({...process.env, FAKE_DSH_MODE: 'auth'}, () => {}, {executable: process.execPath, args: [fake]});
  t.after(() => dsh.close()); await dsh.start(await directory(t), 'model', 'high');
  await assert.rejects(dsh.prompt('Hi', {}), error => /401/.test(error.message) && !/sk-/.test(error.message));
});
test('DSH crash and cancellation reject pending requests without hanging', async t => {
  for (const mode of ['crash', 'hang']) {
    const dsh = new Dsh({...process.env, FAKE_DSH_MODE: mode}, () => {}, {executable: process.execPath, args: [fake]});
    t.after(() => dsh.close()); const dir = await directory(t);
    if (mode === 'crash') await assert.rejects(dsh.start(dir, 'model', ''), /DSH exited/);
    else { await dsh.start(dir, 'model', ''); const pending = assert.rejects(dsh.prompt('Hi', {}), /stopped/); setTimeout(() => dsh.close(), 40); await pending; }
  }
});

function mockRuntime(options = {}) {
  const events = [], agents = []; let spoken = 0;
  const api = {transcribe: async () => 'Recognized question', speak: async () => { spoken++; if (options.ttsFails) throw new Error('TTS unavailable'); return wav(); }};
  const createDsh = onEvent => {
    const agent = {onEvent, prompts: [], closed: false, start: async (...args) => { agent.route = args; },
      prompt: async (...args) => { agent.prompts.push(args); agent.onEvent({type: 'tool/call', data: {name: 'read_file'}}); return 'A short answer.'; },
      close: async () => { agent.closed = true; }};
    agents.push(agent); return agent;
  };
  return {events, agents, api, createDsh, spoken: () => spoken};
}
test('voice flow recognizes, reasons, synthesizes and emits a playable private file', async t => {
  const dir = await directory(t), mock = mockRuntime(); await writeFile(join(dir, 'recording.wav'), wav());
  const bridge = new VoiceBridge({}, event => mock.events.push(event), {...mock, audioDirectory: dir});
  t.after(() => bridge.stop());
  await bridge.command({type: 'ask', request: 'r1', audio: join(dir, 'recording.wav'), cwd: dir, context: {selection: 'selected'}});
  assert.equal(mock.agents[0].prompts[0][0], 'Recognized question'); assert.deepEqual(mock.agents[0].prompts[0][1], {selection: 'selected'});
  assert.equal(mock.spoken(), 1); assert.ok(mock.events.some(e => e.type === 'audio'));
  assert.equal((await stat(join(dir, 'reply.wav'))).mode & 0o777, 0o600);
  assert.deepEqual(mock.events.filter(e => e.type === 'status').map(e => e.state), ['recognizing', 'thinking', 'synthesizing', 'idle']);
  assert.ok(mock.events.every(e => e.request === 'r1'));
});
test('selected speed reaches the reply audio and converter failure leaves the transcript readable', async t => {
  const dir = await directory(t), mock = mockRuntime(); mock.api.speak = async () => wav(2);
  const bridge = new VoiceBridge({PATH: process.env.PATH}, event => mock.events.push(event), {...mock, audioDirectory: dir});
  t.after(() => bridge.stop());
  await bridge.command({type: 'ask', text: 'Question', cwd: dir, speed: 2});
  assert.ok(mock.events.some(event => event.type === 'audio'));
  assert.ok(inspectWav(await readFile(join(dir, 'reply.wav'))).duration < 1.1);
  mock.events.length = 0; bridge.env.PATH = '/nonexistent';
  await bridge.command({type: 'ask', text: 'Question', cwd: dir, speed: 1.5});
  assert.ok(mock.events.some(event => event.role === 'assistant'));
  assert.ok(mock.events.some(event => /Install FFmpeg/.test(event.text || '')));
  assert.ok(!mock.events.some(event => event.type === 'audio'));
  assert.equal(mock.events.at(-1).state, 'idle');
});
test('invalid speed is rejected before a provider request', async t => {
  const dir = await directory(t), mock = mockRuntime();
  const bridge = new VoiceBridge({}, event => mock.events.push(event), {...mock, audioDirectory: dir});
  t.after(() => bridge.stop());
  await bridge.command({type: 'ask', text: 'Question', cwd: dir, speed: 'bad'});
  assert.equal(mock.agents.length, 0); assert.equal(mock.spoken(), 0);
  assert.ok(mock.events.some(event => /Speech speed/.test(event.text || '')));
});
test('follow-up turns reuse DSH; model stays flash/max and workspace changes start a fresh session', async t => {
  const dir = await directory(t), mock = mockRuntime();
  const bridge = new VoiceBridge({}, event => mock.events.push(event), {...mock, audioDirectory: dir}); t.after(() => bridge.stop());
  const request = {type: 'ask', text: 'Question', cwd: dir, speak: false};
  await bridge.command(request); await bridge.command(request); assert.equal(mock.agents.length, 1);
  assert.equal(mock.events.filter(e => e.type === 'process').length, 2); assert.equal(mock.spoken(), 0);
  await bridge.command({...request, model: 'deepseek-v4-pro', effort: 'low'}); assert.equal(mock.agents.length, 1);
  assert.deepEqual(mock.agents[0].route, [dir, 'deepseek-flash', 'max']);
  await bridge.command({...request, cwd: await directory(t)}); assert.equal(mock.agents.length, 2); assert.equal(mock.agents[0].closed, true);
});
test('TTS failure leaves the answer readable and returns to idle', async t => {
  const dir = await directory(t), mock = mockRuntime({ttsFails: true});
  const bridge = new VoiceBridge({}, event => mock.events.push(event), {...mock, audioDirectory: dir}); t.after(() => bridge.stop());
  await bridge.command({type: 'ask', text: 'Hi', cwd: dir});
  assert.ok(mock.events.some(e => e.role === 'assistant')); assert.ok(mock.events.some(e => /TTS unavailable/.test(e.text || '')));
  assert.equal(mock.events.at(-1).state, 'idle');
});
test('cancelled recognition cannot publish stale answers or audio', async t => {
  const dir = await directory(t), mock = mockRuntime(); await writeFile(join(dir, 'recording.wav'), wav());
  let release; mock.api.transcribe = () => new Promise(resolve => { release = resolve; });
  const bridge = new VoiceBridge({}, event => mock.events.push(event), {...mock, audioDirectory: dir}); t.after(() => bridge.stop());
  const turn = bridge.command({type: 'ask', audio: join(dir, 'recording.wav'), cwd: dir});
  while (!release) await new Promise(resolve => setTimeout(resolve, 5));
  await bridge.command({type: 'stop'}); release('late recognition'); await turn;
  assert.equal(mock.agents.length, 0); assert.ok(!mock.events.some(e => e.type === 'message' || e.type === 'audio'));
});
test('recording location must belong to this panel', async t => {
  const dir = await directory(t), outside = await directory(t), mock = mockRuntime();
  await writeFile(join(outside, 'recording.wav'), wav());
  const bridge = new VoiceBridge({}, event => mock.events.push(event), {...mock, audioDirectory: dir});
  await bridge.command({type: 'ask', cwd: dir, audio: join(outside, 'recording.wav')});
  assert.ok(mock.events.some(e => /Unexpected recording/.test(e.text || ''))); assert.equal(mock.agents.length, 0);
});
