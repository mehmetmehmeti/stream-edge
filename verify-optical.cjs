'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {spawn} = require('node:child_process');

const BASE = 'data/optical/2026-10-22';
const ANALYZER = 'thesis-documentation/evaluation/analyze-optical-latency.cjs';
const MANIFEST = `${BASE}/manifest.json`;
const HEADERS = ['set', 'image_number', 'filename', 'source_path', 'sha256',
  'file_bytes', 'source_clock', 'streamed_clock', 'source_ms', 'streamed_ms',
  'status', 'latency_ms', 'repeated_pair_of', 'note'];
const PINS = Object.freeze({
  input_semantic: '898de63bc91c389c5230bc8868ba1a5de0a803d8ea6ce1677d908f6be67e91d2',
  csv_semantic: 'f59889ab1bd07d9eb678a78031c47f1c513b09ef9c513b86dc30a30fa34d37bd',
  originals: {
    [`${BASE}/readings-input.json`]: 'b901b46012eb9e2a78e4b57b0f5c47639979197c6d64c0fdd53badcdaa65016b',
    [`${BASE}/readings.csv`]: 'bc1adb4ce042c4dd4e3422522c480d56ef778dc18f62d45ad7a96c479a8047a1',
    [`${BASE}/summary.json`]: '2731226710ccdcdbd8296a51d2e86cdfb23a9fb957f134ceacd2b7f4278e0965',
    [`${BASE}/OPTICAL_LATENCY_REPORT_2026-10-22.md`]: 'b2e35231e9795af8813250451a631d4f83578de03402cc5c1bfdacaad6317912',
    [ANALYZER]: 'f926191db6342d86698e27ee6cb9ec2876a8c987aaa071c6d31c30f233ffffc8'
  }
});
const EXCLUSIONS = {local: [1, 4, 13, 18, 21, 25], web: [1, 3, 11, 13, 16, 20, 24]};
const COUNTS = {local: 31, web: 28};
const HASH = /^[a-f0-9]{64}$/;
const UUID = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const plain = value => JSON.parse(JSON.stringify(value));
const text = bytes => new TextDecoder('utf-8', {fatal: true}).decode(bytes);
const cell = value => String(value ?? '');

function stable(value) {
  if (Array.isArray(value)) return value.map(stable);
  if (value !== null && typeof value === 'object') {
    return Object.fromEntries(Object.keys(value).sort().map(k => [k, stable(value[k])]));
  }
  return value;
}

function semanticHash(value) {
  return sha(JSON.stringify(stable(value)));
}

function relativePath(value) {
  assert(typeof value === 'string' && value.length > 0, 'Empty package path');
  assert(!value.includes('\\') && !value.includes('\0') && !value.includes(':'), 'Invalid package path');
  assert(!path.posix.isAbsolute(value), 'Absolute package path');
  assert(value.split('/').every(p => p && p !== '.' && p !== '..'), 'Path traversal or noncanonical path');
  return value;
}

function diskPackage(directory) {
  const requested = path.resolve(directory);
  assert(!fs.lstatSync(requested).isSymbolicLink(), 'Package root is a symlink');
  const root = fs.realpathSync(requested);
  assert(fs.statSync(root).isDirectory(), 'Package root is not a directory');
  function locate(relative, directoryExpected = false) {
    const parts = relativePath(relative).split('/');
    let current = root;
    for (let i = 0; i < parts.length; i++) {
      current = path.join(current, parts[i]);
      const stat = fs.lstatSync(current);
      assert(!stat.isSymbolicLink(), `Symlink rejected: ${relative}`);
      const isDirectory = i < parts.length - 1 || directoryExpected;
      assert(isDirectory ? stat.isDirectory() : stat.isFile(), `Not a regular package entry: ${relative}`);
    }
    return current;
  }
  return {
    read: relative => fs.readFileSync(locate(relative)),
    entries: relative => fs.readdirSync(locate(relative, true)),
    absolute: relative => path.join(root, relativePath(relative))
  };
}

