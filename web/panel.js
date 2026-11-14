"use strict";
const renderer = createAgentRenderer(markdownit, katex);
const conversation = document.getElementById("conversation");
const empty = document.getElementById("empty");
let nativePanel, activity, timer, processGroup;
let followTail = true;
const messages = new Map();
const scroll = () => { if (followTail) window.scrollTo(0, document.documentElement.scrollHeight); };
window.addEventListener("scroll", () => {
  followTail = innerHeight + scrollY >= document.documentElement.scrollHeight - 40;
}, {passive: true});
function append(node) {
  empty.hidden = true; conversation.append(node);
  if (activity && activity !== node) conversation.append(activity);
  while (conversation.childElementCount > 500) {
    const oldest = conversation.firstElementChild;
    if (oldest.dataset.key) messages.delete(oldest.dataset.key);
    oldest.remove();
  }
  scroll();
}
function status(event) {
  clearInterval(timer); activity?.remove(); activity = null;
  if (event.state === "idle") return;
  const labels = {starting: "Starting", listening: "Listening", hearing: "Hearing speech",
    recognizing: "Recognizing", thinking: "Working", synthesizing: "Preparing speech", speaking: "Speaking"};
  const label = labels[event.state] || "Working";
  activity = document.createElement("div"); activity.className = "activity";
  activity.setAttribute("role", "status"); activity.textContent = label + "."; append(activity);
  let dots = 1;
  timer = setInterval(() => { if (activity) activity.textContent = label + ".".repeat(dots = dots % 3 + 1); }, 450);
}
function process(text) {
  if (!processGroup) {
    const node = document.createElement("details"); node.className = "process";
    const title = document.createElement("summary"); title.textContent = "Process";
    const body = document.createElement("pre"); node.append(title, body); append(node);
    title.addEventListener("click", () => { followTail = false; }); processGroup = body;
  }
  processGroup.textContent = (processGroup.textContent + text + "\n").slice(-24000); scroll();
}
window.receive = event => {
  if (event.type === "theme") {
    for (const name of ["background", "foreground", "muted", "accent", "border"])
      document.documentElement.style.setProperty(`--${name}`, event[name]);
    document.documentElement.style.colorScheme = event.colorScheme;
    document.body.style.fontFamily = event.fontFamily; document.body.style.fontSize = `${event.fontSize}px`;
    document.documentElement.style.setProperty("--fixed-font", JSON.stringify(event.fixedFontFamily));
  } else if (event.type === "clear") {
    status({state: "idle"}); conversation.replaceChildren(); messages.clear(); processGroup = null; empty.hidden = false; followTail = true;
  } else if (event.type === "status") status(event);
  else if (event.type === "process") process(event.text);
  else if (event.type === "message") {
    const role = event.role === "user" ? "user" : "assistant";
    if (role === "user") processGroup = null;
    const key = `${role}:${event.id}`;
    let node = messages.get(key);
    if (!node) {
      node = document.createElement("article"); node.className = "message " + role; node.dataset.key = key;
      const label = document.createElement("header"); label.textContent = role === "user" ? "You" : "Voice";
      const body = document.createElement("div"); body.className = "body"; node.append(label, body);
      messages.set(key, node); append(node);
    }
    node.querySelector(".body").innerHTML = renderer.render(String(event.text || "").slice(0, 128000)); scroll();
  } else if (["error", "notice", "timing"].includes(event.type)) {
    const node = document.createElement("div"); node.className = event.type;
    if (event.type === "timing") {
      const elapsed = Number(event.elapsed);
      if (!Number.isFinite(elapsed)) return;
      const costs = Object.entries(event.costs || {}).filter(([, n]) => Number.isFinite(n) && n > 0);
      node.textContent = `${event.outcome === "failed" ? "Failed" : "Completed"} in ${elapsed.toFixed(1)} s` +
        costs.map(([phase, n]) => ` · ${phase} ${n.toFixed(1)} s (${Math.round(n / Math.max(.001, elapsed) * 100)}%)`).join("");
      node.title = "Client-observed elapsed time; playback is not included.";
    } else node.textContent = event.text;
    append(node);
  }
};
document.addEventListener("click", event => {
  const link = event.target.closest("a"); if (!link) return;
  event.preventDefault(); nativePanel?.openLink(link.getAttribute("href"));
});
new QWebChannel(qt.webChannelTransport, channel => {
  nativePanel = channel.objects.panel;
  nativePanel.transcriptEvent.connect(json => { try { window.receive(JSON.parse(json)); } catch {} });
  nativePanel.rendererReady();
});
