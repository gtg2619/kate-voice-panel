# Changelog

## Unreleased

- Save parameter changes automatically and restore them across restarts, without enabling the microphone.
- Preserve custom audio device IDs and avoid overwriting newer settings when another Kate window closes.
- Add 0.5×–2× speech speed using local FFmpeg tempo adjustment without changing voice pitch.
- Balance the voice switch contents and move the level meter below it, retaining the four-row settings layout.
- Align settings at the top, fix Wayland dock dragging across the native panel, and restore a usable height when reopening.
- Strengthen the prompt with explicit one-to-two-sentence answer budgets and exceptions for requested detail.
- Add a Skip speech button and configurable shortcut that preserve the conversation and resume enabled input.

## 0.1.0

- Add a read-only Kate voice panel using DeepSeek Harness and DeepSeek speech/web services.
- Fix reasoning to deepseek-flash with max effort and keep replies brief.
- Add local volume-triggered input, silence detection, playback, and background listening.
- Capture editor context automatically and constrain workspace tools to read-only access.
- Provide eight voice settings, a large switch, and a separate scrolling transcript.
- Hide the panel when it cannot fit the controls; preserve enabled listening while hidden.
- Reuse Agent Panel file references, Markdown/LaTeX rendering, and native theme behavior.