// The stdlib CSV parser handles quoted commas, escaped quotes and embedded newlines.
const CSV_PYTHON = String.raw`
import csv, io, json, sys
csv.field_size_limit(1024 * 1024)
reader = csv.reader(io.StringIO(sys.stdin.buffer.read().decode('utf-8'), newline=''), strict=True)
headers = next(reader, None)
if headers is None or len(headers) != len(set(headers)):
    raise ValueError('Missing or duplicate CSV headers')
rows = []
for values in reader:
    if len(values) != len(headers):
        raise ValueError('Wrong CSV field count at line ' + str(reader.line_num))
    rows.append(dict(zip(headers, values)))
print(json.dumps({'headers': headers, 'rows': rows}, ensure_ascii=True))
`;

async function parseCSV(bytes) {
  const input = text(bytes);
  const stdout = await new Promise((resolve, reject) => {
    const child = spawn('python3', ['-I', '-B', '-c', CSV_PYTHON], {stdio: ['pipe', 'pipe', 'pipe']});
    const output = [];
    const errors = [];
    let length = 0;
    const timer = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error('Python CSV parser timed out'));
    }, 15000);
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.stdin.on('error', error => {
      if (error.code !== 'EPIPE') { child.kill('SIGKILL'); reject(error); }
    });
    for (const [stream, target] of [[child.stdout, output], [child.stderr, errors]]) {
      stream.on('data', chunk => {
        length += chunk.length;
        if (length > 4 * 1024 * 1024) {
          child.kill('SIGKILL');
          reject(new Error('Python CSV parser output limit exceeded'));
        } else target.push(chunk);
      });
    }
    child.on('close', code => {
      clearTimeout(timer);
      if (code !== 0) reject(new Error(`CSV parse failed: ${Buffer.concat(errors).toString('utf8').trim()}`));
      else resolve(Buffer.concat(output).toString('utf8'));
    });
    child.stdin.end(input);
  });
  const parsed = JSON.parse(stdout);
  assert.deepEqual(parsed.headers, HEADERS, 'CSV header schema differs');
  return parsed.rows;
}

function loadAnalyzer(source, readImage) {
  const moduleObject = {exports: {}};
  const allowed = new Set(['node:path', 'node:crypto', 'node:assert/strict']);
  function restrictedRequire(name) {
    if (name === 'node:fs') return Object.freeze({readFileSync: readImage});
    assert(allowed.has(name), `Unexpected analyzer import: ${name}`);
    return require(name);
  }
  // No require.main/process or filesystem write API: import exports, never CLI main().
  vm.runInNewContext(text(source), {require: restrictedRequire, module: moduleObject},
    {filename: ANALYZER, timeout: 5000});
  assert.equal(typeof moduleObject.exports.analyze, 'function');
  assert.equal(typeof moduleObject.exports.report, 'function');
  return moduleObject.exports;
}

function checkRows(rows, generated, io, records) {
  assert.equal(rows.length, 59, 'CSV must contain all 59 photos, including exclusions');
  const keyed = new Map();
  for (const row of rows) {
    assert(['local', 'web'].includes(row.set), 'Unknown CSV set');
    assert(/^[1-9][0-9]*$/.test(row.image_number), 'Noncanonical image number');
    const key = `${row.set}:${row.image_number}`;
    assert(!keyed.has(key), `Duplicate CSV key ${key}`);
    keyed.set(key, row);
  }
  for (const row of generated) {
    const key = `${row.set}:${row.image_number}`;
    const previous = keyed.get(key);
    assert(previous, `Missing CSV key ${key}`);
    const portable = `images/${row.set}/${row.filename}`;
    assert.equal(row.source_path, io.absolute(`${BASE}/${portable}`), `Generated path ${key}`);
    for (const header of HEADERS) {
      const expected = header === 'source_path' ? portable : cell(row[header]);
      assert.equal(previous[header], expected, `CSV ${key} ${header}`);
    }
    const record = records.get(`${BASE}/${portable}`);
    assert.equal(record.published_sha256, previous.sha256, `Manifest/CSV hash ${key}`);
    assert.equal(record.original_sha256, previous.sha256, `Original photo hash ${key}`);
    assert.equal(String(record.size_bytes), previous.file_bytes, `Manifest/CSV size ${key}`);
    keyed.delete(key);
  }
  assert.equal(keyed.size, 0, 'Unexpected CSV keys');
}

