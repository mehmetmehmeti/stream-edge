"""Offline schema/hash checks only; never starts thesis services."""
from pathlib import Path
import hashlib
import json
import math

ROOT = Path(__file__).resolve().parent
manifest = json.loads((ROOT / 'manifest.json').read_text())
assert len(manifest['reports']) == 4
data = {}
for item in manifest['reports']:
    path = ROOT / item['path']
    raw = path.read_bytes()
    assert path.parent == ROOT and path.name.endswith('.json')
    assert len(raw) == item['size_bytes'], path.name
    assert hashlib.sha256(raw).hexdigest() == item['sha256'], path.name
    data[path.name] = json.loads(raw)

workflow = data['workflow-result.json']
assert set(workflow) == {'results', 'streamId', 'videoId', 'probe'}
assert len(workflow['results']) == 7
stream = workflow['probe']['streams'][0]
assert (stream['codec_name'], stream['width'], stream['height'], stream['nb_frames']) == ('h264', 640, 480, '183')
assert math.isclose(float(workflow['probe']['format']['duration']), 6.020667)
recovery = data['recovery-result.json']
assert set(recovery) == {'results'} and len(recovery['results']) == 3
browser = data['cloud-camera-result.json']
assert set(browser) == {'first', 'last', 'recording', 'errors'}
assert browser['first'] == {'width': 640, 'height': 480, 'time': 1.03, 'frames': 11}
assert browser['last'] == {'width': 640, 'height': 480, 'frames': 611, 'pixelRange': 30}
assert browser['recording'] == {'width': 640, 'height': 480, 'duration': 3.936333}
assert browser['errors'] == []
assert data['qt-camera.json'] == {'live': True, 'renderedFrames': 305}
print('PASS: four report hashes, sizes, schemas and historical values; no services contacted.')
