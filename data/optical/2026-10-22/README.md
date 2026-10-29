# Optical Latency: Portable Verification

Integrated-publication note: this README is `data/optical/2026-10-22/README.md`; the verifier is at the repository root. The helper-validation history below predates package assembly. For the actual integrated inventory and publication scope, use the [root index](../../../README.md) and [offline verification record](../../../offline-verification.json).

This isolated helper verifies the 22 October 2026 optical evidence without changing files, starting cameras/services, rerunning measurements or using the network. It does not publish anything. Requirements: an installed Node.js supporting `structuredClone` and Python 3. Only built-ins and Python's standard-library CSV parser are used; no npm/pip install is needed. Run from any working directory.

## Package Layout

```text
PACKAGE_ROOT/
  verify-optical.cjs
  thesis-documentation/evaluation/analyze-optical-latency.cjs
  data/optical/2026-10-22/
    manifest.json
    README.md
    readings-input.json
    readings.csv
    summary.json
    OPTICAL_LATENCY_REPORT_2026-10-22.md
    images/local/<31 original UUID filenames>.jpeg
    images/web/<28 original UUID filenames>.jpeg
```

Preserve the historical analyzer, summary, report and all 59 JPEGs byte-for-byte. Keep all 13 excluded photos, not just figure examples. The four two-laptop setup photos are context, not part of these 59 readings or the scoped 64-file manifest. This helper and README are new publication materials; they do not replace historical evidence.

Only home-path privacy sanitization is permitted in the published input/CSV. In `readings-input.json`, replace the two `source_directory` values with `images/local` and `images/web`. In `readings.csv`, replace each `source_path` with `images/local/<filename>` or `images/web/<filename>`. All filenames, numeric/null values, clocks, acceptance states, exclusions, notes, repeated-pair links and row order must remain unchanged. JSON object-key ordering and JSON/CSV formatting may change. Do not anonymize numeric fields, strip metadata, alter image pixels or rewrite the old analyzer. Keep the private originals separately, unchanged.

## Manifest Contract

Default manifest: `data/optical/2026-10-22/manifest.json`. A different package-relative manifest path may be supplied as the second argument. The scoped manifest must contain exactly 64 unique records: four data files, 59 JPEGs and the historical analyzer. Do not include the manifest itself, helpers, setup context or other measurement families in this scoped list.

```json
{
  "schema_version": 1,
  "files": [
    {
      "path": "data/optical/2026-10-22/readings-input.json",
      "size_bytes": 1234,
      "original_sha256": "b901b46012eb9e2a78e4b57b0f5c47639979197c6d64c0fdd53badcdaa65016b",
      "published_sha256": "<64 lowercase hex characters from the actual sanitized bytes>"
    }
  ]
}
```

The example is intentionally incomplete; `1234` is not an actual size. Every `path` is relative to `PACKAGE_ROOT`, not the manifest's directory. `size_bytes` describes published bytes. `original_sha256` records the private original before sanitization; `published_sha256` hashes the exact staged publication bytes. For images, analyzer, summary and report, the two hashes must be equal. For input/CSV, the hashes may differ because of privacy-only path changes and serialization.

The unchanged summary's `transcription_input_sha256` describes the **original** input and must equal its manifest `original_sha256`. It is deliberately NOT compared with the newly sanitized input's byte hash. The helper verifies that new hash against `published_sha256` instead. Historical hashes and path-normalized semantic hashes are pinned in the helper, so refreshing manifest hashes cannot silently authorize changes to readings, notes or exclusions. The original input/CSV need not be present in the public package to verify those semantics.

The semantic digests use UTF-8 JSON with recursively sorted object keys, compact separators and array order retained. The input's two directory fields and each CSV row's source path are normalized to the exact portable forms above; all other parsed values remain unchanged. CSV cells remain strings. These pins were calculated read-only from the audited originals, not from a subsequently sanitized file.

Treat the helper/manifest as trusted inputs: pin their publication commit or independently verify their provenance. Hashes attest to bytes, not capture-time custody or truth of the photographed digits. No manifest or helper has yet been uploaded by this task.

## Run

```bash
node /path/to/verify-optical.cjs /path/to/PACKAGE_ROOT
node /path/to/verify-optical.cjs /path/to/PACKAGE_ROOT path/to/optical-manifest.json
node /path/to/verify-optical.cjs --self-test /path/to/ROOT_WITH_HISTORICAL_ANALYZER
```

