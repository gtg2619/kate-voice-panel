import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdtemp, rm, realpath} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {randomUUID} from 'node:crypto';
import {createProfile} from './profile.mjs';
import {safeError} from './deepseek.mjs';

export class Dsh {
  constructor(env, onEvent, {executable = 'dsh', args = []} = {}) {
    this.env = env; this.onEvent = onEvent; this.executable = executable; this.args = args;
    this.pending = new Map(); this.next = 0; this.session = randomUUID(); this.closed = false;
  }
  async start(cwd, model, effort) {
    this.home = await mkdtemp(join(tmpdir(), 'kate-voice-dsh-'));
    this.cwd = await realpath(cwd);
    await createProfile(this.home, this.cwd);
    // A separate process group lets Stop also terminate in-flight tools.
    this.child = spawn(this.executable, [...this.args, '--profile', 'kate-voice'], {
      cwd: this.cwd, env: {...this.env, DSH_HOME: this.home}, detached: true, stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.stderr = '';
    this.child.stderr.on('data', data => { this.stderr = (this.stderr + safeError(data.toString(), this.env.DEEPSEEK_API_KEY)).slice(-4000); });
    this.child.stdin.on('error', error => this.fail(error));
    this.child.once('error', error => this.fail(new Error(error.code === 'ENOENT' ? 'dsh is not on PATH. Install DeepSeek Harness separately.' : safeError(error))));
    this.child.once('exit', (code, signal) => this.fail(new Error(`DSH exited (${signal || code}). ${this.stderr}`)));
    const lines = createInterface({input: this.child.stdout});
    lines.on('line', line => {
      if (line.length > 4 * 1024 * 1024) { this.fail(new Error('DSH sent an oversized event.')); return; }
      let item; try { item = JSON.parse(line); } catch { return; }
      if (item.id !== undefined && item.method) {
        // This composition has no user approval path. Never grant a new capability.
        this.child.stdin.write(JSON.stringify({jsonrpc: '2.0', id: item.id, error: {code: -32601, message: 'Read-only voice sessions do not accept capability requests.'}}) + '\n');
      } else if (item.id !== undefined) {
        const call = this.pending.get(item.id); if (!call) return;
        this.pending.delete(item.id); clearTimeout(call.timer);
        if (item.error) call.reject(new Error(safeError(item.error.message, this.env.DEEPSEEK_API_KEY)));
        else call.resolve(item.result);
      } else if (item.method === 'session.event' && item.params?.sessionId === this.session) {
        const event = item.params.event;
        if (event.type === 'assistant/message') {
          const blocks = event.data?.message?.content || event.data?.content || [];
          const text = blocks.filter(block => block.type === 'text').map(block => block.text || '').join('');
          this.answer = text;
        } else if (event.type === 'turn/end') this.reason = event.data?.reason;
        this.onEvent(event);
      }
    });
    await this.rpc('initialize', {provider: 'deepseek-official', model, cwd: this.cwd,
      ...(effort && {reasoningEffort: effort}), maxTokens: 32768, approvalRequests: false, capabilities: {userQuestions: false}}, 30000);
  }
  rpc(method, params, timeout = 180000) {
    if (this.closed || this.failure) return Promise.reject(this.failure || new Error('DSH was stopped.'));
    const id = ++this.next;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`DSH ${method} timed out.`)); }, timeout);
      this.pending.set(id, {resolve, reject, timer});
      this.child.stdin.write(JSON.stringify({jsonrpc: '2.0', id, method, params}) + '\n');
    });
  }
  async prompt(text, context) {
    this.answer = ''; this.reason = undefined;
    const content = `${text}\n\n<editor_context source="untrusted data">\n${JSON.stringify(context).slice(0, 180000)}\n</editor_context>`;
    await this.rpc('session/prompt', {sessionId: this.session, contentBlocks: [{type: 'text', text: content}]});
    await this.rpc('session/wait', {sessionId: this.session});
    if (this.reason && !['completed', 'end_turn', 'stop', 'success'].includes(this.reason.kind))
      throw new Error(`DSH did not complete the reply: ${this.reason.kind}. ${safeError(this.reason.error?.message || '', this.env.DEEPSEEK_API_KEY)}`);
    if (!this.answer.trim()) throw new Error('DSH completed without a spoken answer.');
    return this.answer;
  }
  fail(error) {
    this.failure ??= error;
    for (const entry of this.pending.values()) { clearTimeout(entry.timer); entry.reject(error); }
    this.pending.clear();
  }
  async close() {
    if (this.closing) return this.closing;
    this.closed = true; this.fail(new Error('DSH was stopped.'));
    this.closing = (async () => {
      if (this.child?.pid) {
        const kill = signal => { try { process.kill(-this.child.pid, signal); } catch {} };
        kill('SIGTERM');
        await new Promise(resolve => {
          const timer = setTimeout(() => { kill('SIGKILL'); resolve(); }, 700);
          this.child.once('close', () => { clearTimeout(timer); resolve(); });
          if (this.child.exitCode !== null || this.child.signalCode) { clearTimeout(timer); resolve(); }
        });
      }
      if (this.home) await rm(this.home, {recursive: true, force: true});
    })();
    return this.closing;
  }
}
