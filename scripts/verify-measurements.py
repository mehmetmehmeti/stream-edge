"""Offline measurement verifier for standalone or integrated public packages."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

ROOT = Path(__file__).resolve().parents[1]
F = ROOT / 'data/f450/20261015T173020Z'
A = ROOT / 'data/buffering-ab/measured-20261026-01'
CHECKS = 0
INTEGRATED = False
INTEGRATED_PATHS = {
    'provenance.json': 'provenance/measurement-stage-provenance.json',
    'recording-attestation.json': 'data/recording-attestation.json',
    'verification-result.json': 'provenance/measurement-stage-verification-result.json',
}


def owned_path(label):
    relative = Path(INTEGRATED_PATHS.get(label, label) if INTEGRATED else label)
    assert not relative.is_absolute() and '..' not in relative.parts, label
    file = ROOT / relative
    assert file.resolve().is_relative_to(ROOT), ('Measurement path escapes package', label)
    return file


def read(path):
    return json.loads(path.read_text())


def rows(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def check(actual, expected, label):
    global CHECKS
    if isinstance(actual, dict) and isinstance(expected, dict):
        assert actual.keys() == expected.keys(), (label, actual.keys(), expected.keys())
        for k in actual:
            check(actual[k], expected[k], label + '.' + k)
        return
    if isinstance(actual, (int, float)) and not isinstance(actual, bool) and isinstance(expected, (int, float)):
        assert math.isclose(actual, expected, rel_tol=1e-11, abs_tol=1e-9), (label, actual, expected)
    else:
        assert actual == expected, (label, actual, expected)
    CHECKS += 1


def stats(values):
    a = sorted(x for x in values if x is not None and math.isfinite(x))
    return dict(n=len(a), mean=statistics.mean(a) if a else None, p50=statistics.median(a) if a else None,
                p95=a[math.ceil(.95*len(a))-1] if a else None, min=min(a) if a else None, max=max(a) if a else None)


def web(first, last):
    seconds = (last['elapsed_ms'] - first['elapsed_ms']) / 1000
    assert seconds > 0
    assert first['rtp']['id'] == last['rtp']['id']
    delta = lambda key: last[key] - first[key]
    rt = lambda key: last['rtp'][key] - first['rtp'][key]
    return dict(interval_s=seconds, presented_frames=delta('presented_frames'), presented_fps=delta('presented_frames')/seconds,
                decoded_frames=rt('framesDecoded'), decoded_fps=rt('framesDecoded')/seconds, received_frames=rt('framesReceived'),
                playback_total_frames=delta('playback_total_frames'), playback_dropped_frames=delta('playback_dropped_frames'),
                playback_drop_percent=100*delta('playback_dropped_frames')/delta('playback_total_frames'),
                received_rtp_mbps=rt('bytesReceived')*8/seconds/1e6, packets_lost_delta=rt('packetsLost'), rtp_counter_continuity=True)


def qt(samples, start, end, live=False):
    selected = [x for x in samples if start <= x['utc_ms'] <= end and x.get('live' if live else 'preview_present')]
    first, last = selected[0], selected[-1]
    seconds = (last['elapsed_ns'] - first['elapsed_ns'])/1e9
    return seconds, last['rendered_frames']-first['rendered_frames'], first, last


def resource(samples, start, end, complete=False):
    return [x for x in samples if x['valid_cpu_interval'] and start <= x['utc_ms'] <= end
            and (not complete or x['utc_ms']-x['interval_s']*1000 >= start)]


def verify_f450():
    result, summary = read(F/'benchmark-result.json'), read(F/'summary.json')
    samples, resources, frames = rows(F/'samples.jsonl'), rows(F/'resources.jsonl'), rows(F/'qt.jsonl')
    check(result['status'], 'passed', 'F450 status')
    check(len(result['conditions']), 7, 'F450 conditions')
    for raw, saved in zip(result['conditions'], summary['conditions']):
        name = raw['name']
        observations = [x['web'] for x in samples if x['condition'] == name]
        for k, v in web(observations[0], observations[-1]).items():
            check(v, raw[k], name+'.browser.'+k)
        seconds, count, first, last = qt(frames, raw['utc_start_ms'], raw['utc_end_ms'])
        check(count/seconds, saved['qt']['fps'], name+'.qt.fps')
        check(seconds, saved['qt']['interval_s'], name+'.qt.seconds')
        for k in ('invalid_frames', 'interop_failures', 'bridge_dropped_frames'):
            check(last[k]-first[k], saved['qt'][k], name+'.qt.'+k)
        usage = resource(resources, raw['utc_start_ms'], raw['utc_end_ms'], True)
        cpu = [sum(c['cpu_percent'] for c in x['components'].values()) for x in usage]
        rss = [sum(c['rss_bytes'] for c in x['components'].values())/1024**2 for x in usage]
        check(stats(cpu), saved['total_cpu_percent'], name+'.cpu')
        check(stats(rss), saved['total_rss_mib'], name+'.rss')
        gpu = [list(map(float, line.split(','))) for x in usage if x.get('gpu_csv') for line in x['gpu_csv'].splitlines()]
        check(stats([x[0] for x in gpu]), saved['system_gpu_percent'], name+'.gpu')
        check(stats([x[1] for x in gpu]), saved['system_gpu_memory_mib'], name+'.gpu_memory')
    mapping = {'web_presented_fps':lambda x:x['presented_fps'], 'qt_rendered_fps':lambda x:x['qt']['fps'],
               'received_rtp_mbps':lambda x:x['received_rtp_mbps'], 'total_cpu_percent':lambda x:x['total_cpu_percent']['mean'],
               'total_rss_mib':lambda x:x['total_rss_mib']['mean'], 'system_gpu_percent':lambda x:x['system_gpu_percent']['mean'],
               'system_gpu_memory_mib':lambda x:x['system_gpu_memory_mib']['mean']}
    for label, enabled in [('live_only', False), ('live_record', True)]:
        selected = [x for x in summary['conditions'] if x['recording'] == enabled and x['name'] != 'long-recording']
        check(len(selected), 3, label+'.replicates')
        for k, fn in mapping.items():
            check(stats([fn(x) for x in selected]), summary['groups'][label][k], label+'.'+k)
    for k in summary['comparisons']:
        baseline, recorded = (summary['groups'][g][k]['mean'] for g in ('live_only','live_record'))
        check(recorded-baseline, summary['comparisons'][k]['record_minus_baseline'], k+'.difference')
        check((recorded-baseline)/baseline*100, summary['comparisons'][k]['percent_change'], k+'.percent')
    for rec, transfer in zip(result['recordings'], rows(F/'transfers.jsonl')):
        check(rec['transfer_timing'], transfer, rec['id']+'.transfer')
        check(rec['sha256_edge'], rec['sha256_server'], rec['id']+'.historical_pair_hash')
        check(rec['recorder_diagnostics']['received_frames'], rec['recorder_diagnostics']['written_frames'], rec['id']+'.recorder_count')
    check(round(summary['groups']['live_only']['total_cpu_percent']['mean'],3),58.745,'reported off CPU')
    check(round(summary['groups']['live_record']['total_cpu_percent']['mean'],3),206.528,'reported on CPU')


def native_probe(records):
    starts=[x for x in records if x['event']=='measurement_start']; ends=[x for x in records if x['event']=='measurement_end']
    assert len(starts)==len(ends)==1
    assert not any(x['event']=='safety_abort' for x in records)
    start,end=starts[0],ends[0]
    inputs=[x for x in records if x['event']=='input']; by_id={x['id']:x for x in inputs}
    assert len(by_id)==len(inputs)==len({x['pts_ns'] for x in inputs})
    for event in ('queue_output','consumer_output','encoded'):
        output=[x for x in records if x['event']==event]
        for i,x in enumerate(output):
            assert x['matched'] and x['pts_ns']==by_id[x['id']]['pts_ns'] and x['entered_us']==by_id[x['id']]['mono_us']
            assert x['mono_us']>=x['entered_us'] and (i==0 or x['id']>output[i-1]['id'])
    measured=[x for x in records if start['mono_us']<=x['mono_us']<end['mono_us']]
    out=[x for x in measured if x['event']=='queue_output']; enc=[x for x in measured if x['event']=='encoded']; qs=[x for x in measured if x['event']=='queue_sample']
    seconds=(end['mono_us']-start['mono_us'])/1e6
    skipped=out[-1]['id']-out[0]['id']+1-len(out)
    return dict(interval_s=seconds,start_mono_us=start['mono_us'],end_mono_us=end['mono_us'],start_utc_ms=start['utc_us']/1000,end_utc_ms=end['utc_us']/1000,
                queue_wait_ms=stats([(x['mono_us']-x['entered_us'])/1000 for x in out]),
                input_to_encoded_ms=stats([(x['mono_us']-x['entered_us'])/1000 for x in enc]),
                queue_buffers=stats([x['buffers'] for x in qs]),queue_bytes=stats([x['bytes'] for x in qs]),
                publisher_rss_mib=stats([x['rss_bytes']/1048576 for x in qs]),queue_output_fps=len(out)/seconds,encoded_fps=len(enc)/seconds,
                intentionally_skipped_between_observed_output_ids=skipped,skipped_percent_between_observed_output_ids=100*skipped/(out[-1]['id']-out[0]['id']+1),
                input_counter_delta=end['input_count']-start['input_count'],output_counter_delta=end['output_count']-start['output_count'])


def verify_ab():
    result,analysis=read(A/'result.json'),read(A/'analysis.json')
    frames,resources=rows(A/'qt.jsonl'),rows(A/'resources.jsonl')
    check(len(result['trials']),12,'AB trials');check(len(result['commands']),48,'AB commands')
    computed=[]
    for t,saved in zip(result['trials'],analysis['trials']):
        records=rows(A/t['probe_log']); got=native_probe(records)
        for k,v in got.items():check(v,t['probe'][k],str(t['index'])+'.native.'+k)
        cfg=t['probe']['configuration']
        check(cfg['max_buffers'],2 if t['profile']=='latest' else 0,'queue bound');check(cfg['leaky'],2 if t['profile']=='latest' else 0,'leaky')
        check(cfg['consumer_sleep_us'],100000 if t['load']=='slow' else 0,'injected slowdown')
        usage=resource(resources,t['utc_start_ms'],t['utc_end_ms'])
        cpu=stats([sum(c['cpu_percent'] for c in x['components'].values()) for x in usage]);rss=stats([sum(c['rss_bytes'] for c in x['components'].values())/1048576 for x in usage])
        check(cpu,saved['resources']['cpu_percent'],'AB CPU');check(rss,saved['resources']['rss_mib'],'AB RSS')
        sec,count,_,_=qt(frames,t['utc_start_ms'],t['utc_end_ms'],True)
        check(count/sec,saved['qt']['rendered_fps'],'AB Qt')
        check(stats(t['browser_presentation_gaps_ms']),saved['browser_presentation_gap_ms'],'AB gap arrays')
        pipe=read((A/t['probe_log']).parent/'pipeline.json')
        check(hashlib.sha256(pipe['pipeline'].encode()).hexdigest(),pipe['published_pipeline_sha256'],'published pipeline hash')
        computed.append(dict(t,resources=saved['resources'],qt=saved['qt'],probe=got))
    mapping={'queue_p50_ms':lambda t:t['probe']['queue_wait_ms']['p50'],'queue_p95_ms':lambda t:t['probe']['queue_wait_ms']['p95'],
             'encoded_slice_p95_ms':lambda t:t['probe']['input_to_encoded_ms']['p95'],'browser_fps':lambda t:t['web']['presented_fps'],
             'publisher_rss_mib':lambda t:t['probe']['publisher_rss_mib']['mean'],'cpu_percent':lambda t:t['resources']['cpu_percent']['mean'],
             'summed_rss_mib':lambda t:t['resources']['rss_mib']['mean'],'qt_fps':lambda t:t['qt']['rendered_fps'],
             'browser_gap_p95_ms':lambda t:stats(t['browser_presentation_gaps_ms'])['p95']}
    for group in analysis['groups']:
        ts=[t for t in computed if t['load']==group['load'] and t['profile']==group['profile']]
        check(len(ts),3,'AB condition replicates')
        for k,fn in mapping.items():check(stats([fn(t) for t in ts]),group[k],'AB group '+k)
        check(sum(t['probe']['intentionally_skipped_between_observed_output_ids'] for t in ts),group['skipped_frames'],'AB skipped IDs')
    fifo=next(g['queue_p95_ms']['mean'] for g in analysis['groups'] if g['load']=='slow' and g['profile']=='fifo')
    latest=next(g['queue_p95_ms']['mean'] for g in analysis['groups'] if g['load']=='slow' and g['profile']=='latest')
    check(round(fifo,3),531.363,'reported FIFO p95');check(round(latest,3),129.844,'reported latest p95')
    check(round((fifo-latest)/fifo*100,3),75.564,'reported p95 reduction')
    build=read(ROOT/'data/buffering-ab/build-provenance.json')
    check(build['probe_sha256'],result['source_hashes']['ab-probe.c'],'historical probe source hash')
    check(build['header_sha256'],result['source_hashes']['ab-probe.h'],'historical probe header hash')
    check(build['upstream_sha256'],'c1dc9cccee104cc2b36657d757279080974f8dfb51c94a2352fc95685593f6fc','historical modified WHIP source hash')


def verify_preflight():
    p=ROOT/'data/camera-preflight'; result=read(p/'result.json'); buffers=rows(p/'capture-buffers.jsonl')
    valid=[x for x in buffers if not x['driver_error'] and x['bytesused']==1802240 and x['timestamp_s']>=buffers[0]['timestamp_s']+result['warmup_s']]
    fps=(len(valid)-1)/(valid[-1]['timestamp_s']-valid[0]['timestamp_s'])
    check(fps,result['observed_fps'],'preflight FPS');check(len(valid),result['measured_buffers'],'preflight buffers')


def verify_privacy_provenance():
    provenance=owned_path('provenance.json')
    ledger=read(provenance);expected=set()
    for entry in ledger['files']:
        rel=Path(entry['published_path']);assert not rel.is_absolute() and '..' not in rel.parts
        file=owned_path(entry['published_path']);expected.add(file.resolve());check(hashlib.sha256(file.read_bytes()).hexdigest(),entry['published_sha256'],'published SHA256 '+str(rel))
        if entry['source_path']:assert not Path(entry['source_path']).is_absolute()
    if not INTEGRATED:
        actual={p.resolve() for p in ROOT.rglob('*') if p.is_file() and p.name != 'provenance.json'}
        assert actual==expected,('Untracked public files',actual-expected,expected-actual)
    for p in expected|{provenance}:
        if INTEGRATED and p.relative_to(ROOT).parts[0] != 'data':continue
        text=p.read_text()
        # Verifier source contains its own detection expressions; data must not contain these patterns.
        if p==Path(__file__).resolve():continue
        assert not re.search(r'/home/|https?://|wss?://|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}|\b(?:\d{1,3}\.){3}\d{1,3}\b|-----BEGIN .*PRIVATE KEY',text),p
    attest=read(owned_path('recording-attestation.json'));check(len(attest['recordings']),46,'attestation count')
    for r in attest['recordings']:
        check(r['historical_sha256'],r['current_sha256'],'attested hash');check(r['historical_bytes'],r['current_bytes'],'attested bytes')
    assert not any(p.suffix.lower()=='.mp4' for p in expected)


def main():
    global INTEGRATED
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--integrated',action='store_true',help='Use parent package paths and validate only measurement-owned manifest entries.')
    args=parser.parse_args()
    INTEGRATED=args.integrated or (ROOT/INTEGRATED_PATHS['provenance.json']).is_file()
    verify_f450();verify_ab();verify_preflight();verify_privacy_provenance()
    print(json.dumps({'status':'passed','checks':CHECKS,'layout':'integrated' if INTEGRATED else 'standalone','scope':'Public-only offline arithmetic and measurement-owned derivative hashes; no experiments or decoding.',
                      'limitations':['Media withheld: cannot publicly redo decoding or raw MP4 hashes.','A/B browser terminal endpoints missing; saved browser aggregates are not independently endpoint-recomputed.']}))


if __name__=='__main__':main()
