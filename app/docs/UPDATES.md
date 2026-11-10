# USB Loader release/update contract

Only https://github.com/TechRepairs4U/USB-Loader is the app update source.
Checking is manual (minus / Updates), so normal storage use requires no network.
The public GitHub Releases API is used without tokens. No accounts or telemetry.

The checker reads the most recent 20 published releases, skips drafts/invalid
assets and picks the highest numeric version. Preview releases are included and
labelled. Use a new x.y.z version for every update, including preview to stable;
the same numeric version is not automatically reinstalled.

## Publishing

1. Bump nexus/release.hpp, CMake/NACP, build wrapper, README and package script.
2. Build/test using vX.Y.Z or vX.Y.Z-preview. The owner requests a binary-only
   GitHub repository: do not push source code or a source ZIP there. Arrange
   compliant access to the corresponding GPL source separately before publishing.
3. Upload only the standalone **USBLoader.nro** asset to GitHub, in accordance
   with the owner's distribution request. Draft releases are invisible.
4. Confirm GitHub exposes the asset size and sha256 digest. Without a digest the
   release is not accepted by the updater.
5. Fetch the public release JSON and run tests/update_tests.cpp against the build.

The app verifies HTTPS hostname/certificate, repository-specific download URL,
size (64 KiB to 64 MiB), SHA-256, NRO/ASET bounds, app name and embedded version.
Only GitHub's HTTPS release-asset hosts are allowed as redirects. No release
script is downloaded or executed. No console credential is needed.

The new app is staged/read-back verified on physical SD and renamed into
switch/usbloader/USBLoader.nro. The old file is retained as
USBLoader-VERSION-TICKS.nro.bak in that same directory. Exit/reopen after updating.
Other app paths, hbmenu, firmware and installed games are not changed.

If interrupted between the renames, restore that backup as USBLoader.nro.
A uniquely named update-TICKS.part file is not an installed update.
No automatic boot-time update or backup deletion is performed.

TLS failures are not bypassed: correct the console clock, connect to the internet,
or update manually from Releases. GitHub rate limits are reported as HTTP errors.

API: https://docs.github.com/en/rest/releases/releases#list-releases
CA source: https://curl.se/docs/caextract.html
