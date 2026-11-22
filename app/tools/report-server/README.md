# Nexus diagnostic service

This small Linux service accepts bounded diagnostic JSON and serves the signed
repair catalogue. Deploy it behind an HTTPS reverse proxy. It listens only on
loopback. The systemd unit runs it as a dedicated unprivileged user and keeps
reports outside the website root. Existing reports have no public read endpoint.

Routes:

- `POST /nexus-reports/v1/reports`: receive a report and return its SHA-256 receipt.
- `GET /nexus-reports/v1/catalog`: serve the signed catalogue unchanged.
- `GET /nexus-reports/health`: basic service health.

The reverse proxy must limit requests, connections and body sizes. The receiver
accepts at most 128 KiB per report, retains up to 1,000 reports/100 MiB, prunes
reports older than 30 days both periodically and when receiving new ones, and refuses new reports when
free disk space is below 512 MiB. A repeated identical report returns the same
receipt. Reports contain selected structured events and numeric counters, not
raw log archives. Automatic reports have no free-text description.

Manager asks once before enabling automatic reporting. Reports wait on the
physical SD card until the app is idle, then retry if unavailable. A queue entry
is removed only after a matching server receipt has been saved locally. Users can
disable automatic reporting, send a manual report or discard queued reports.

Deploy the application endpoint using an external `nexus_report_endpoint.hpp`
file and `NEXUS_REPORT_CONFIG_DIR`. Define `NEXUS_REPORT_UPLOAD_URL` and
`NEXUS_REPORT_CATALOG_URL` there. Without this private build configuration,
uploads are disabled and repair guidance uses the public GitHub fallback.
Never put administrator credentials, deployment addresses, signing keys or
private configuration into the repository. The upload URL is present in the
compiled application and should be treated as public; it is not authentication.

The service does not distribute tickets, certificates, keys or game content.
Adding other repair payloads requires a separate versioned format, validation,
user review and rollback support. Run `python3 tools/report-server/test_receiver.py`
on Linux to check the receiver and `python3 tests/run_report_service_tests.py`
to test Manager's production queue with injected network/storage failures.
