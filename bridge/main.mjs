import {createInterface} from 'node:readline';
import {readFile, writeFile, realpath, chmod} from 'node:fs/promises';
import {resolve, dirname, join} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {DeepSeek, credentials, safeError} from './deepseek.mjs';
import {Dsh} from './dsh.mjs';
import {adjustSpeed, speechSpeed} from './audio.mjs';

export class VoiceBridge {
  constructor(env, emit, {api, createDsh, audioDirectory} = {}) {
    this.env = env; this.emit = emit; this.api = api;
    this.createDsh = createDsh || ((onEvent) => new Dsh(env, onEvent));
    this.directory = audioDirectory || dirname(fileURLToPath(import.meta.url));
    this.sequence = 0; this.generation = 0;
  }
  stop() {
    ++this.generation;
    this.abort?.abort(); this.abort = null;
    const dsh = this.dsh; this.dsh = null; this.route = '';
    return this.stopping = Promise.allSettled([this.stopping, dsh?.close()]).then(() => undefined);
  }
  async command(command) {
    if (command.type === 'stop' || command.type === 'reset') {
      await this.stop(); this.emit({type: 'status', state: 'idle', request: command.request, text: command.type === 'reset' ? 'New conversation' : 'Stopped'});
      return;
    }
    if (command.type !== 'ask') return;
    const queuedGeneration = this.generation;
    await this.stopping;
    if (queuedGeneration !== this.generation) return;
    if (this.abort) { this.emit({type: 'error', text: 'A conversation turn is already running.'}); return; }
    const generation = ++this.generation;
    const abort = this.abort = new AbortController();
    const id = String(command.request || ++this.sequence); const start = performance.now(); const costs = {};
    const current = () => generation === this.generation;
    const emit = event => { if (current()) this.emit({...event, request: command.request}); };
    const phase = async (name, action) => {
      const start = performance.now(); emit({type: 'status', state: name});
      try { return await action(); } finally { costs[name] = (performance.now() - start) / 1000; }
    };
    let outcome = 'completed';
    try {
      const speed = speechSpeed(command.speed);
      this.api ||= new DeepSeek(this.env.DEEPSEEK_API_KEY);
      let text = String(command.text || '').trim();
      if (command.audio) {
        const path = await realpath(command.audio);
        // The UI creates recordings only in this private, per-panel directory.
        if (dirname(path) !== await realpath(this.directory) || !path.endsWith('.wav')) throw new Error('Unexpected recording location.');
        text = await phase('recognizing', async () => this.api.transcribe(await readFile(path), abort.signal));
      }
      if (!text || text.length > 32000) throw new Error('Enter a message of 1–32,000 characters.');
      emit({type: 'message', role: 'user', id, text});
      const cwd = await realpath(command.cwd);
      const model = 'deepseek-flash';
      const effort = 'max';
      const route = JSON.stringify([cwd, model, effort]);
      const answer = await phase('thinking', async () => {
        if (!this.dsh || this.route !== route || this.dsh.failure) {
          await this.dsh?.close();
          abort.signal.throwIfAborted();
          this.dsh = this.createDsh(() => {});
          this.route = route; await this.dsh.start(cwd, model, effort);
        }
        this.dsh.onEvent = event => {
          if (event.type.startsWith('tool/')) emit({type: 'process', text: event.type + ': ' + safeError(JSON.stringify(event.data), this.env.DEEPSEEK_API_KEY).slice(0, 2000)});
        };
        abort.signal.throwIfAborted();
        return this.dsh.prompt(text, command.context || {});
      });
      abort.signal.throwIfAborted();
      emit({type: 'message', role: 'assistant', id, text: answer});
      if (command.speak !== false) {
        try {
          const audio = await phase('synthesizing', async () => {
            const original = await this.api.speak(answer, command.voice || 'stella', abort.signal);
            return original ? adjustSpeed(original, speed, abort.signal, this.env) : original;
          });
          if (audio && current()) {
            const path = join(this.directory, 'reply.wav');
            await writeFile(path, audio, {mode: 0o600}); await chmod(path, 0o600);
            emit({type: 'audio', path});
          }
        } catch (error) {
          if (abort.signal.aborted) throw error;
          emit({type: 'error', text: `The reply is in the transcript. ${safeError(error, this.env.DEEPSEEK_API_KEY)}`});
        }
      }
    } catch (error) {
      outcome = abort.signal.aborted ? 'stopped' : 'failed';
      if (current()) {
        emit({type: 'error', text: safeError(error, this.env.DEEPSEEK_API_KEY)});
        const dsh = this.dsh; this.dsh = null; this.route = ''; await dsh?.close();
      }
    } finally {
      if (current()) {
        this.abort = null;
        emit({type: 'timing', elapsed: (performance.now() - start) / 1000, costs, outcome});
        emit({type: 'status', state: 'idle'});
      }
    }
  }
}

async function main() {
  const emit = event => process.stdout.write(JSON.stringify(event) + '\n');
  const env = await credentials();
  // Node's built-in proxy support is initialized from the environment at startup.
  // Re-exec once after reading DSH's .env, without printing or passing secrets in argv.
  if (process.env.KATE_VOICE_PROXY_READY !== '1') {
    const {spawn} = await import('node:child_process');
    const child = spawn(process.execPath, ['--use-env-proxy', fileURLToPath(import.meta.url)], {
      stdio: 'inherit', env: {...env, KATE_VOICE_PROXY_READY: '1'},
    });
    const stop = () => child.kill('SIGTERM'); process.on('SIGTERM', stop); process.on('SIGINT', stop);
    child.on('error', () => { emit({type: 'error', text: 'Node.js 24.5 or newer is required.'}); process.exitCode = 1; });
    child.on('exit', code => { process.exitCode = code || 0; }); return;
  }
  const bridge = new VoiceBridge(env, emit);
  let closing = false;
  const close = async () => { if (closing) return; closing = true; await bridge.stop(); process.exit(0); };
  process.on('SIGTERM', close); process.on('SIGINT', close);
  const lines = createInterface({input: process.stdin});
  lines.on('close', close);
  lines.on('line', line => {
    if (line.length > 512000) { emit({type: 'error', text: 'Editor context exceeds the size limit.'}); return; }
    try { const command = JSON.parse(line); bridge.command(command).catch(error => emit({type: 'error', text: safeError(error, env.DEEPSEEK_API_KEY)})); }
    catch { emit({type: 'error', text: 'Invalid panel request.'}); }
  });
  emit({type: 'ready', hasKey: Boolean(env.DEEPSEEK_API_KEY)});
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href)
  main().catch(error => { process.stdout.write(JSON.stringify({type: 'error', text: safeError(error)}) + '\n'); process.exitCode = 1; });
