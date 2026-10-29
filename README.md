# Thesis Evidence: Reviewed Publication, 29 October 2026

This is the canonical integration-evidence package for Mehmet Mehmeti's thesis. It extends the four functional reports published on 26 October. Measurements span six components, so datasets are stored once in `stream-edge`, with implementation ownership and pinned source commits in [repository-ownership.json](repository-ownership.json). No application `main` branch is changed by this evidence release.

## Result To Evidence

| Thesis result | Evidence and reproduction | Professor comments |
| --- | --- | --- |
| Historical integration, recovery and camera checks, Tables 9/13 | [Four-report explanation](data/historical/README-2026-10-26.md), root JSONs and [UI controls](data/historical) | 23, 43 |
| Optical latency, Table 11 | [All 59 photos, readings and exclusions](data/optical/2026-10-22/README.md) | 18, 25, 43 |
| F450 hardware, timed FPS/resources, recording off/on and long MP4, Tables 3-4/12/14 | [F450 measurements](data/f450/README.md) | 18-19, 21-25, 43 |
| Two-laptop commands/startup, FPS/drops and playback, Table 10 | [LAN observations](data/lan/README.md), [server checks](data/server/README.md) | 18, 20, 23, 25, 43 |
| FIFO versus bounded queue, Table 15 | [A/B raw timing and analysis](data/buffering-ab/README.md) | 19, 24-25, 43 |
| Component and tool tests | [Automated test records and sources](data/automated-tests/README.md) | 43, 49 |

The machine-readable [claim map](claims-to-evidence.json) connects sections and tables to their source families. [artifact-manifest.json](artifact-manifest.json) lists every published file's size/hash; provenance files distinguish original hashes from privacy-projected public bytes. A checksum confirms bytes, not authorship or the truth of a reported execution.

## Offline Verification

Prerequisites: Python 3.10+ and Node.js 18+ on PATH. No npm install, camera, browser, application service, database or network connection is needed for the public arithmetic checks.

```bash
python3 -B verify_bundle.py
sha256sum -c SHA256SUMS.txt
```

The checker verifies every included file, binds each of the 59 photo hashes to its exact CSV row, recalculates optical statistics, and checks the public F450/A/B and LAN/server measurements. It does not execute the live experiment producers. Newly performed offline verification is dated 29 October; it is not an independent replication of the historical experiments. Operational runners and their dependency lockfile are provided as source, with their original hardware/layout/configuration requirements.

## Selection And Privacy

Included: cited source data, accepted/excluded optical photos, failed-preparation and correction ledgers, configuration/version records, relevant test/analysis source, and verification records. Excluded: credentials, `.env`, private settings, database dumps, unrestricted operational logs, builds, `node_modules`, duplicate downloads and unrelated trials. Third-party source attribution and GPLv3 license are retained.

**The author chose to keep all MP4s private.** [Recording attestation](data/recording-attestation.json) records the 29 October read-only check of retained recordings against historical hashes, sizes and probe values. Readers cannot independently rehash or decode absent MP4s; the package does not claim otherwise. This restriction differs from the original timer JPEGs, which are public and byte-identical. No photo was retouched.

## Limits That Remain Explicit

- Consecutive optical pictures are not independent trials; display/phone uncertainty is not fully calibrated.
- Some final browser counter snapshots were not persisted. Saved interval metrics support aggregate checks but not complete endpoint reconstruction.
- F450 historical dirty patches are not reconstructed from commit/diff hashes alone.
- The server supplement retains two failed exact version checks while its 31 file checks match. Two requested supplementary files were never supplied.
- A/B measures an instrumented pre-encoder stage under imposed slowdown, not end-to-end optical latency, universal optimality, stressed FPS improvement or meaningful RSS savings.
- Current tooling/source references do not retroactively establish missing historical execution fingerprints. The preserved 15 October six-test CTest log and earlier five-test summary have different dates.

## Packaging Practice

The package follows claim-to-artifact mapping, explicit dependencies, documented commands and honest scope, drawing on [ACM SIGPLAN artifact-evaluation guidance](https://popl26.sigplan.org/track/POPL-2026-artifact-evaluation). GitHub supplies reviewable versioned files; no ACM badge, DOI archive or independent replication is claimed. [GitHub file-size guidance](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github) informs the separation of reviewable text/photos from large media. Accessed 29 October 2026.