The self-test needs only the unchanged analyzer at the listed relative location. The original diploma root also works for this mode. It creates a synthetic 59-photo package entirely in memory, including privacy-sanitized input with a different byte hash and CSV cells with commas, escaped quotes, CRLF and embedded newlines. A separate CSV round trip checks Unicode and embedded CR, LF and CRLF without newline normalization. It verifies the package, then rejects swapped row hashes even after updating the published CSV digest, missing exclusions, duplicate keys, clock/size/numeric changes, wrong original hashes, altered images, extra/missing images, traversal, duplicate manifest entries and malformed CSV quoting. It also exercises six invalid path forms. No fixture files are created.

Success produces JSON on stdout and exit code 0. Failure produces a diagnostic on stderr and a nonzero exit code. Nothing is written by either mode. A positive synthetic self-test is a verifier test, not a measurement and not evidence that an as-yet unassembled public package exists.

## Checks

- Exact 31/28 file inventories and 59 unique original JPEG hashes; no missing, unreferenced or symlinked image entries. Only canonical contained relative paths and regular files are accepted.
- Every manifest file's published size/hash, immutable bytes where required, and original historical hashes for input, CSV, summary, report and analyzer.
- All 59 CSV records keyed by `(set,image_number)`, with every filename, hash, size, timer clock, millisecond value, status, latency, repeated-pair reference and note matched to the analyzer output. Exclusions remain null/blank, never inferred.
- The analyzer's exported `analyze()` runs with directories rebased only in memory and reads verified image buffers. It is loaded with read-only file access; its CLI `main()` is never invoked. This avoids the old CLI's absolute paths and weak CSV substring-hash check. The VM is an execution mechanism, not a general untrusted-code security sandbox; the analyzer's audited hash is verified first.
- Exact complete historical summary, including method metadata, histograms, repeated pairs, source clock values and distinct-pair sensitivity; historical report regeneration must match unchanged text.
- Home-path-only semantic preservation in input and CSV. No new-input-hash versus historical-summary-hash equality is required.

The historical report retains its original `.runtime` reproduction command and original Desktop-folder description. Those instructions are historical, not the portable package instructions. Use this helper for the published layout.

## Results And Limits

Local: 25 accepted of 31, exclusions #1/#4/#13/#18/#21/#25; mean 160.4 ms, median/p95 172/172 ms, range 114-172 ms. Web: 21 accepted of 28, exclusions #1/#3/#11/#13/#16/#20/#24; mean 204.52380952380952 ms, median/p95 229/229 ms, range 171-229 ms. Distinct-pair means are 159.3913043478261 ms (23 local pairs) and 201.94736842105263 ms (19 web pairs).

These are short, consecutive photographic observations, not independent trials. Different JPEG hashes and distinct displayed pairs do not prove independent capture instants. The local/web difference is not a paired network-only or server-only effect. A reported 60 Hz reference period of 16.67 ms is not a bound on total uncertainty. No confidence interval, calibrated 1 ms accuracy or numerical total uncertainty is established. This helper checks files and arithmetic, not OCR or independent visual retranscription.

## Implementation Validation

On 29 October 2026, the production verifier also passed against an in-memory package assembled from all 64 actual evidence files, with only input directories and CSV source paths sanitized. All 59 photos, 46 accepted readings, 13 exclusions, complete summaries and the regenerated report matched. The sanitized input byte hash differed from the historical hash as intended. A swap of two CSV row hashes was rejected even after refreshing the manifest's published CSV hash/size. The original 69 audited source hashes and the native DOCX hash were rechecked unchanged. No staged publication directory or manifest was written by that test; no disk package is claimed to have been assembled or uploaded.

## Privacy Review: 29 October 2026

All 59 full-frame timer images were reviewed on five inspection-only contact sheets generated in memory; no sheet was saved or published and no original image was altered. At contact-sheet scale, no faces or credential/token strings were observed. This is not a guarantee about tiny or unreadable text and is not an exhaustive native-resolution secret scan. Browser/profile initial M, the stopwatch website, wallpaper, hands, camera housing and surrounding equipment remain visible. Preserve them only under the author's explicit full relevant-evidence disclosure authorization.

The earlier file-level review decoded all 59 timer JPEGs and four setup JPEGs at 2048 x 1536. No EXIF/GPS tags or common XMP/IPTC markers were found; the shared ICC profile is retained unchanged. Absence of metadata does not establish capture cadence/date or pixel privacy. Setup originals, reviewed separately, also expose a private LAN endpoint/port, application details and room/whiteboard content; they are not part of this scoped verifier.

Only home paths in JSON/CSV are sanitized here; do not claim all visual identifying details have been removed. If subsequent full-resolution review finds a credential or uninvolved person's identifying detail, resolve disclosure before publication rather than retouching evidence. No upload, original edit, DOCX/PDF change, camera or service action was performed to create these helpers.
