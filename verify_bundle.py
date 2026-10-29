"""Verify published bytes and invoke only offline, read-only evidence checks."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
manifest = json.loads((ROOT / 'artifact-manifest.json').read_text())
assert manifest['schema_version'] == 1
assert manifest['recording_media_policy'] == 'private_by_author_choice'
seen = set()
for entry in manifest['files']:
    name = entry['path']
    path = ROOT / name
    assert name not in seen and '..' not in Path(name).parts and not Path(name).is_absolute()
    assert path.is_file() and not path.is_symlink(), name
    data = path.read_bytes()
    assert len(data) == entry['bytes'], name
    assert hashlib.sha256(data).hexdigest() == entry['sha256'], name
    seen.add(name)
actual = {str(p.relative_to(ROOT)) for p in ROOT.rglob('*') if p.is_file() and '.git' not in p.parts}
assert actual == seen | {'artifact-manifest.json', 'SHA256SUMS.txt'}, sorted(actual ^ seen)
assert len(list((ROOT / 'data/optical/2026-10-22/images/local').glob('*.jpeg'))) == 31
assert len(list((ROOT / 'data/optical/2026-10-22/images/web').glob('*.jpeg'))) == 28
assert not list(ROOT.rglob('*.mp4'))
for args in [
    ['node', 'verify-optical.cjs', '.'],
    [sys.executable, '-B', 'scripts/verify-lan.py'],
    [sys.executable, '-B', 'scripts/verify-measurements.py'],
]:
    subprocess.run(args, cwd=ROOT, check=True)
print(f'PASS: {len(seen)} file hashes, 59 latency photos, LAN/server and F450/A/B arithmetic. MP4 media remains private.')
