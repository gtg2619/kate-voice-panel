#!/usr/bin/env python3
"""PipeWire fixture; never opens a microphone or speaker."""
import json
import os
import signal
import struct
import sys
import time
import wave

name = os.path.basename(sys.argv[0])
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
if name == "pw-dump":
    print(json.dumps([{"info": {"props": {"media.class": "Audio/Source", "node.name": "test.mic", "node.description": "Test microphone"}}}]))
elif name == "pw-play":
    time.sleep(30 if os.environ.get("KATE_VOICE_TEST_PLAYBACK") else .2)
elif "--raw" in sys.argv:
    # Optional finite speech followed by silence drives automatic input tests.
    if os.environ.get("KATE_VOICE_TEST_SPEECH") == "1":
        speech = struct.pack("<h", 6000) * 8000
        os.write(sys.stdout.fileno(), speech)
    while True:
        os.write(sys.stdout.fileno(), b"\0" * 640)
        time.sleep(.02)
else:
    with wave.open(sys.argv[-1], "wb") as audio:
        audio.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
        audio.writeframes(struct.pack("<h", 6000) * 8000)
    while True:
        time.sleep(.05)