async function verifyEvidence(io, manifestPath = MANIFEST, pins = PINS) {
  const manifest = JSON.parse(text(io.read(relativePath(manifestPath))));
  assert.equal(manifest.schema_version, 1, 'Manifest schema_version must be 1');
  assert(Array.isArray(manifest.files) && manifest.files.length === 64,
    'Scoped manifest must contain exactly 64 files (59 photos, four data files, analyzer)');
  const records = new Map();
  const buffers = new Map();
  for (const record of manifest.files) {
    const name = relativePath(record.path);
    assert(!records.has(name), `Duplicate manifest path ${name}`);
    assert(HASH.test(record.original_sha256) && HASH.test(record.published_sha256), `Invalid digest ${name}`);
    assert(Number.isSafeInteger(record.size_bytes) && record.size_bytes >= 0, `Invalid size ${name}`);
    const bytes = io.read(name);
    assert.equal(bytes.length, record.size_bytes, `Published size ${name}`);
    assert.equal(sha(bytes), record.published_sha256, `Published hash ${name}`);
    if (pins.originals[name]) assert.equal(record.original_sha256, pins.originals[name], `Historical hash ${name}`);
    if (name !== `${BASE}/readings-input.json` && name !== `${BASE}/readings.csv`) {
      assert.equal(record.published_sha256, record.original_sha256, `Immutable bytes ${name}`);
    }
    records.set(name, record);
    buffers.set(name, bytes);
  }
  function bytes(name) {
    assert(buffers.has(name), `Missing manifest entry ${name}`);
    return buffers.get(name);
  }
  const input = JSON.parse(text(bytes(`${BASE}/readings-input.json`)));
  const summary = JSON.parse(text(bytes(`${BASE}/summary.json`)));
  assert.deepEqual(input.sets.map(set => set.id), ['local', 'web'], 'Input sets/order differ');
  const expectedPaths = new Set(Object.keys(pins.originals));
  for (const set of input.sets) {
    assert.equal(set.source_directory, `images/${set.id}`, 'Input paths must be privacy-sanitized portable paths');
    assert.equal(set.rows.length, COUNTS[set.id], 'Input photo count differs');
    const names = set.rows.map(tuple => {
      assert(Array.isArray(tuple) && tuple.length === 5 && UUID.test(tuple[0]), 'Invalid transcription tuple/stem');
      return `${tuple[0]}.jpeg`;
    });
    assert.equal(new Set(names).size, names.length, 'Duplicate input filename');
    const directory = `${BASE}/images/${set.id}`;
    assert.deepEqual(io.entries(directory).sort(), [...names].sort(), `Photo inventory ${set.id}`);
    for (const name of names) expectedPaths.add(`${directory}/${name}`);
  }
  assert.deepEqual([...records.keys()].sort(), [...expectedPaths].sort(), 'Manifest allowlist differs');
  // Pins normalize only path fields; numeric values, notes, order and all other data remain fixed.
  assert.equal(semanticHash(input), pins.input_semantic, 'Input changed beyond home-path sanitization');
  const rebased = structuredClone(input);
  for (const set of rebased.sets) set.source_directory = io.absolute(`${BASE}/images/${set.id}`);
  const imageBuffers = new Map([...buffers].map(([name, data]) => [io.absolute(name), data]));
  const analyzer = loadAnalyzer(bytes(ANALYZER), filename => {
    assert(imageBuffers.has(filename) && filename.endsWith('.jpeg'), 'Unexpected analyzer file read');
    return imageBuffers.get(filename);
  });
  const result = plain(analyzer.analyze(rebased));
  const rows = await parseCSV(bytes(`${BASE}/readings.csv`));
  checkRows(rows, result.rows, io, records);
  assert.equal(semanticHash(rows), pins.csv_semantic, 'CSV changed beyond home-path sanitization');
  // This digest describes ORIGINAL transcription bytes, not the sanitized published input.
  const expectedSummary = {
    analysis_date: input.analysis_date, method: input.method, sampling: input.sampling,
    source_monitor_refresh_hz: input.source_monitor_refresh_hz,
    receiver_display_refresh_hz: input.receiver_display_refresh_hz,
    quantified_total_uncertainty_ms: null, independent_trials_claimed: false,
    transcription_input_sha256: records.get(`${BASE}/readings-input.json`).original_sha256,
    sets: result.summaries
  };
  assert.deepEqual(summary, expectedSummary, 'Exact historical summary differs');
  assert.equal(result.rows.length, 59);
  assert.equal(new Set(result.rows.map(row => row.sha256)).size, 59, 'Byte-identical photo duplicate');
  for (const id of ['local', 'web']) {
    assert.deepEqual(result.summaries[id].excluded_ambiguous_images, EXCLUSIONS[id], `Exclusions ${id}`);
    assert.equal(result.summaries[id].accepted_images, id === 'local' ? 25 : 21);
  }
  assert.equal(analyzer.report(rebased, result), text(bytes(`${BASE}/OPTICAL_LATENCY_REPORT_2026-10-22.md`)),
    'Historical report differs');
  return {ok: true, files: 64, photos: 59, accepted: 46, excluded: 13,
    original_input_sha256: expectedSummary.transcription_input_sha256,
    published_input_sha256: records.get(`${BASE}/readings-input.json`).published_sha256,
    summaries: result.summaries};
}

