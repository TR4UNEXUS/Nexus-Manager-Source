# Nexus Manager

Nexus Manager is the Switch app for managing games across SD and USB storage. It provides game installation and moves, USB library import and repair, a file manager, network installation over SMB/NFS, and app updates.

This repository contains the 0.2.19 application source, assets, dependencies, build scripts and tests. Installable NSP and NRO updates are published separately in [Nexus Manager releases](https://github.com/TechRepairs4U/Nexus-Manager/releases/tag/v0.2.19).

## What's new

- Fixed a USB file-ownership check that could prevent games from being deleted. Known content can use the installed-game records when its on-drive metadata cannot be read.
- Ordinary game deletion no longer depends on checking unrelated unfinished downloads. Clearer errors explain a check that still cannot finish.
- XCI content is marked for installed use while the original package data is still checked for corruption. The original XCI is not changed.
- If an XCI installed with an older version will not launch, delete its broken installed copy and reinstall from your original XCI. Saves are kept.

- Visible Copy, Move, Delete and Paste here controls in File manager, with Switch shortcuts and touch buttons.
- Copy and move files or folders between SD and USB; switching drives keeps the clipboard selection. Moves verify the copy first, and deletion requires confirmation.
- A cleaner blue and purple interface with dedicated Cleanup and SMB/NFS pages.
- Installed games shows each game's title ID, version, update revision, DLC count and size on SD, internal storage and USB. Move and delete are available through Manage.
- Delete a chosen storage copy, including its base game, updates and DLC, while keeping saves and copies on other storage.
- Review unused content and tickets, or use Cleanup's Delete all with a confirmation and checks before each removal.
- FTP installs stream directly to SD or USB, with live transfer and installation details.
- Successful USB-cable installs remove their temporary package. Failed, cancelled or skipped installs keep it for retry; original PC/NAS files are untouched.
- Saved SMB/NFS locations, network discovery and direct streamed installs from a NAS.

The 0.2.19 build and PC regression checks passed. This version still needs testing on a Switch.

## Getting started

- [Build instructions](BUILDING.md)
- [Contributing](CONTRIBUTING.md) â€” help building the app, testing it and improving the interface is welcome.
- [App downloads](https://github.com/TechRepairs4U/Nexus-Manager/releases)
- [Nexus Atmosphere source](https://github.com/TR4UNEXUS/Nexus-Atmosphere)
- [USB firmware integration](https://github.com/TR4UNEXUS/Nexus-USB-Firmware)

The app source is in `app/`. Most Nexus features are in `app/sphaira/source/nexus`; the interface is under `app/sphaira/source/ui`. `app/tests` contains host tests for storage operations, library recovery, networking, updates and layout. SDK source is under `dependencies/`; compiled SDK libraries are generated locally.

USB features require matching Nexus firmware. The app uses the shared firmware service so two raw USB drivers do not try to own the same drive. The updater continues to use the existing Manager release repository; this source repository is for development.

## Credits and licence

Nexus Manager is based on Sphaira and retains its GPL licence and dependency notices. Read [the source attribution](app/ATTRIBUTION.md), [LICENSE](LICENSE) and [dependency revisions](app/DEPENDENCIES.json). Source names inherited from upstream are kept where they are part of the implementation.
