# Returned Server Evidence

The two-laptop results are supported by privacy-projected host, service, database and file-verification records collected on 22 October and supplied on 23 October. The [dated review](../../reports/SERVER_EVIDENCE_REVIEW_2026-10-23.md) records the receipt and supplementation sequence. The verification commands and extraction rules are described in the public provenance and [offline checker](../../scripts/verify-lan.py).

For Tables 3-4, see the [server hardware supplement](hardware.json) and [captured host/service versions](host-service.json). The [selected database record](selected-database-evidence.json), [original verifier](supplied-verifier-original-report.json), and [supplemental verifier](server-lan-verification.json) support the integrity and synchronization results.

Both original and supplemental verifier outcomes are preserved. The supplement reports `failed` for two exact version-equality checks (expected video version 5, observed 6); all 31 reported file hashes, sizes and decoded counts agree. Later APPLIED edge events corroborate version 6. Do not reinterpret the failed equality checks as file corruption or silently relabel them passed.

The original archive still lacks `VERIFICATION-SUMMARY.md` and `postgres.txt`; PostgreSQL version information is available in the supplied JSON. Hardware details from the later paste are author-attributed and have no capture timestamp. Physical-server browser controls were not observed. MP4s and unrestricted process/network/database dumps are intentionally private. Current public arithmetic checks are not a fresh measurement on the remote server.
