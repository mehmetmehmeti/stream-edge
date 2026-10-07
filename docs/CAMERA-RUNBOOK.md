# Camera Demo and Verification

Validated locally on 2026-09-26 on the laptop with /dev/video0, RTX 3050,
Holoscan, Qt/QML and Chrome. This is a working single-laptop integration
deployment, not yet a validated remote production deployment.

## Start and stop

From the existing configured checkout:

```bash
cd /home/mehmet-work/Desktop/diploma/diploma-2026-stream-edge
bash scripts/stack.sh stop
bash scripts/stack.sh start
```

Cloud UI: http://localhost:3001. Qt opens automatically. Use the same account
on both applications for your recordings. Select the active stream in the
cloud viewer. Click Start in either application; click Record when live.
Stop Stream also finalizes an active recording. Wait for Completed before
shutting down services when preserving a recording is important.

Do not separately start another publisher, another camera capture, a
standalone media viewer, or the legacy C++ command executor. The media viewer
is embedded in the cloud frontend. The Qt preview uses the same camera frames
as the publisher, not a second /dev/video0 capture.

The start script launches development servers and assumes dependencies,
ignored .env files, database schemas and local build directories already
exist. It is not a fresh-machine installer. It deliberately leaves PostgreSQL
running when stopping the application processes.

For rebuilding after edits:

```bash
bash scripts/stack.sh stop
bash scripts/stack.sh build
bash scripts/stack.sh start
```

Logs and PID files are in ../.runtime:
media.log, stream.log, cloud.log, edge.log, web.log and qt.log.

## Components

| Component | Role | Local address |
| --- | --- | --- |
| frontend-edge | Qt/QML controls and OpenGL camera preview | Native window |
| backend-edge | Local auth, PostgreSQL, command executor, upload retries | 3002 |
| stream-edge | Holoscan camera graph, frame bridge, MP4 recording, WHIP publisher | 8081 |
| media-server | mediasoup WebRTC producer/consumer routing | 3010, media 40000 |
| backend-cloud | Cloud auth, replica, command creation, recording storage/playback | 3000 |
| frontend-cloud | Embedded live WebRTC and recorded MP4 viewers | 3001 |

PostgreSQL listens on 55432. diploma_edge and diploma_cloud are separate
databases with matching application schemas, not two names for one database.
The legacy streaming_service database remains for the C++ service's internal
tables. Users, streams and videos are the business entities; devices,
commands, inbox and outbox tables support execution and replication.

## Media path

1. Holoscan captures the laptop camera at 640x480, YUYV, 30 fps requested.
2. The frame bridge fans out to the recorder and GStreamer shared-memory export.
3. Qt reads RGBA frames from the shared-memory socket into an appsink and the
   internship-style QQuickFramebufferObject/OpenGL renderer.
4. A separate libsoup3 WHIP client encodes VP8 and publishes to mediasoup.
5. The Next.js viewer consumes that exact database stream UUID over WebRTC.

The separate publisher process avoids mixing libsoup2 and libsoup3 in the
camera/Qt process. Local MP4 recording uses H.264 independently of the live
VP8 browser stream. These are real camera frames, not the former test pattern.
The internship reference repositories were read, not modified; their camera,
frame bridge, recorder and Qt renderer patterns remain the reference.

Important streaming-service/.env settings are reflected in .env.example:

```dotenv
STREAMING_SOURCE_TYPE=camera
STREAMING_ENABLE_PREVIEW=0
STREAMING_CAMERA_DEVICE=/dev/video0
STREAMING_CAMERA_WIDTH=640
STREAMING_CAMERA_HEIGHT=480
STREAMING_CAMERA_FRAME_RATE=30
STREAMING_CAMERA_PIXEL_FORMAT=YUYV
STREAMING_RECORD_WIDTH=640
STREAMING_RECORD_HEIGHT=480
CLOUD_PUBLISH_ENABLED=1
CLOUD_PUBLISH_REQUIRED=1
EDGE_COMMAND_SYNC_ENABLED=0
MEDIA_PUBLISH_VIDEO_CODEC=vp8
MEDIA_PUBLISH_STREAM_NAME=
MEDIA_PUBLISH_SESSION_ID=
MEDIA_PUBLISH_VIEW_URL=http://localhost:3001
```

Keep the two name fields empty: each Start command supplies its stream UUID.
Keep EDGE_COMMAND_SYNC_ENABLED=0: the Node edge backend is the sole executor.
No custom publisher command template is required for the camera.
The camera's advertised 720p YUYV mode is only 10 fps; changing width/height
alone does not establish a 720p30 capture pipeline.

## Synchronization and recordings

An application mutation and its outbox event commit in one database transaction.
WebSocket delivery is retried until acknowledged. The receiving inbox deduplicates
events and rejects stale entity versions. A monotonic local sequence orders
outbox delivery and command execution, including recording-stop before stream-stop.
This is application-level database replication, not PostgreSQL physical replication.

Only the edge backend claims and executes commands. Claimed commands interrupted
by an executor restart are failed explicitly rather than blindly replayed.
Commands queued while the edge backend is offline execute when it reconnects.
Device and stream state are reconciled against the actual camera service.

