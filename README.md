# Nexus Manager

Nexus Manager is the Switch app for managing games across SD and USB storage. It provides game installation and moves, USB library import and repair, a file manager, network installation over SMB/NFS, and app updates.

This repository contains the 0.2.30 application source, assets, dependencies, build scripts and tests. Installable NSP and NRO updates are published separately in [Nexus Manager releases](https://github.com/TechRepairs4U/Nexus-Manager/releases/tag/v0.2.30).

## What's new since 0.2.28

- Clear firmware-update guidance when the active USB service does not support the required protocol.
- Move recovery when identical destination content is already registered. Content must match before reuse; a failed check keeps the source.
- Clearer storage, connection, locked-file and conflicting-content errors.
- More accurate failure-stage reporting, with install setup and sleep/wake diagnostics.
- Bulk deletion of selected games while keeping saved progress.
- Tickets + link existing files for complete content already on a drive, using a suitable original package.
- Clearer guidance for formatted drives. A map backup cannot restore erased game content.

Guided USB repair, console backups of the USB map, language selection, port and mod management, File manager, FTP/USB cable transfers and SMB/NFS installs remain available. Repairs cannot replace missing game files or tickets, and integrity checks stay enabled.

Problem reports remain optional and exclude raw logs, credentials, tickets, saves and game files. The public build leaves private server uploads disabled; deployment configuration stays outside this repository. Users can disable reporting or discard queued reports.

The application build and automated checks passed. Version 0.2.30 still needs console testing. Please report reproducible problems and a diagnostic report where available, without including private files.

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
