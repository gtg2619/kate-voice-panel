import {spawn} from 'node:child_process';
import {realpath, stat, open, readdir} from 'node:fs/promises';
import {constants} from 'node:fs';
import {resolve, relative, isAbsolute, join} from 'node:path';

export class Workspace {
  constructor(root) { this.root = root; }
  async path(input = '.') {
    if (typeof input !== 'string' || input.includes('\0')) throw new Error('Invalid workspace path.');
    const path = await realpath(resolve(this.root, input));
    const rel = relative(this.root, path);
    if (rel === '..' || rel.startsWith('../') || isAbsolute(rel)) throw new Error('Path is outside the workspace.');
    return path;
  }
  async read(input, start = 1, count = 120) {
    if (!Number.isSafeInteger(start) || start < 1 || !Number.isSafeInteger(count) || count < 1 || count > 400)
      throw new Error('Use a positive starting line and 1–400 lines.');
    const path = await this.path(input);
    const handle = await open(path, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK);
    try {
      // Verify the opened inode after opening, including concurrent symlink changes.
      await this.path(`/proc/self/fd/${handle.fd}`);
      const info = await handle.stat();
      if (!info.isFile() || info.size > 2 * 1024 * 1024) throw new Error('Read requires a regular text file of at most 2 MiB.');
      const buffer = Buffer.alloc(2 * 1024 * 1024 + 1);
      const {bytesRead} = await handle.read(buffer, 0, buffer.length, 0);
      if (bytesRead > 2 * 1024 * 1024) throw new Error('The file grew beyond the read limit.');
      const text = buffer.subarray(0, bytesRead);
      if (text.includes(0)) throw new Error('Binary files are not supported.');
      return text.toString('utf8').split(/\r?\n/).slice(start - 1, start - 1 + count)
        .map((line, n) => `${start + n}: ${line}`).join('\n').slice(0, 40000);
    } finally { await handle.close(); }
  }
  async list(input = '.') {
    const directory = await this.path(input);
    if (!(await stat(directory)).isDirectory()) throw new Error('Not a workspace directory.');
    const entries = await readdir(directory, {withFileTypes: true});
    return entries.sort((a, b) => a.name.localeCompare(b.name)).slice(0, 1000)
      .map(entry => entry.name + (entry.isDirectory() ? '/' : entry.isSymbolicLink() ? ' (symlink)' : '')).join('\n');
  }
  async bash(command, signal) {
    if (typeof command !== 'string' || !command.trim() || command.length > 16000) throw new Error('Invalid Bash command.');
    const root = await realpath(this.root);
    const args = ['--unshare-all', '--new-session', '--die-with-parent', '--ro-bind', '/usr', '/usr',
      '--symlink', 'usr/bin', '/bin', '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib64', '/lib64',
      '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--dir', '/etc',
      '--ro-bind-try', '/etc/ld.so.cache', '/etc/ld.so.cache', '--ro-bind', root, '/workspace',
      '--chdir', '/workspace', '--clearenv', '--setenv', 'PATH', '/usr/bin:/bin', '--setenv', 'HOME', '/tmp',
      '--setenv', 'LANG', 'C.UTF-8', '/bin/bash', '--noprofile', '--norc', '-c', command];
    return new Promise((resolveResult, reject) => {
      const child = spawn('bwrap', args, {stdio: ['ignore', 'pipe', 'pipe']});
      const chunks = []; let length = 0; let stopped = '';
      const stop = reason => { stopped ||= reason; child.kill('SIGKILL'); };
      const abort = () => stop('Command stopped.');
      const timer = setTimeout(() => stop('Command exceeded 15 seconds.'), 15000);
      const collect = data => { length += data.length; if (length <= 64000) chunks.push(data); else stop('Command output exceeded 64 KiB.'); };
      child.stdout.on('data', collect); child.stderr.on('data', collect);
      signal?.addEventListener('abort', abort, {once: true}); if (signal?.aborted) abort();
      child.once('error', () => { cleanup(); reject(new Error('Bubblewrap is unavailable. Bash stays disabled. Install bubblewrap and enable unprivileged user namespaces.')); });
      child.once('close', code => { cleanup(); resolveResult(`${Buffer.concat(chunks).toString('utf8')}\n${stopped || `Exit status: ${code}`}`); });
      function cleanup() { clearTimeout(timer); signal?.removeEventListener('abort', abort); }
    });
  }
}