function encodeCSV(rows) {
  const quote = value => `"${cell(value).replaceAll('"', '""')}"`;
  return Buffer.from([HEADERS.map(quote).join(','),
    ...rows.map(row => HEADERS.map(header => quote(row[header])).join(','))].join('\r\n') + '\r\n');
}

function memoryPackage(files) {
  return {
    read(name) { assert(files.has(name), `Missing memory file ${name}`); return files.get(name); },
    entries(directory) {
      return [...new Set([...files.keys()].filter(name => name.startsWith(`${directory}/`))
        .map(name => name.slice(directory.length + 1).split('/')[0]))];
    },
    absolute: name => path.join('/virtual-optical-package', relativePath(name))
  };
}

function fixture(source) {
  const files = new Map([[ANALYZER, source]]);
  const io = memoryPackage(files);
  const local = [null,171,172,null,172,114,172,172,172,172,171,172,null,115,171,171,172,null,172,115,null,172,115,172,null,172,172,172,172,115,172];
  const web = [null,229,null,229,229,172,172,171,172,229,null,172,null,229,229,null,229,229,172,null,172,229,229,null,229,229,172,172];
  const input = {schema_version: 1, analysis_date: '2026-10-22', method: 'Synthetic self-test only',
    sampling: 'Not independent', source_monitor_refresh_hz: 60, receiver_display_refresh_hz: null,
    sets: Object.entries({local, web}).map(([id, values], setIndex) => ({id,
      source_directory: `images/${id}`, rows: values.map((latency, index) => {
        const number = index + 1;
        const repeated = id === 'local' ? {10: 9, 27: 26} : {15: 14, 23: 22};
        const sourceMs = 100000 + (repeated[number] || number) * 1000;
        const stem = `00000000-0000-4000-8000-${String(setIndex * 100 + number).padStart(12, '0')}`;
        files.set(`${BASE}/images/${id}/${stem}.jpeg`, Buffer.from(`Synthetic photo ${id}:${number}`));
        return [stem, sourceMs, latency === null ? null : sourceMs - latency,
          latency === null ? 'ambiguous' : 'accepted', number === 1 ? 'Comma, quote " and\nsecond line' : ''];
      })}))};
  const originalInput = structuredClone(input);
  for (const set of originalInput.sets) set.source_directory = `/home/private/Desktop/${set.id}`;
  const originalInputBytes = Buffer.from(JSON.stringify(originalInput, null, 2) + '\n');
  files.set(`${BASE}/readings-input.json`, Buffer.from(JSON.stringify(input, null, 2) + '\n'));
  const analyzer = loadAnalyzer(source, filename => {
    const name = path.relative('/virtual-optical-package', filename).split(path.sep).join('/');
    return io.read(name);
  });
  const rebased = structuredClone(input);
  for (const set of rebased.sets) set.source_directory = io.absolute(`${BASE}/images/${set.id}`);
  const result = plain(analyzer.analyze(rebased));
  const rows = result.rows.map(row => Object.fromEntries(HEADERS.map(header =>
    [header, header === 'source_path' ? `images/${row.set}/${row.filename}` : cell(row[header])])));
  const originals = new Map([[ANALYZER, source], [`${BASE}/readings-input.json`, originalInputBytes]]);
  originals.set(`${BASE}/readings.csv`, encodeCSV(rows.map(row =>
    ({...row, source_path: `/home/private/Desktop/${row.set}/${row.filename}`}))));
  files.set(`${BASE}/readings.csv`, encodeCSV(rows));
  const summary = {analysis_date: input.analysis_date, method: input.method, sampling: input.sampling,
    source_monitor_refresh_hz: 60, receiver_display_refresh_hz: null,
    quantified_total_uncertainty_ms: null, independent_trials_claimed: false,
    transcription_input_sha256: sha(originalInputBytes), sets: result.summaries};
  for (const [name, value] of [['summary.json', JSON.stringify(summary, null, 2)],
    ['OPTICAL_LATENCY_REPORT_2026-10-22.md', analyzer.report(rebased, result)]]) {
    files.set(`${BASE}/${name}`, Buffer.from(value));
    originals.set(`${BASE}/${name}`, files.get(`${BASE}/${name}`));
  }
  const pins = {input_semantic: semanticHash(input), csv_semantic: semanticHash(rows),
    originals: Object.fromEntries([...originals].map(([name, bytes]) => [name, sha(bytes)]))};
  const manifest = {schema_version: 1, files: [...files].map(([name, bytes]) => ({path: name,
    size_bytes: bytes.length, original_sha256: sha(originals.get(name) || bytes), published_sha256: sha(bytes)}))};
  files.set(MANIFEST, Buffer.from(JSON.stringify(manifest)));
  return {files, pins};
}

