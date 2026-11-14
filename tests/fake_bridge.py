#!/usr/bin/env python3
"""No API calls: exercise the native JSONL/recording boundary."""
import json
import os
from pathlib import Path
import sys
import wave

def emit(value):
    print(json.dumps(value), flush=True)

emit({"type": "ready", "hasKey": True})
for line in sys.stdin:
    command = json.loads(line)
    request = command.get("request")
    if command["type"] == "ask":
        emit({"type": "status", "state": "thinking", "request": request})
        emit({"type": "message", "role": "user", "id": request, "text": command.get("text") or "Recorded question", "request": request})
        emit({"type": "process", "text": "read_file: completed", "request": request})
        emit({"type": "message", "role": "assistant", "id": request, "text": "See `sample.cpp:L2-3`. The formula is $x^2$.", "request": request})
        if os.environ.get("KATE_VOICE_TEST_PLAYBACK"):
            audio = Path(sys.argv[1]).parent / "reply.wav"
            with wave.open(str(audio), "wb") as file:
                file.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                file.writeframes(b"\0" * 4800)
            emit({"type": "audio", "path": str(audio), "request": request})
        emit({"type": "timing", "outcome": "completed", "elapsed": 1.2, "costs": {"thinking": 1.2}, "request": request})
    emit({"type": "status", "state": "idle", "request": request})
