# Nexus Manager

Nexus Manager is the Switch app for managing games across SD and USB storage. It provides game installation and moves, USB library import and repair, a file manager, network installation over SMB/NFS, and app updates.

This repository contains the 0.2.31 application source, assets, dependencies, build scripts and tests. Installable NSP and NRO updates are published separately in [Nexus Manager releases](https://github.com/TechRepairs4U/Nexus-Manager/releases/tag/v0.2.31).

## Changes in 0.2.31

- Clearer backup and incomplete-source messages, without treating a failed check as proof that content or tickets are missing.
- Live USB failure snapshots and separate short-output/decompression/filesystem-open diagnostics, with filtered numeric events only.
- Existing map repair, moves, deletion, network installs and in-app updates remain available. Firmware recovery improvements require Nexus RF3.

The NRO/NSP and host checks passed; console testing is still needed. Help testing, building and improving Nexus is welcome. Private upload endpoints and credentials are not included in this source.

## Getting started

- [Build instructions](BUILDING.md)
- [Contributing](CONTRIBUTING.md) - help building the app, testing it and improving the interface is welcome.
- [App downloads](https://github.com/TechRepairs4U/Nexus-Manager/releases)
- [Nexus Atmosphere source](https://github.com/TR4UNEXUS/Nexus-Atmosphere)
- [USB firmware integration](https://github.com/TR4UNEXUS/Nexus-USB-Firmware)

The application is in `app/`. Most Nexus features are under `app/sphaira/source/nexus`; the interface is under `app/sphaira/source/ui`. Host tests are in `app/tests`. SDK source is under `dependencies/`; compiled SDK libraries are generated locally. Generic catalogue-signing and report-receiver tools are in `app/tools/repair-catalog` and `app/tools/report-server`.

USB features require matching Nexus firmware. Manager uses the shared firmware service so two raw USB drivers do not try to own the same drive. The updater uses the existing Manager release repository; this repository is for development.

## Credits and licence

Nexus Manager is based on Sphaira and retains its GPL licence and dependency notices. Read [the source attribution](app/ATTRIBUTION.md), [LICENSE](LICENSE) and [dependency revisions](app/DEPENDENCIES.json). Source names inherited from upstream are kept where they are part of the implementation.
