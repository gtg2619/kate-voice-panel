# Testing

`scripts/test.sh` builds the native plugin and runs CTest under Xvfb when available.
Node.js 24.5+ and Python 3 are required for fixtures. `KATE_VOICE_TEST_NODE` and
`KATE_VOICE_BUILD_DIR` override the test executable and build directory.

## Coverage

- DeepSeek multipart ASR, TTS audio validation, search/fetch contracts, authentication,
  rate limits, malformed responses, cancellation, proxy configuration, and redaction.
- DSH initialization, final-answer extraction, error propagation, process cleanup,
  follow-up sessions, fixed flash/max routing, and workspace changes.
- Actual Bubblewrap denial of workspace writes/deletes, host-home access, and network
  access; output limits and cancellation; file traversal and symlink rejection.
- Audio threshold, attack time, pre-roll, silence, short-noise rejection, fragmented
  samples, bounded recording duration, and WAV encoding.
- Real FFmpeg tempo conversion at 0.5×, 1.5×, and 2×, with duration and pitch checks,
  cancellation/process cleanup, missing converters, and normal-speed passthrough.
- Parameter edits saved to disk before sending or closing, device refresh/custom IDs,
  close-time flushing, cross-window preservation, and microphone-off restoration.
- Native context snapshots, transcript/file/LaTeX rendering, audio activation and
  cancellation, background listening, fixed control layout, low-height hiding, themes,
  lazy plugin loading, missing executables, and Esc from different focus targets.
- Repeated dock growth/shrinking, top-aligned controls, and skipping playback through
  the button or shortcut while hidden, preserving the conversation and resuming input.

The current offline run passes all six CTest suites: 27 Node bridge cases, 22 native
panel cases, 10 voice-activity cases, file-link tests, rendering checks, and metadata
validation. Qt totals include suite setup/cleanup; screenshot capture is opt-in.
The two input-region cases require Wayland and are skipped under Xvfb.

The normal suite does not record a physical microphone, play sound, or call a paid
API. Native tests replace PipeWire commands and the bridge with fixtures.

## Live validation

Run `KATE_VOICE_LIVE=1 node tests/live_test.mjs` to opt into paid DeepSeek calls.
This generates a synthetic question with TTS, runs ASR → DSH → TTS through the
actual bridge at 1.5× speed, checks short English/Chinese follow-ups and unchanged
fixtures, then checks search/fetch. FFmpeg is required for this live test.
If a stale shell key overrides an updated `.dsh/.env`, use
`env -u DEEPSEEK_API_KEY KATE_VOICE_LIVE=1 node tests/live_test.mjs`.

On Fedora 45 with Qt 6.11.2, KF6 6.31, Node 24.16, and DSH 0.2.2-rc.1:

- DeepSeek TTS generated a valid mono 24 kHz WAV; DeepSeek ASR recognized its text.
- DeepSeek search and fetch returned results from the official Kate website.
- The real `deepseek-flash` model with `max` effort called `read_file`, `bash`,
  `web_search`, and `web_fetch`, then produced a short answer. Fixture files were unchanged.
- A full synthetic voice question completed ASR → DSH → TTS through the bridge at
  1.5× speed; English and Chinese follow-ups met the default answer budgets.
- The restored panel rendered in a real Kate instance under headless GNOME/Wayland
  with the dark theme, sidebar, fixed controls, and transcript intact.
- Actual pointer drags enlarged and shrank the dock, then hid it below the control
  height. Reopening restored a usable height and the voice switch remained clickable.
  Wayland tests also checked restoring pointer input after release/hiding and
  skipping speech with the panel hidden.

These checks validate the service/protocol path. They do not certify microphone
quality, room acoustics, or every audio device. Physical microphone and speaker
testing requires the user's selected hardware.

## Artifacts

`build/test-results.xml` and `build/Testing/Temporary/LastTest.log` contain results.
Set `KATE_VOICE_SCREENSHOT` to a PNG path and run `panel_test screenshot` to capture
the native panel with an example conversation. The screenshot case is opt-in.
