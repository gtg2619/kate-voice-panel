/* Markdown and mathematics renderer. Shared by the embedded view and Node tests. */
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory;
  else root.createAgentRenderer = factory;
})(typeof globalThis !== "undefined" ? globalThis : this, function (markdownit, katex) {
  "use strict";
  const md = markdownit({html: false, linkify: true, breaks: true});
  const escape = md.utils.escapeHtml;
  const originalValidate = md.validateLink;
  md.validateLink = href => /^file:\/\//i.test(href) || originalValidate(href);
  const math = (source, display) => {
    try {
      return katex.renderToString(source, {displayMode: display, throwOnError: false,
        trust: false, strict: "ignore", maxExpand: 1000, maxSize: 20, output: "htmlAndMathml"});
    } catch (_) { return `<code>${escape(source)}</code>`; }
  };
  md.inline.ruler.before("escape", "agent_math", (state, silent) => {
    const start = state.pos;
    let open, close;
    if (state.src.startsWith("\\(", start)) { open = "\\("; close = "\\)"; }
    else if (state.src[start] === "$" && state.src[start + 1] !== "$" && !/\s/.test(state.src[start + 1] || " ")) { open = close = "$"; }
    else return false;
    let end = start + open.length;
    while ((end = state.src.indexOf(close, end)) !== -1) {
      if (state.src[end - 1] !== "\\") break;
      end += close.length;
    }
    if (end < 0 || end === start + open.length || (close === "$" && /\s/.test(state.src[end - 1]))) return false;
    if (close === "$" && /\d/.test(state.src[end + 1] || "")) return false;
    if (!silent) {
      const token = state.push("agent_math", "math", 0);
      token.content = state.src.slice(start + open.length, end);
    }
    state.pos = end + close.length;
    return true;
  });
  md.renderer.rules.agent_math = (tokens, i) => math(tokens[i].content, false);
  md.block.ruler.before("fence", "agent_math_block", (state, startLine, endLine, silent) => {
    const start = state.bMarks[startLine] + state.tShift[startLine];
    let open, close;
    if (state.src.startsWith("$$", start)) open = close = "$$";
    else if (state.src.startsWith("\\[", start)) { open = "\\["; close = "\\]"; }
    else return false;
    const end = state.src.indexOf(close, start + open.length);
    if (end < 0 || end + close.length > state.eMarks[endLine - 1]) return false;
    let next = startLine;
    while (next < endLine && state.eMarks[next] < end + close.length) next++;
    if (state.src.slice(end + close.length, state.eMarks[next]).trim()) return false;
    if (silent) return true;
    const token = state.push("agent_math_block", "math", 0);
    token.block = true;
    // Markdown adjusts line offsets inside lists and blockquotes; slicing the raw
    // source would leak quote markers into the TeX expression.
    const block = state.getLines(startLine, next + 1, state.blkIndent, false).trim();
    token.content = block.slice(open.length, -close.length).trim();
    token.map = [startLine, next + 1];
    state.line = next + 1;
    return true;
  }, {alt: ["paragraph", "reference", "blockquote", "list"]});
  md.renderer.rules.agent_math_block = (tokens, i) => `<div class="math-block">${math(tokens[i].content, true)}</div>\n`;
  const pathPattern = /^(?:(?:\.{1,2}\/|~\/|\/)?(?:[^\s<>"|:*?]+\/)*[^\s<>"|:*?/]+\.[A-Za-z0-9_-]+)(?::L\d+(?:-L?\d+)?|:\d+(?::\d+)?|#L\d+(?:C\d+)?(?:-L\d+)?)?$/;
  const looksLikePath = text => (pathPattern.test(text) || /^[^\n\r<>"|:*?]+:L\d+(?:-L?\d+)?$/.test(text) ||
    (text.includes("/") && /^[^\n\r<>"|:*?]+\.[A-Za-z0-9_-]+(?::L\d+(?:-L?\d+)?|:\d+(?::\d+)?|#L\d+(?:C\d+)?(?:-L\d+)?)?$/.test(text))) &&
    !/^[a-z]+:\/\//i.test(text) && !/^[\d.]+$/.test(text);
  const fileAnchor = (text, code = false, label = text) => `<a class="file-link" href="agent-file:${encodeURIComponent(text)}">${code ? "<code>" : ""}${escape(label)}${code ? "</code>" : ""}</a>`;
  // CLIs sometimes emit Markdown destinations with literal spaces. Recover only
  // explicit local file links; code, network URLs, and nested links keep normal parsing.
  md.inline.ruler.before("link", "agent_spaced_file_link", (state, silent) => {
    if (state.linkLevel || state.src[state.pos] !== "[" || state.src[state.pos - 1] === "!") return false;
    const match = /^\[([^\]\r\n]+)\]\(([^()\r\n]+)\)/.exec(state.src.slice(state.pos));
    if (!match || !/\s/.test(match[2]) || !looksLikePath(match[2])) return false;
    if (!silent) {
      const token = state.push("agent_spaced_file_link", "", 0);
      token.content = match[2]; token.meta = {label: match[1]};
    }
    state.pos += match[0].length;
    return true;
  });
  md.renderer.rules.agent_spaced_file_link = (tokens, i) => fileAnchor(tokens[i].content, false, tokens[i].meta.label);
  md.renderer.rules.code_inline = (tokens, i) => {
    const text = tokens[i].content;
    return !tokens[i].meta?.insideLink && looksLikePath(text) ? fileAnchor(text, true) : `<code>${escape(text)}</code>`;
  };
  // Transform only plain text tokens outside existing links; code fences and math are untouched.
  md.core.ruler.after("inline", "agent_file_links", state => {
    for (const block of state.tokens) {
      if (!block.children) continue;
      let depth = 0;
      const out = [];
      for (const token of block.children) {
        if (token.type === "link_open") depth++;
        if (token.type === "link_close") depth--;
        if (token.type === "code_inline" && depth) token.meta = {...token.meta, insideLink: true};
        if (token.type !== "text" || depth) { out.push(token); continue; }
        const re = /(?:\.{1,2}\/|~\/|\/)?(?:[\p{L}\p{N}\p{M}_@.%+\-]+\/)*[\p{L}\p{N}\p{M}_@.%+\-]+:L\d+(?:-L?\d+)?|(?:\.{1,2}\/|~\/|\/)?(?:[\p{L}\p{N}\p{M}_@.%+\-]+\/)*[\p{L}\p{N}\p{M}_@.%+\-]+\.[A-Za-z0-9_-]+(?::\d+(?::\d+)?|#L\d+(?:C\d+)?(?:-L\d+)?)?/gu;
        let offset = 0;
        for (const match of token.content.matchAll(re)) {
          const text = match[0];
          if (!looksLikePath(text)) continue;
          const before = new state.Token("text", "", 0); before.content = token.content.slice(offset, match.index); out.push(before);
          const link = new state.Token("agent_file", "", 0); link.content = text; out.push(link);
          offset = match.index + text.length;
        }
        const after = new state.Token("text", "", 0); after.content = token.content.slice(offset); out.push(after);
      }
      block.children = out;
    }
  });
  md.renderer.rules.agent_file = (tokens, i) => fileAnchor(tokens[i].content);
  // No remote image loading or model-controlled HTML, scripts, styles, or bridge calls.
  md.renderer.rules.image = (tokens, i) => `<span class="image-placeholder">[${escape(tokens[i].content || "image")}]</span>`;
  return {render: text => md.render(String(text)), looksLikePath};
});
