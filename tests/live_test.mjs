// Explicit opt-in only. Uses synthetic speech, never a physical microphone or speaker.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp, writeFile, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {credentials, DeepSeek, inspectWav, safeError} from '../bridge/deepseek.mjs';
import {VoiceBridge} from '../bridge/main.mjs';

if (process.env.KATE_VOICE_LIVE !== '1') {
  console.error('Set KATE_VOICE_LIVE=1 to allow paid DeepSeek API calls.'); process.exit(2);
}
const env = await credentials();
if (!process.env.KATE_VOICE_LIVE_PROXY_READY) {
  const child = spawn(process.execPath, ['--use-env-proxy', fileURLToPath(import.meta.url)], {
    stdio: 'inherit', env: {...env, KATE_VOICE_LIVE_PROXY_READY: '1'},
  });
  process.exitCode = await new Promise(resolve => child.on('exit', resolve));
} else {
  const directory = await mkdtemp(join(tmpdir(), 'kate-voice-live-'));
  const events = [];
  const bridge = new VoiceBridge(env, event => events.push(event), {audioDirectory: directory});
  try {
    const api = new DeepSeek(env.DEEPSEEK_API_KEY);
    const source = 'The project codename is Indigo Finch.\n';
    await writeFile(join(directory, 'README.txt'), source);
    const question = await api.speak('Read the readme file and tell me the project code name in one sentence.', 'stella');
    await writeFile(join(directory, 'recording.wav'), question);
    await bridge.command({type: 'ask', request: 'live', cwd: directory, audio: join(directory, 'recording.wav'), voice: 'tide', speed: 1.5,
      context: {workspace: directory, openFiles: [{path: 'README.txt'}]}});
    const errors = events.filter(event => event.type === 'error');
    assert.deepEqual(errors, [], JSON.stringify(errors));
    const answer = events.find(event => event.role === 'assistant')?.text;
    assert.match(answer || '', /Indigo Finch/i);
    const speech = events.find(event => event.type === 'audio'); assert.ok(speech);
    assert.ok(inspectWav(await readFile(speech.path)).duration > 0);
    assert.equal(await readFile(join(directory, 'README.txt'), 'utf8'), source);
    const code = 'export function lookup(items, id) { return items.find(item => item.id === id) ?? null; }';
    await writeFile(join(directory, 'lookup.js'), code);
    const brevity = [];
    for (const [language, question] of [
      ['English', 'What does the selected function do?'],
      ['Chinese', 'Answer in Chinese: why use ?? null in this function?'],
    ]) {
      events.length = 0;
      await bridge.command({type: 'ask', request: language, cwd: directory, text: question, speak: false,
        context: {workspace: directory, activeFile: {path: 'lookup.js', selection: code, excerpt: code}}});
      assert.ok(!events.some(event => event.type === 'error'));
      const reply = events.find(event => event.role === 'assistant')?.text; assert.ok(reply);
      assert.doesNotMatch(reply, /(^|\n)\s*(?:#{1,6}\s|[-*]\s|\d+\.\s)|```/);
      assert.ok(reply.trim().split(/\n\s*\n/).length <= 2);
      if (language === 'English') assert.ok(reply.trim().split(/\s+/).length <= 60, reply);
      else { const count = (reply.match(/\p{Script=Han}/gu) || []).length; assert.ok(count > 0 && count <= 100, reply); }
      brevity.push({language, reply});
    }
    assert.equal(await readFile(join(directory, 'lookup.js'), 'utf8'), code);
    const search = JSON.parse(await api.search('KDE Kate official editor')); assert.ok(search.length);
    assert.ok((await api.page('https://kate-editor.org/')).length > 100);
    console.log(JSON.stringify({voiceRoundTrip: true, speechSpeed: 1.5, fixedModel: 'deepseek-flash', effort: 'max',
      answer, brevity, searchResults: search.length,
      webFetch: true, workspaceUnchanged: true}, null, 2));
  } catch (error) { console.error(safeError(error, env.DEEPSEEK_API_KEY)); process.exitCode = 1; }
  finally { await bridge.stop(); await rm(directory, {recursive: true, force: true}); }
}