async function selfTest(source) {
  assert.equal(sha(source), PINS.originals[ANALYZER], 'Self-test requires the unchanged historical analyzer');
  const csvCases = [Object.fromEntries(HEADERS.map(header => [header,
    header === 'note' ? 'Comma, "quote"\r\nCRLF\nLF\rCR \u00e9' : '']))];
  assert.deepEqual(await parseCSV(encodeCSV(csvCases)), csvCases, 'CSV special-cell round trip');
  const original = fixture(source);
  const positive = await verifyEvidence(memoryPackage(original.files), MANIFEST, original.pins);
  assert.notEqual(positive.original_input_sha256, positive.published_input_sha256);
  let negatives = 0;
  async function reject(name, change, pattern) {
    const files = new Map([...original.files].map(([key, value]) => [key, Buffer.from(value)]));
    await change(files);
    await assert.rejects(() => verifyEvidence(memoryPackage(files), MANIFEST, original.pins), pattern, name);
    negatives++;
  }
  function refresh(files, name, bytes) {
    files.set(name, bytes);
    const manifest = JSON.parse(text(files.get(MANIFEST)));
    const record = manifest.files.find(entry => entry.path === name);
    record.size_bytes = bytes.length;
    record.published_sha256 = sha(bytes);
    files.set(MANIFEST, Buffer.from(JSON.stringify(manifest)));
  }
  async function editCSV(files, edit) {
    const name = `${BASE}/readings.csv`;
    const rows = await parseCSV(files.get(name));
    edit(rows);
    refresh(files, name, encodeCSV(rows));
  }
  await reject('swapped row hashes with refreshed published CSV digest', files => editCSV(files, rows => {
    [rows[0].sha256, rows[1].sha256] = [rows[1].sha256, rows[0].sha256];
  }), /CSV local:1 sha256/);
  await reject('missing exclusion', files => editCSV(files, rows => rows.shift()), /all 59/);
  await reject('duplicate row key', files => editCSV(files, rows => { rows[0] = {...rows[1]}; }), /Duplicate CSV key/);
  await reject('clock mutation', files => editCSV(files, rows => { rows[0].source_clock = '00:00:00.000'; }), /source_clock/);
  await reject('incorrect size field', files => editCSV(files, rows => { rows[0].file_bytes = '1'; }), /file_bytes/);
  await reject('changed numeric input and refreshed digest', files => {
    const name = `${BASE}/readings-input.json`;
    const input = JSON.parse(text(files.get(name)));
    input.sets[0].rows[1][1]++;
    refresh(files, name, Buffer.from(JSON.stringify(input)));
  }, /beyond home-path sanitization/);
  await reject('wrong historical input hash', files => {
    const manifest = JSON.parse(text(files.get(MANIFEST)));
    const entry = manifest.files.find(record => record.path === `${BASE}/readings-input.json`);
    entry.original_sha256 = entry.published_sha256;
    files.set(MANIFEST, Buffer.from(JSON.stringify(manifest)));
  }, /Historical hash/);
  await reject('changed image even with refreshed published digest', files => {
    const name = [...files.keys()].find(key => key.endsWith('.jpeg'));
    refresh(files, name, Buffer.from('changed photo'));
  }, /Immutable bytes/);
  await reject('unreferenced image', files => files.set(`${BASE}/images/local/extra.jpeg`, Buffer.from('extra')), /Photo inventory/);
  await reject('missing photo', files => files.delete([...files.keys()].find(key => key.endsWith('.jpeg'))), /Missing memory file/);
  await reject('manifest path traversal', files => {
    const manifest = JSON.parse(text(files.get(MANIFEST)));
    manifest.files[0].path = '../analyzer.cjs';
    files.set(MANIFEST, Buffer.from(JSON.stringify(manifest)));
  }, /traversal/);
  await reject('duplicate manifest path', files => {
    const manifest = JSON.parse(text(files.get(MANIFEST)));
    manifest.files[1] = {...manifest.files[0]};
    files.set(MANIFEST, Buffer.from(JSON.stringify(manifest)));
  }, /Duplicate manifest/);
  await reject('malformed CSV quoting', files => refresh(files, `${BASE}/readings.csv`, Buffer.from('"unterminated')), /CSV parse failed/);
  for (const value of ['../x', '/absolute', 'x/../y', 'x//y', 'C:\\x', 'x\0y']) {
    assert.throws(() => relativePath(value));
  }
  return {ok: true, self_test: true, synthetic_photos: 59, positive_cases: 2,
    negative_controls: negatives, path_rejections: 6, writes: 0,
    note: 'Synthetic in-memory package plus CSV round trip with quoted commas, quotes, Unicode and embedded CR/LF/CRLF.'};
}

async function main(args) {
  const testing = args[0] === '--self-test';
  const rest = testing ? args.slice(1) : args;
  assert(rest.length >= 1 && rest.length <= (testing ? 1 : 2),
    'Usage: node verify-optical.cjs PACKAGE_ROOT [MANIFEST_RELATIVE_PATH]\n' +
    '       node verify-optical.cjs --self-test ROOT_WITH_HISTORICAL_ANALYZER');
  const io = diskPackage(rest[0]);
  const result = testing ? await selfTest(io.read(ANALYZER)) : await verifyEvidence(io, rest[1] || MANIFEST);
  process.stdout.write(JSON.stringify(result, null, 2) + '\n');
}

if (require.main === module) {
  main(process.argv.slice(2)).catch(error => {
    process.stderr.write(`Optical verification failed: ${error.message}\n`);
    process.exitCode = 1;
  });
}

module.exports = {verifyEvidence, selfTest, parseCSV, checkRows, diskPackage};
