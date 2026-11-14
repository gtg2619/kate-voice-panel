import {createInterface} from 'node:readline';
const send = value => process.stdout.write(JSON.stringify(value) + '\n');
const mode = process.env.FAKE_DSH_MODE;
let session;
createInterface({input: process.stdin}).on('line', line => {
  const request = JSON.parse(line);
  if (request.method === 'initialize') {
    if (mode === 'crash') { process.stderr.write('fake failure sk-must-not-leak'); process.exit(2); }
    if (mode === 'timeout') return;
    send({id: request.id, result: {serverInfo: {name: 'fake'}}});
  } else if (request.method === 'session/prompt') {
    session = request.params.sessionId;
    send({id: request.id, result: {messageId: 'test'}});
    send({method: 'session.event', params: {sessionId: 'unrelated', event: {type: 'assistant/message', data: {content: [{type: 'text', text: 'Wrong session'}]}}}});
    send({method: 'session.event', params: {sessionId: session, event: {type: 'assistant/message', data: {message: {content: [{type: 'thinking', text: 'Private thought'}, {type: 'text', text: 'The answer.'}]}}}}});
    send({method: 'session.event', params: {sessionId: session, event: {type: 'turn/end', data: {reason: mode === 'auth' ? {kind: 'error', error: {message: 'HTTP 401 invalid key sk-must-not-leak'}} : {kind: 'completed'}}}}});
  } else if (request.method === 'session/wait') {
    if (mode !== 'hang') send({id: request.id, result: {}});
  }
});
