"""Offline checks for saved UI-session evidence, not a hardware test rerun."""
from pathlib import Path
import hashlib
import json

B=Path(__file__).resolve().parent
read=lambda name:json.loads((B/name).read_text())
v=read('verification.json')
for shot in v['screenshots']:
    data=(B/shot['file']).read_bytes()
    assert len(data)==shot['bytes'],shot['file']
    assert hashlib.sha256(data).hexdigest()==shot['sha256'],shot['file']
assert len(v['screenshots'])==8
assert len(v['copies'])==2 and v['byteIdentical']
assert v['copies'][0]['sha256']==v['copies'][1]['sha256']
assert v['copies'][0]['bytes']==v['copies'][1]['bytes']==57378248
assert v['metadataDurationSeconds']==61
assert abs(float(v['probe']['format']['duration'])-61.012333)<1e-6
assert int(v['probe']['format']['size'])==57378248
assert int(v['probe']['streams'][0]['nb_read_frames'])==1853
assert v['qt']['durationMs']==61012
assert abs(v['web']['duration']-61.012333)<1e-6
assert v['decodeErrors']==''
assert v['cameraStopped'] and v['recordingStopped']
assert all(v['web']['controls'].values())
assert read('qt-playback-check.json')==v['qt']
assert read('web-playback-check.json')==v['web']
record=read('recording.json')
assert record['id']==v['videoId'] and record['duration_seconds']==61
assert int(record['size_bytes'])==57378248 and int(record['frame_count'])==1853
for name in ('video-3000.json','video-3002.json'):
    snap=read(name)['video']
    assert snap==record,name
status=read('stream-final-status.json')
assert not status['streaming'] and not status['recording']
assert status['recordingStatus']['case_id']==v['videoId']
for entry in read('provenance.json')['files']:
    data=(B/entry['file']).read_bytes()
    assert hashlib.sha256(data).hexdigest()==entry['public_sha256']
    if entry['file'].endswith('.png'):assert entry['unchanged_bytes'] and entry['original_sha256']==entry['public_sha256']
assert not list(B.rglob('*.mp4'))
print('PASS: eight original PNG hashes; consistent saved backend/player/probe records; MP4 private. No live experiment was rerun.')
