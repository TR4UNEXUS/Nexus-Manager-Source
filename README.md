# Nexus Manager

Nexus Manager is the Switch app for managing games across SD and USB storage. It provides game installation and moves, USB library import and repair, a file manager, network installation over SMB/NFS, and app updates.

This repository contains the 0.2.24 application source, assets, dependencies, build scripts and tests. Installable NSP and NRO updates are published separately in [Nexus Manager releases](https://github.com/TechRepairs4U/Nexus-Manager/releases/tag/v0.2.24).

## What's new

- USB library repair runs when you choose it. Removed the automatic startup warning and repeated Installed games prompts.
- Empty drives and older update versions no longer trigger misleading repair advice. Unfinished checks are reported as unconfirmed.
- Ticket checks use the current system's ticket records. A missing ticket backup on USB is not treated as a missing installed ticket.
- Cleanup protects installed games and rechecks selections before removal. A ticket warning alone never makes an installed game eligible for cleanup. Explicit game deletion remains separate and keeps saved progress.
- Installs, copies and moves show one overall progress bar and transfer speed. Game installs have a larger cover and bold title, with simple status text.
- USB library backup tools can preview restoring missing routing links from the console-side backup provided by N4 firmware.
- Improved local split-XCI handling and cartridge-content installation. Keep all parts together on SD or USB and select the first part.

Port management, icon caching, mod folders and ZIPs, File manager copy/move/delete, direct FTP and NAS installs, and automatic app updates remain included.

The 0.2.24 build and automated checks passed, and the app was reported working in the N4R2 console test. Wider testing is still needed across libraries, drives and network servers.

## Getting started

- [Build instructions](BUILDING.md)
- [Contributing](CONTRIBUTING.md) - help building the app, testing it and improving the interface is welcome.
- [App downloads](https://github.com/TechRepairs4U/Nexus-Manager/releases)
- [Nexus Atmosphere source](https://github.com/TR4UNEXUS/Nexus-Atmosphere)
- [USB firmware integration](https://github.com/TR4UNEXUS/Nexus-USB-Firmware)

The app source is in `app/`. Most Nexus features are in `app/sphaira/source/nexus`; the interface is under `app/sphaira/source/ui`. `app/tests` contains host tests for storage operations, library recovery, networking, updates and layout. SDK source is under `dependencies/`; compiled SDK libraries are generated locally.

USB features require matching Nexus firmware. The app uses the shared firmware service so two raw USB drivers do not try to own the same drive. The updater continues to use the existing Manager release repository; this source repository is for development.

## Credits and licence

Nexus Manager is based on Sphaira and retains its GPL licence and dependency notices. Read [the source attribution](app/ATTRIBUTION.md), [LICENSE](LICENSE) and [dependency revisions](app/DEPENDENCIES.json). Source names inherited from upstream are kept where they are part of the implementation.
