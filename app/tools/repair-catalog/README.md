# Repair catalogue

Manager 0.2.28 can read signed repair guidance from the configured report service,
then the `repair/catalog.nxrc` file in TR4UNEXUS/Nexus-USB-HATS if that service is
unavailable. A bundled copy and two checked SD cache slots support offline use.

Rules describe findings and suggest existing Manager actions. They cannot run
commands, supply filesystem paths, turn off verification, or download tickets
or game content. Exact metadata variants use the application ID, version, content
ID and SHA-256 together. Recognising a variant never bypasses local integrity,
referenced-content, journal or rollback checks.

To publish guidance, edit the catalogue JSON, increase its revision, and sign it
with `catalog.py`. The signing key must be kept outside source and output folders.
Install Python's `cryptography` package in a private authoring environment, then:

```sh
python catalog.py catalog.json --key /private/repair-catalog-signing.pem --output catalog.nxrc
```

Publish the same signed bytes to the report service and GitHub. Do not re-sign
changed content with the same revision: Manager rejects same-revision changes
and older revisions. Never publish the signing key. The public verification key
is safe to publish. Changing it requires an application update.

Run `tests/run_repair_catalog_tests.py`, `tests/run_catalog_cache_tests.py` and
`tests/run_repair_assistant_tests.py` when changing the format or app integration.
