# UI Documentation Session - 31 October 2026

These eight original screenshots accompany the thesis's refreshed UI figures.
The author approved both the clothing scene and publication of this evidence.
No faces are visible in the reviewed screenshots. PNG bytes are unchanged;
cropping and enlarged details in the DOCX are non-destructive document layout.

## Scope

All services ran on one Linux laptop with its integrated camera configured at
640x480 and 30 FPS. This session illustrates operation and verifies the identity
of one recording. It is separate from the earlier F450 performance experiments,
two-laptop LAN runs, optical-latency photographs, and FIFO/bounded comparisons.
Configured FPS is not a measured live viewing rate.

| File | View |
| --- | --- |
| [01-qt-live.png](01-qt-live.png) | Qt live preview |
| [02-qt-recording.png](02-qt-recording.png) | Qt recording active |
| [03-qt-recordings.png](03-qt-recordings.png) | Qt completed recording, 61 seconds |
| [04-qt-playback.png](04-qt-playback.png) | Same recording playing in Qt, 1:01 |
| [05-qt-activity.png](05-qt-activity.png) | Actual command history |
| [06-web-live.png](06-web-live.png) | Browser WebRTC live view |
| [07-web-recordings.png](07-web-recordings.png) | Browser completed recording, 61 seconds |
| [08-web-playback.png](08-web-playback.png) | Browser fullscreen playback, 1:01 |

The Qt recording view has no elapsed recording timer; its real active status is
shown instead. Activity includes earlier commands and is not a duration clock.

## Recording Identity And Checks

[verification.json](verification.json) binds the eight PNG hashes to recording
UUID `61822cf1-5eed-4904-a40f-3e1cd1aa98b6`. The backend snapshots and player
checks retained here refer to that same UUID.

- Metadata duration: 61 seconds.
- FFprobe/browser duration: 61.012333 seconds; Qt: 61012 milliseconds.
- Original and server MP4: 57,378,248 bytes each; SHA-256
  `cf266d82cd0341d21df7db75b9456d1387fa5076d33b3ade5b536b5eed7101db`.
- FFprobe decoded frame count: 1,853; full FFmpeg decode returned no errors.
- Browser pause, seek, resume, and native fullscreen enter/exit checks passed.
- Camera and recording were stopped after capture.

The new consistent example replaces old duration screenshots; it does not prove
the cause of the historical 45/43-second discrepancy. The final stopped-service
snapshot contains reset writer state, not a second file-finalization test.

## Production And Independent Inspection

The session used local Playwright automation with Google Chrome and a C++
`QQuickWindow::grabWindow` harness loading the application's existing Qt/QML
resources. Application source was not changed. FFprobe, FFmpeg, SHA-256 and
authenticated read-only video API snapshots provided the recording checks.
These are saved execution records, not an independent rerun by a third party.

Run the supplied offline checker without camera, browser or network:

```bash
python3 -B data/ui-2026-10-31/verify.py
```

It rehashes PNGs, checks identity/size/duration/frame-count consistency, compares
the backend snapshots and verifies recorded control outcomes. It does not repeat
the browser actions, decode the withheld MP4, or measure streaming performance.

## Privacy

The MP4 remains private by the author's choice. Readers cannot independently
rehash or decode a file that is not supplied. [provenance.json](provenance.json)
records original/public hashes and the JSON projection: personal absolute
recording paths become clearly unavailable `withheld-recordings/` references.
Quantitative observations are unchanged. The capture-time restriction in the
original report is retained as historical metadata; the author subsequently
authorized this publication. No credentials, `.env`, private settings, DOCX,
database dumps or unrestricted logs are included.
