"""Project only cited hardware/version fields from the retained private inventory."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import shlex


def project(source):
    cpu = {row['field'].rstrip(':'): row['data']
           for row in json.loads(source['cpu']['stdout'])['lscpu']}
    os_release = dict(line.split('=', 1) for line in source['os_release'].splitlines() if '=' in line)
    gpu = next(csv.reader(io.StringIO(source['gpu']['stdout']), skipinitialspace=True))
    versions = {name: source['software'][name]['stdout'].splitlines()[0]
                for name in ['node', 'npm', 'ffprobe', 'gstreamer', 'postgresql', 'chrome', 'cmake']}
    versions['Qt6'] = source['resolved_versions']['Qt6']
    versions['holoscan'] = source['resolved_versions']['holoscan']
    cuda = re.search(r'release (\d+\.\d+), V([\d.]+)', source['resolved_versions']['cuda_compiler']['stdout'])
    assert cuda is not None
    versions['cuda_compiler_release'], versions['cuda_compiler_version'] = cuda.groups()
    package_allowlist = {
        'backend-edge': ['@nestjs/core', '@prisma/client', 'typescript'],
        'backend-web': ['@nestjs/core', '@prisma/client', 'typescript'],
        'frontend-web': ['next', 'react', 'mediasoup-client', 'typescript'],
        'media-server': ['mediasoup', 'mediasoup-client'],
    }
    packages = {component: {name: source['packages'][component]['installed'][name] for name in names}
                for component, names in package_allowlist.items()}
    for values in packages.values():
        assert all(re.fullmatch(r'\d+(?:\.\d+)+(?:[-+][A-Za-z0-9.-]+)?', v) for v in values.values())
    configuration = {k: v for k, v in source['capture_configuration'].items() if k in {
        'STREAMING_CAMERA_WIDTH', 'STREAMING_CAMERA_HEIGHT', 'STREAMING_CAMERA_FRAME_RATE',
        'STREAMING_CAMERA_PIXEL_FORMAT', 'STREAMING_CAMERA_EXPOSURE_TIME', 'STREAMING_CAMERA_GAIN',
        'STREAMING_CAMERA_DYNAMIC_FRAME_RATE', 'MEDIA_PUBLISH_VIDEO_CODEC',
        'STREAMING_RECORD_WIDTH', 'STREAMING_RECORD_HEIGHT', 'STREAMING_RECORD_FPS',
        'STREAMING_RECORD_MODE', 'STREAMING_RECORD_BITRATE_KBPS'}}
    assert all(re.fullmatch(r'[A-Za-z0-9_.-]{0,30}', str(v)) for v in configuration.values())
    return {
        'schema_version': 1, 'host': 'EDGE_HOST', 'collected_utc': source['collected_utc'],
        'kernel': source['kernel'], 'os': shlex.split(os_release['PRETTY_NAME'])[0],
        'cpu': {k: cpu[k] for k in ['Architecture', 'Model name', 'CPU(s)', 'Core(s) per socket', 'Socket(s)']},
        'logical_cpu_count': source['logical_cpu_count'],
        'memory': {'MemTotal': source['memory']['MemTotal'],
                   'os_visible_gib': int(source['memory']['MemTotal'].split()[0]) / 1024**2},
        'gpu': {'model': gpu[0], 'driver_version': gpu[1], 'memory': gpu[2]},
        'software_versions': versions, 'installed_application_packages': packages,
        'capture_configuration': configuration,
        'limits': ['OS-visible RAM is not installed-module capacity.',
                   'Resolved Qt/CUDA versions supplement initially unavailable PATH commands.',
                   'This is a privacy projection of the 15 October inventory, not a fresh hardware scan.'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists()
    raw = args.input.read_bytes()
    result = project(json.loads(raw))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'source_sha256': hashlib.sha256(raw).hexdigest(),
                      'published_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
                      'projection': 'Only explicit CPU/GPU/memory/version/configuration fields; host alias; no process, network, home-path, environment or serial-number dump.'}))


if __name__ == '__main__':
    main()
