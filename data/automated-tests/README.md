# Automated Checks And Their Dates

[ctest-2026-10-15.log](ctest-2026-10-15.log) is the preserved CTest transcript from 15 October: all six listed tests passed, including streaming-source configuration. [historical-evidence-manifest.json](historical-evidence-manifest.json) contains the earlier 7 October five-test summary; it is not the missing original raw transcript for that earlier execution. These are separate records, not a new rerun.

The six C++ test sources and their CMake registration are under [sources/stream-edge/streaming-service](../../sources/stream-edge/streaming-service). Application code remains in its owning repository at the commits listed in [repository-ownership.json](../../repository-ownership.json). Source snapshots explain the tests but do not establish an unrecorded historical executable hash.

The Python statistics/camera-parser unit tests, Node metrics checks, LAN analysis self-test and A/B analysis negative controls are under [thesis-documentation/evaluation](../../thesis-documentation/evaluation). The package's offline verification record is dated 29 October and does not claim a new camera, LAN, database or end-to-end experiment. Live producers require the original application dependencies, configured credentials and explicit camera/service authorization; they are not invoked by `verify_bundle.py`.
