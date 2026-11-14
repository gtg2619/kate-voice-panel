const assert = require('node:assert/strict');
const markdownit = require('../web/vendor/markdown-it/markdown-it.min.js');
const katex = require('../web/vendor/katex/katex.min.js');
const renderer = require('../web/renderer.js')(markdownit, katex);
const {render} = renderer;
assert.match(render('$E=mc^2$'), /class="katex"/);
assert.match(render('$$\n\\int_0^1 x^2 dx\n$$'), /katex-display/);
assert.match(render('\\[x^2\\]'), /katex-display/);
assert.match(render('\\(\\alpha + \\beta\\)'), /class="katex"/);
assert.match(render('\\( x^2 + y^2 \\)'), /class="katex"/);
for (const [open, close] of [['$$', '$$'], ['\\[', '\\]']]) {
  const output = render(`Here is the formula:\n${open}\n\\frac{1}{2}\n${close}\nContinue.`);
  assert.match(output, /katex-display/);
  assert.match(output, /<p>Continue\.<\/p>/);
}
for (const source of [
  '$$\\begin{pmatrix}1&2\\\\3&4\\end{pmatrix}$$',
  '$$\n\\begin{aligned}a&=b+c\\\\d&=e\\end{aligned}\n$$',
  '\\[\\sum_{k=1}^n k=\\frac{n(n+1)}{2}\\]'
]) {
  assert.match(render(source), /katex-display/);
  assert.doesNotMatch(render(source), /katex-error/);
}
assert.match(render('$\\frac{1}{$'), /katex-error/);
assert.doesNotMatch(render('Unfinished $\\frac{1}{'), /class="katex"/);
assert.match(render('Unfinished $\\frac{1}{2}$'), /class="katex"/);
assert.doesNotMatch(render('Costs $10 and $20; escaped \\$x\\$.'), /class="katex"/);
assert.doesNotMatch(render('`$x$`'), /class="katex"/);
assert.doesNotMatch(render('```latex\n$x$\n```'), /class="katex"/);
for (const source of ['> $$\n> x^2\n> $$', '- $$\n  x^2\n  $$']) {
  assert.match(render(source), /<annotation encoding="application\/x-tex">x\^2<\/annotation>/);
  assert.doesNotMatch(render(source), /katex-error/);
}
assert.doesNotMatch(render('> ```latex\n> $$x^2$$\n> ```'), /class="katex"/);
assert.match(render('`project/src/test.cpp:L123-234`'), /agent-file:project%2Fsrc%2Ftest.cpp%3AL123-234/);
assert.match(render('`project/src/a file.cpp:L1-2`'), /agent-file:project%2Fsrc%2Fa%20file.cpp%3AL1-2/);
assert.match(render('`project/path/to/file:L123-234`'), /agent-file:project%2Fpath%2Fto%2Ffile%3AL123-234/);
assert.match(render('See project/path/to/file:L123-234 please.'), /agent-file:project%2Fpath%2Fto%2Ffile%3AL123-234/);
assert.match(render('https://example.com/a.cpp'), /href="https:\/\/example.com\/a.cpp"/);
assert.match(render('See project/src/test.cpp:L123-234 please.'), /agent-file:project%2Fsrc%2Ftest.cpp%3AL123-234/);
assert.match(render('[file](project/src/test.cpp:L123-234)'), /href="project\/src\/test.cpp:L123-234"/);
assert.doesNotMatch(render('[x](javascript:alert(1))'), /href="javascript:/);
assert.doesNotMatch(render('<img src=x onerror="alert(1)">'), /<img/);
assert.doesNotMatch(render('![remote](https://example.com/track.png)'), /<img/);
assert.doesNotMatch(render('$\\href{javascript:alert(1)}{x}$'), /href="javascript/);
assert.doesNotMatch(render('```cpp\nproject/src/test.cpp:L2\n```'), /agent-file:/);
assert.equal((render('[src/a.cpp](src/a.cpp)').match(/<a /g) || []).length, 1);
assert.equal((render('[`project/src/a.cpp:L2`](project/src/a.cpp:L2)').match(/<a /g) || []).length, 1);
for (const reference of ['project/src/\u4e2d\u6587.cpp:L123-234', '\u9879\u76ee/\u76ee\u5f55/\u6587\u4ef6:L3-9', 'project/src/a%20file.cpp:L2', 'project/src/a+b.cpp:7:2']) {
  assert.ok(render(`See ${reference} please.`).includes(`agent-file:${encodeURIComponent(reference)}`), reference);
}
assert.ok(render('`project/src/\u4e2d\u6587 file%20.cpp:L123-234`').includes(`agent-file:${encodeURIComponent('project/src/\u4e2d\u6587 file%20.cpp:L123-234')}`));
const spaced = 'project/src/\u4f8b\u5b50 file%20.cpp:L123-234';
const spacedLink = render(`[${spaced}](${spaced})`);
assert.ok(spacedLink.includes(`agent-file:${encodeURIComponent(spaced)}`));
assert.equal((spacedLink.match(/<a /g) || []).length, 1);
assert.doesNotMatch(render(`\`[label](${spaced})\``), /<a /);
assert.doesNotMatch(render(`\`\`\`text\n[label](${spaced})\n\`\`\``), /<a /);
assert.doesNotMatch(render('[label](javascript:alert bad)'), /href="(?:javascript|agent-file):/);
assert.doesNotMatch(render('[label](https://example.com/a file.cpp)'), /agent-file:https/);
console.log('Renderer: math, code isolation, workspace ranges, links, and untrusted input passed');