Recordings are finalized locally before upload. The edge uploader retries a
streaming PUT to the cloud backend. The cloud checks SHA-256, stores the file
in its recordings directory, then publishes completed metadata back to edge.
Cloud playback uses an owner-scoped, expiring ticket and supports HTTP byte
ranges. A database row alone does not make a remote local file playable.
The old public application endpoint for declaring arbitrary paths uploaded was
removed; completion comes from a verified file transfer.

For older configured databases, apply prisma/queue-order.sql in each backend
to its own database before building. This adds the ordering columns without
resetting users or recordings. Generate both Prisma clients afterward.

## Queue reset

The old command queues were cleared after database backups. Subsequent test
commands remain as history; Completed and Failed history is not pending work.

```bash
bash scripts/reset-commands.sh --yes
bash scripts/stack.sh start
```

This is a destructive command-history reset, not the normal restart command.
It stops the stack, backs up all three databases under ../.runtime, clears
command queues, and closes interrupted stream/recording metadata. It preserves
users and video files. Do not run during a recording you need to finalize.

## Repeatable checks

Set THESIS_TEST_EMAIL and THESIS_TEST_PASSWORD in your shell to a dedicated
test account that exists on both replicas. Credentials are not stored in the
scripts. They start/stop the real camera and create short recordings;
do not run them during a recording session you want to keep uninterrupted.

```bash
ctest --test-dir streaming-service/build --output-on-failure
node scripts/verify-workflow.cjs
node scripts/verify-recovery.cjs
```

Workflow checks: login on both replicas; cloud Start; exact WHIP stream UUID;
duplicate Start rejection; edge recording; cloud Stop Recording; file checksums;
206 range playback; automatic recording finalization on Stop Stream; camera
release; restart with a new stream UUID.

Recovery checks: queued cloud Stop while edge is offline; stale/duplicate command
event rejection; recording upload after a cloud backend outage; matching file
metadata and frame counts in both databases. The restart harness explicitly
loads each backend's own environment to prevent inheriting the other database URL.

The browser check uses Playwright and installed Chrome. Start a stream first,
or set START_STREAM=1, and set PLAYWRIGHT_MODULE to an installed Playwright package:

```bash
PLAYWRIGHT_MODULE=/path/to/node_modules/playwright START_STREAM=1 node scripts/verify-cloud-camera.cjs
```

It checks increasing decoded frame counts for over 1,800 frames, nonblank pixels,
recorded playback and desktop/mobile screenshots. Qt's --verify-camera option
checks live frames and writes a screenshot; DIPLOMA_TEST_START=1 invokes the same
QML action as the Start button. It requires DIPLOMA_TEST_AUTH_TOKEN for the local
test account and optional DIPLOMA_TEST_OUTPUT for its output prefix.

Evidence is under ../.runtime: workflow-result.json, recovery-result.json,
cloud-camera-result.json, qt-camera.json and camera screenshots. These contain
local camera imagery and should not be committed or published without consent.

## Fixed failure causes

- Camera publishing did not consistently use the synchronized stream UUID.
- Qt displayed a generated pattern rather than consuming the shared camera frames.
- WHIP ICE consent expired; the client now enables libnice keepalive checks.
- Viewer reconnect cleanup could delete the wrong session or retain a dead one.
- Outbox acknowledgments could race with SENT writes; cloud flush could miss
  events emitted before transaction commit.
- Timestamp ties reordered dependent commands; stale updates could revive old
  command states; BigInt recording metadata was lost during serialization.
- Recording files lacked a complete retryable upload and browser playback path.

ICE references:
[mediasoup transport API](https://mediasoup.org/documentation/v3/mediasoup/api/)
and [libnice NiceAgent properties](https://libnice.freedesktop.org/libnice/NiceAgent.html).

## Remaining thesis work

- Deploy edge and cloud on separate machines; validate real network routing,
  advertised media address, firewall, HTTPS/WSS, TURN and reconnect under loss.
  Loopback URLs in this local setup are not remote deployment settings.
- Measure capture-to-display latency p50/p95, CPU/GPU, memory, bitrate, dropped
  frames, command/sync latency and recovery time; compare justified baselines.
  Working video alone is not evidence of optimized performance.
- Complete authorization across users, streams, commands and devices before
  exposure to untrusted users. Recording listing/playback is owner-scoped, but
  the overall app still assumes a trusted single-device demo environment.
- Add replica bootstrap/resnapshot and a deterministic conflict policy for
  simultaneous offline edits, duplicate-email registration and deletion/tombstones.
  Event replay tests do not prove arbitrary multi-writer convergence.
- Validate long recordings, disk-full behavior, camera unplug, service crashes,
  multi-viewer load and multi-device routing. Upload currently retries whole files,
  not resumable chunks.
- Pin/document dependencies for fresh-machine reproducibility and package
  services for supervised startup. Keep secrets out of Git; rotate any credentials
  previously shared in chat. Do not expose the local demo service tokens publicly.

Five C++ tests pass with assertions enabled in Release builds. Backend builds and
cloud frontend type checking pass. Static Qt still emits an unrelated ComboBox
image-plugin warning; it does not prevent live OpenGL preview.
