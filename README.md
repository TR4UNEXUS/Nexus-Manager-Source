# Nexus Manager

Nexus Manager is the Switch app for managing games across SD and USB storage. It provides game installation and moves, USB library import and repair, a file manager, network installation over SMB/NFS, and app updates.

This repository contains the 0.2.22 application source, assets, dependencies, build scripts and tests. Installable NSP and NRO updates are published separately in [Nexus Manager releases](https://github.com/TechRepairs4U/Nexus-Manager/releases/tag/v0.2.22).

## What's new

- Browse ports in an icon grid with SD and USB labels. Both drives appear by default.
- Recently viewed port icons stay cached while browsing, changing filters and returning from details. Press Y: Refresh to reload them.
- Copy or move NRO ports between SD and USB without creating a HOME shortcut first. Linked data folders are included, and moves verify the copied files before removing the originals.
- Manage port mods from existing folders or ZIP files. Enable a mod, disable it or restore the original files.
- Delete port files and HOME shortcuts separately, with a warning about saves stored inside port folders.
- Progress bars follow the whole task instead of restarting for each file.
- Delete confirms first, then checks and removes the selection in one task. File and port deletion no longer hashes every file before removing it.
- Removed the public startup timing tests and timing-shortcut creation.

Earlier improvements include File manager copy/move/delete, storage-specific game deletion, Cleanup, direct FTP and NAS installs, and USB-library repair. The fixes for game deletion and XCI installation from 0.2.19 are included.

The 0.2.22 build and automated checks passed. These changes still need testing on Switch hardware.

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
