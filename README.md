# diploma-2026-stream-edge

Edge media execution layer for the diploma project.

This repository now carries the streaming pipeline copied from the internship
stream-edge repository and adapted for the diploma architecture. The Node edge
backend owns users, streams, videos, commands, and sync metadata; this C++
service owns actual media execution: Holoscan source processing, stream start
and stop, MP4 recording, frame export, and optional WHIP publishing to the
local mediasoup server.

## Pipeline

Cloud operator action:

```text
frontend-cloud -> backend-cloud -> database sync -> backend-edge
  -> streaming-service HTTP command -> Holoscan/GStreamer pipeline
  -> mediasoup WHIP stream and/or local MP4 recording
```

The important copied internship pieces are:

- `streaming-service/src/streaming/HoloscanVideoPipeline.cc`
- `streaming-service/src/streaming/HoloscanVideoApplication.cc`
- `streaming-service/src/streaming/RecordingOp.cc`
- `streaming-service/src/streaming/GStreamerMp4Writer.cc`
- `streaming-service/src/services/StreamingService.cc`
- `streaming-service/src/services/CloudPublisher.cc`
- `streaming-service/src/controllers/StreamingController.cc`

## HTTP Control API

The edge backend calls these endpoints by default through
`STREAM_EDGE_CONTROL_BASE_URL=http://127.0.0.1:8081`.

```http
GET  /health
GET  /stream/status
GET  /record/status
POST /stream/start
POST /stream/stop
POST /record/start
POST /record/stop
```

Example command payload:

```json
{
  "command_id": "uuid",
  "case_id": "stream-or-video-id",
  "device_id": "edge-dev-001",
  "stream_id": "uuid",
  "video_id": "uuid",
  "metadata_managed_by_client": true
}
```

`metadata_managed_by_client` keeps database ownership in the Node edge/cloud
backends while this service performs the media work.

## Local Setup

The project intentionally does not use Docker for the diploma version.

```bash
cd streaming-service
cp .env.example .env
./install-project-cmake.sh
STREAM_EDGE_EXTRA_PREFIX_PATH="/home/mehmet-work/wp-edge-core/build/vcpkg_installed/x64-linux:$HOME/.local:/opt/nvidia/holoscan" ./build-local.sh
./start-local-postgres.sh
./run-local.sh
```

The service expects Holoscan, CUDA, Drogon, TinyORM, Qt runtime pieces, and
GStreamer development packages to be installed locally, matching the internship
pipeline requirements.

`STREAM_EDGE_EXTRA_PREFIX_PATH` is a convenience wrapper around
`CMAKE_PREFIX_PATH`; it lets the diploma repo reuse the already-built
internship/vcpkg native dependencies without committing those binaries here.

`start-local-postgres.sh` creates and starts a user-owned PostgreSQL instance
on port `55432`, which avoids depending on system Postgres or Docker. Copy the
DB values it prints into `streaming-service/.env`.

## Source Configuration

For file replay:

```env
STREAMING_SOURCE_TYPE=file
STREAMING_SOURCE_DIR=/absolute/path/to/local/video/source
STREAMING_SOURCE_BASENAME=stream_basename
STREAMING_RECORD_WIDTH=1280
STREAMING_RECORD_HEIGHT=720
STREAMING_RECORD_FPS=30
```

For camera input:

```env
STREAMING_SOURCE_TYPE=camera
STREAMING_CAMERA_DEVICE=/dev/video0
STREAMING_CAMERA_WIDTH=1280
STREAMING_CAMERA_HEIGHT=720
STREAMING_CAMERA_FRAME_RATE=30
```

## Mediasoup Publishing

Run the diploma media server separately:

```bash
cd ../diploma-media-server
HTTP_PORT=3010 ANNOUNCED_ADDRESS=127.0.0.1 npm run server
```

Enable publishing in `streaming-service/.env`:

```env
CLOUD_PUBLISH_ENABLED=1
CLOUD_PUBLISH_REQUIRED=1
MEDIA_PUBLISH_WHIP_URL=http://127.0.0.1:3010/whip
MEDIA_PUBLISH_VIEW_URL=http://127.0.0.1:5173
```

The copied internship publisher exports Holoscan frames through shared memory
and launches the configured external WHIP command. For the diploma project the
template is generic `MEDIA_PUBLISH_COMMAND_TEMPLATE`, with the old service
provider removed from the documentation and runtime naming.

For a simple MP4 demo stream, use the repo-local libsoup3 WHIP client from
`diploma-media-server` and point stream-edge at a sample file:

```env
STREAMING_SOURCE_TYPE=mp4
STREAMING_SOURCE_FILE=/home/mehmet-work/Desktop/diploma/diploma-2026-stream-edge/videos/sample.mp4
MEDIA_PUBLISH_WHIP_CLIENT=/home/mehmet-work/Desktop/diploma/diploma-media-server/.tools/bin/whip-client
MEDIA_PUBLISH_LOOP_FILE=1
```

In MP4 mode stream-edge appends the command/case ID to the WHIP base URL, so a
cloud command for `case_id=demo` publishes to `/whip/demo` and the cloud viewer
opens stream `demo`.

## Recording

Recording is controlled through `/record/start` and `/record/stop`. Files are
written locally as MP4 and the response includes `recordingStatus`, which the
edge backend stores on the matching `videos.edge_local_path` record before
syncing that metadata to the cloud database.
