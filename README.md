# Nexus Manager

Nexus Manager is the Switch app for managing games across SD and USB storage. It provides game installation and moves, USB library import and repair, a file manager, network installation over SMB/NFS, and app updates.

This repository contains the 0.2.40 application source, assets, dependencies, build scripts and tests. The Switch app checks for NSP and NRO updates on the [Nexus Manager release feed](https://github.com/TechRepairs4U/Nexus-Manager/releases/latest).

## Changes in 0.2.40

Switch Manager picks up changes prepared by Nexus Manager for PC, including new USB games, game removals and replacements after compression. You can also run the import from Installed games. The app recognises the NX1 pack and edition, offers HATS updates, and includes SaltyNX compatibility guidance.

PC sync reopens the USB drive and checks its identity before applying a change detected at startup. The updater and diagnostic log use the same version number as the installed app.

Help building, testing and improving Nexus is welcome. Private report service settings are configured separately for deployment.

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
