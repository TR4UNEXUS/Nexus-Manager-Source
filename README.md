# Nexus Manager

Nexus Manager is the Switch app for managing games across SD and USB storage. It provides game installation and moves, USB library import and repair, a file manager, network installation over SMB/NFS, and app updates.

This repository contains the 0.2.7 application source, assets, vendored dependencies, build scripts and tests. The latest repair changes handle unrelated homebrew entries while checking a USB game's base, update and DLC.

## Getting started

- [Build instructions](BUILDING.md)
- [Contributing](CONTRIBUTING.md) — help building the app, testing it and improving the interface is welcome.
- [App downloads](https://github.com/TechRepairs4U/Nexus-Manager/releases)
- [Nexus Atmosphere source](https://github.com/TR4UNEXUS/Nexus-Atmosphere)
- [USB firmware integration](https://github.com/TR4UNEXUS/Nexus-USB-Firmware)

The app source is in `app/`. Most Nexus features are in `app/sphaira/source/nexus`; the interface is under `app/sphaira/source/ui`. `app/tests` contains host tests for storage operations, library recovery, networking, updates and layout. SDK source is under `dependencies/`; compiled SDK libraries are generated locally.

USB features require matching Nexus firmware. The app uses the shared firmware service so two raw USB drivers do not try to own the same drive. The updater continues to use the existing Manager release repository; this source repository is for development.

## Credits and licence

Nexus Manager is based on Sphaira and retains its GPL licence and dependency notices. Read [the source attribution](app/ATTRIBUTION.md), [LICENSE](LICENSE) and [dependency revisions](app/DEPENDENCIES.json). Source names inherited from upstream are kept where they are part of the implementation.
