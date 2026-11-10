# USB Loader 0.6.5-preview — busy-state safeguards

This is an app + matching HOME-launcher update, not a firmware update or a
confirmed fix for the cross-app USB lock. Keep package3, HOC, system hbl.nsp,
Magic Monkei and existing games unchanged. It includes the 0.6.4 logger repair.

## Install and test this build

1. Back up your current NRO and HOME launcher package. Copy this ZIP's `switch`
   folder to the physical microSD. Install `USB-Loader-HOME-0.6.5.nsp` over
   USB Loader's existing HOME launcher, choosing **Internal Storage**. Both
   updates are necessary; the NRO alone cannot update an embedded loader.
2. Fully reboot. Open USB Loader and check **0.6.5-preview**, then close it
   with HOME -> X -> Close. Open/close Magic Monkei, then open USB Loader once.
   No game reinstall is needed. Stop at the first failure rather than looping.
3. If startup stays black, allow up to 30 seconds for the app-side watchdog.
   If a progress window stalls, allow 15 seconds for the busy message. B asks
   for cancellation. A slow read-only scan/browser/check also offers X to
   close the app, followed by confirmation. Keep USB connected.
4. Return the result and any files in `switch/usbloader/startup-debug/` and
   `switch/usbloader/forwarder-debug/`, plus the usual Atmosphere USB logs.
   A stalled filesystem can still prevent diagnostic files from being saved.

## What is protected

- Cancel no longer dismisses a window and then blocks the UI joining a live
  worker. The window stays interactive until the worker actually finishes;
  request buffers and file handles remain alive throughout the request.
- Installs, moves, deletes, updates, probe preparation and session-arming use
  protected operations. There is **no forced exit during these operations**.
  Cancellation may stay pending until the current IPC/write returns. A busy
  message does not prove a lock or an error, and never claims install success.
- Read-only browsing, installed scans and capability checks run off the UI
  thread. Installed-service initialization is lazy; it no longer blocks Home.
  USB homebrew icons use one bounded-memory worker instead of UI-thread reads.
- Startup/shutdown has a 30-second syscall-only watchdog in the NRO and
  embedded HOME loader. It exits only its own process, never resets/unmounts
  USB, never kills a service, and is inhibited while protected work is active.
  The loader guard stops before jumping into another NRO; it does not impose
  a time limit on launched homebrew games. The app guard is disarmed in the UI.
- No completion callback can start new work during app destruction.

## Limits

These are app-side mitigations, **not a guarantee against a frozen Switch**.
They cannot repair a deadlocked Horizon service/kernel, protect another app's
writes, or cover code before the watchdog starts. A guard also needs the OS
to schedule it and process exit to work. It does not forcibly interrupt live
synchronous IPC or terminate one worker and free its in-flight buffers.
Forced read-only/startup exit skips normal cleanup; diagnostic/cache/settings
files may be incomplete. It must not be used as permission to unplug USB.
The separate HOME News-menu stall remains unresolved. Switch testing is still
required; compilation/host tests are not a console validation.

Rollback: restore your saved NRO and reinstall its matching HOME NSP. No
firmware rollback or deletion of installed games/saves is required.

## Historical 0.6.4 diagnostics (not current install instructions)

0.6.3 was confirmed on the returned SD, but created no APP/HBL log files.
This build fixes a concrete diagnostic defect: libnx forwards FS_MAX_PATH
(769) bytes for each filesystem path; the old loggers provided 160-byte
filename buffers and short directory literals. Both loggers and the generated
forwarder diagnostic export now provide full, zero-padded path buffers.
The host regression harness rejects the old inputs and accepts the new ones.

This is NOT a confirmed fix for switching between Magic Monkei and USB Loader,
nor for the separate HOME News-menu stall. There are no firmware, HOC, system
HBL, USB routing, session-policy or Magic Monkei changes in this ZIP.
If the app can open but its logger fails, it shows "Startup log unavailable"
with a step and native result: 1=SD open, 2=create, 3=open file, 4=write.

### Historical 0.6.4 test — no game reinstall required

1. Back up `switch/usbloader/USBLoader.nro`; copy the ZIP's `switch` folder to
   the physical microSD. Install the included `USB-Loader-HOME-0.6.4.nsp`
   over USB Loader's HOME launcher, choosing **Internal Storage**. Both files
   are needed. Leave package3, HOC, system hbl.nsp and game forwarders alone.
2. Fully reboot, open USB Loader once, check **0.6.4-preview**, then close it
   through HOME -> X -> Close. Photograph any startup-log error notification.
3. Open Magic Monkei once. If opening/closing alone reproduces the issue as
   reported, no download or installation is needed. If it opens, close it and
   try USB Loader once more. Stop at the first failure; no repeat loops.
4. Return `switch/usbloader/startup-debug/`, `switch/usbloader/forwarder-debug/`,
   `atmosphere/logs/usb_loader_trace.txt` and `usb_storage_status.txt` from SD.
   If it still hangs, allow about 15 seconds and recover as usual.

The logs must first work on the baseline; an empty folder is not proof that
the launcher/app never ran. No file logger can guarantee progress if FS itself
is stalled. The News symptom remains under investigation; don't repeat that
test while recovering from a failed application switch.

Rollback: restore the previous NRO and reinstall HOME 0.6.3 (or your saved
working pair). No firmware rollback or deletion of games/saves is required.

## Previous 0.6.3 diagnostics (historical instructions)

**Diagnostic build, not a confirmed black-screen fix.** This release identifies
which call stops returning after closing Magic Monkei and opening USB Loader.
It does not reset USB, alter installation/game routing or change timeouts.
No package3, HOC, system hbl.nsp, boot configuration or Magic Monkei changes.

## Current test: Magic Monkei -> USB Loader

1. Back up your existing `switch/usbloader/USBLoader.nro`, then copy this ZIP's
   `switch` folder to the physical microSD root.
2. Install the included `USB-Loader-HOME-0.6.3.nsp` to **Internal Storage**, over
   USB Loader's existing HOME launcher. Do not delete games or recreate their
   forwarders. Updating the NRO alone does not update the embedded launcher.
3. After a full reboot, open USB Loader once from HOME to record a baseline.
   Check **0.6.3-preview**, then close it using HOME -> X -> Close.
4. Use Magic Monkei for the USB install that normally triggers the problem.
   Once it finishes (or reports an error), close it with HOME -> X -> Close.
   Open USB Loader from HOME once. No repeated installs are needed.
5. If it black-screens, allow about 15 seconds, then recover as usual. Return
   these folders/files from the microSD; do not delete the baseline logs:
   - `switch/usbloader/startup-debug/` (app stages, max 64 KiB per launch)
   - `switch/usbloader/forwarder-debug/` (HOME launcher stages, max 16 KiB)
   - `switch/MagicMonkeiUSB/install.log`
   - `atmosphere/logs/usb_loader_trace.txt` and `usb_storage_status.txt`

Both loggers create unique files and flush each line. App logs identify threads
and contain begin/end markers around service queries, graphics setup, NS/NCM
database opens and the first frame. No keys, tokens, URLs or file contents.
Logging failure does not replace a call's return value or retry the operation.
Concurrent app logging uses a try-lock and reports skipped events explicitly;
the logger cannot guarantee persistence if SD/FS itself stalls. An unmatched
begin is a lead, not proof, especially with skipped events or an incomplete log.

The app trace starts at libnx userAppInit. The launcher trace starts after its
FS initialization. Missing logs cannot identify a stall before those points.
No system-HBL replacement is included: use the new HOME NSP for this test.
The existing USB port forwarders need not be recreated for this first test.

Rollback: restore the backed-up 0.6.2 NRO and reinstall the previous USB Loader
HOME NSP. No firmware restore is needed. Logs are retained until you remove them.

## Previous 0.6.2 diagnostics (background, not the current test)

0.6.2 added read-back-verified exports of generated homebrew shortcuts and
embedded HBL logging. Those diagnostics remain in 0.6.3, but this test focuses
on opening USB Loader itself after Magic Monkei, not recreating NFS shortcuts.
The returned NRO and generated shortcut were verified as the expected build.
The remaining freeze was not fixed; no new crash report identified the cause.

The diagnostic logger retains a real-SD file handle opened before USB activation,
never reopens it under a USB mapping, closes it before NRO entry, and writes at
most 16 KiB per launch. Each file is newly created; no existing log is overwritten.
No keys, tickets, tokens, RAM dumps or game data are recorded. Copies/logs remain
until you remove them while no app uses them. Creation stops before installing
if the diagnostic NSP cannot be written and verified. A missing startup log alone
does not prove the HBL was never entered: an unavailable SD/logger can also fail.

New shortcuts retain **39-bit addressing and CPU cores 0-3 (mask 0xF)**.
This grants access to those cores; it does not force multithreading/overclocking.

## Previous 0.6.1 repair retained

0.6.0's in-app RomFS builder reversed the later file nodes when there were
three or more files. USB shortcuts use four: arguments, NRO path, USB path and
executable hash. This paired filenames with the wrong lengths and could overlap
payloads. For nfsmw-nx, the 34-byte launch path was declared as 32 bytes and the
32-byte hash as 34 bytes; the loader rejected the malformed metadata.

0.6.1 preserves input order. Tests compile the actual builder and embedded
loader reader and check every byte, size, offset and hash-chain lookup, including
2,000 variable-size cases. This fixes a reproduced packaging defect, but the
resulting forwarder's launch still needs confirmation on the console.

Updating the app alone does not update installed shortcuts. Recreating one is
necessary for its new metadata, permissions and now the embedded startup logger.
No firmware/package3/HOC change or reinstall of USB games is needed.

New forwarders use **39-bit address space and CPU cores 0-3 (mask 0xF)** in
both NPDM capability lists. The included HOME launcher uses the same permissions.
This allows a homebrew port to use all four cores; it does not force a
single-threaded port to use four cores or change CPU clock speeds.

## New installation

Keep the working Nexus Virtual SD 0.5.1 companion (package3) installed. This
release does not replace firmware, HOC, your splash or the firmware version.

1. Back up the existing `SD:/switch/usbloader/USBLoader.nro`, then copy the
   ZIP's `switch` folder to the real microSD root.
2. Install `USB-Loader-HOME-0.6.5.nsp` to **Internal Storage** with your existing
   installer. This is a small HOME launcher, not a self-contained copy of the
   app: the NRO must remain at `SD:/switch/usbloader/USBLoader.nro`.
3. Start **USB Loader** from HOME. No R-title override is needed. This NSP
   embeds the matching full-memory, 39-bit/four-core Virtual SD-aware loader.
4. Open **Installed Games**. It scans USB `/switch` and nested folders for
   valid NROs, showing their embedded names/icons and **Homebrew/USB** labels.
5. Select a homebrew entry -> **Create HOME forwarder** -> **Create**.
   The small shortcut installs to Internal Storage; the NRO and all port data
   stay on USB. Exit the app and start the shortcut directly from HOME.

Keep USB connected. After updating/moving an NRO, remove its old shortcut in
System Settings and recreate it. Existing titles are never silently replaced.
The shortcut verifies the NRO executable hash; changed/missing targets or a
missing companion return to HOME rather than opening a same-named SD file.

The user confirmed the 0.5.1 USB NRO route and 0.6.0 USB Loader HOME NSP work.
The generated-forwarder still froze in the user's 0.6.1 test. This release adds
package export and startup diagnostics to locate the remaining failure.
Universal NRO compatibility is not promised.

## 0.6.0 library and forwarder details

- USB NRO rows are separate from Horizon application IDs; no synthetic IDs are
  sent to NS or used for installed-content move/delete operations.
- Forwarders are identified by a bounded read of an actual `nextNroPath` in
  their program RomFS, including common plaintext/AES-CTR homebrew forwarders.
  Recognized entries say **Forwarder / Internal**, **Forwarder / SD**, etc.
  Arbitrary custom loaders without that marker cannot be identified reliably.
- Fixed newly generated forwarder CNMT dependent-record count and meta NCA
  size. Existing malformed records may be inspected for library recognition,
  but are not rewritten and still face strict verification before moves.
- NRO scanning is cancellable, generation-bound and bounded (4096 folders,
  4096 NROs, 65536 entries, 32 directory levels, 255-byte physical paths).
  Invalid NRO files are skipped. Missing/unplugged USB does not remove the
  normal installed library. Scan failures are recorded in `nexus.log`.
- NRO assets are range-checked; icon data is capped at 1 MiB and decoded with
  pixel/dimension limits. Only visible grid icons stay in GPU memory.
- NRO launch reads the executable size, never a multi-gigabyte heap-sized
  filesystem request. Media-generation and handoff checks stay enabled.
- No keys, tokens or game files are bundled. No GitHub upload was performed.

## 0.5.1 changes

- microSD capacity now comes from the physical SD root filesystem, not NCM's
  logical content store. USB remains an independent shared-service query.
- Dedicated **USB Homebrew** section, with normal `switch/` browsing and
  isolated `homebrew/` profiles. The previous 0x1759 rejection of an NRO under
  `/switch/nfsmw-nx/nfsmw-nx.nro` is addressed by the opt-in USB-root route.
- **Run USB diagnostics** bundles VsdProbe inside USBLoader.nro. It prepares
  a new USB test profile, verifies every byte, then offers Launch. No separate
  probe download or manual file copy is needed.
- USB-root homebrew requires the matching 0.5.1 firmware package and full-memory
  R-title override; this is not enabled by the app NRO alone. See the matched ZIP.

## 0.5.0 changes

- Experimental USB NRO launch from USB Source through the matching Nexus
  Virtual SD v1 companion and modified homebrew loader. No port rebuild needed
  for the normal SD filesystem route; universal compatibility is not promised.
- Per-launch USB profile roots, explicit confirmation, media-generation checks
  and loader capability checks. SD and installed-game routes are unchanged.
- See `HOMEBREW-USB-NOTES.md` for setup, save locations and limitations.

## 0.4.1 changes

- One case-insensitive format classifier for the package list and installer:
  NSP, NSZ, XCI and XCZ. These were already connected to the install engine;
  this update hardens parsing, rather than introducing XCI support from scratch.
- Pass the known source size into both container readers. Reject truncated
  headers/tables, excessive counts, invalid names and out-of-bounds file ranges.
- XCI/XCZ installation reads only the secure game-content partition. The
  cartridge firmware-update partition is not installed as game content.
- Added host tests that compile the real PFS0/HFS0 parsers against synthetic
  bounded sources, including standard and key-area-prefixed XCI layout.

Supported single-file packages: `.nsp`, `.nsz`, `.xci`, `.xcz`, including uppercase
extensions. Both SD Source and USB Source use the same engine and can target SD,
Internal Storage or USB. NSZ/XCZ use the existing NCZ decompressor; block and
solid compression code is retained. Console installs and decompression were not
exercised by the synthetic parser tests.

Split files such as `.ns0`, `.xc0` and `.nsp.00` are not supported by this build.
ZIP/RAR/7z must be extracted first. Raw NCA, NRO, and MSP mod archives are not
offered as installable games. Do not rename unsupported files to a supported
extension. Use complete, valid dumps; an extension alone is not validation.

The 0.4.1 release did not include homebrew launching; 0.5.0 added the opt-in
prototype described in `HOMEBREW-USB-NOTES.md`.

## 0.4.0 changes

- Multi-select package files across folders on one source drive; review/reorder
  a queue of up to 256 files, then install to SD, Internal Storage or USB.
- Separate per-file results and automatic `switch/usbloader/batch-last.log`.
  Stop on the first error/cancellation; keep completed installs and all sources.
- Native microSD capacity query: the microSD widget no longer reads the SD
  content interface whose capacity is redirected to USB by the companion.
  If native-SD querying fails, show Unavailable rather than USB capacity.
- Pin the USB destination generation for the entire batch. Reconnecting or
  replacing the drive stops the queue; it cannot redirect later packages.

This is a locally built preview. Host tests do not substitute for console tests.

## 0.3.1 changes and USB-to-SD investigation

- USB Loader styling for errors, confirmations, progress, action sheets and lists;
  wrapped messages, A/B/touch error dismissal, no upstream issue link in dialogs.
- Compare NCM keys by ID/version/type/install type, ignoring unused padding as NCM
  itself does. Shared-reference scanning now includes fragment-only records too.
- Prevalidate all USB deletion paths before removal. Keep hash, metadata, physical
  destination and media-generation checks; never delete via routed SD storage.
- Failed removal reports its stage and confirmed deletion count in the dialog
  and `switch/usbloader/nexus.log`. Failures before removal say nothing was removed.

The reported 0x1759 is an invalid-input/safety-check error, not a unique diagnosis.
The exact failing check from the earlier console run is not established. The key
comparison correction is host-tested; this build's move needs console validation.
Do not delete either copy or run Cleanup to resolve this error. First confirm that
the SD copy launches with USB disconnected. Keep other installers closed. If the
same move is retried, already verified destination files are reused, not recopied.
If removal still stops, send the full dialog and `switch/usbloader/nexus.log`.

Nothing has been published to GitHub. Source remains local per the user's request.

## Overview

A separate, Sphaira-based homebrew app for the existing Nexus USB content service.
Includes a USB Loader app icon, matching vector menu icons, and a navy/cyan theme.
Only Installed Games, SD Source, USB Source and Cleanup are exposed. The inherited
app store, homebrew launcher, general tools, network servers, theme browser,
music player and extra settings pages have been removed from the app interface.
Their unused upstream source files are retained for provenance, but their menu
implementations are not compiled into this build. This package does **not**
replace Atmosphere, HOC, Hekate, package3, hbmenu, or your existing Sphaira app.

This is a compiled preview, not a console-verified release. Back up important
data before testing. Do not start with source removal or cleanup.

## Setup

1. Copy the package's `switch` folder to the root of the Switch SD card.
2. For installs/moves, keep your working Nexus firmware. USB NRO launching
   additionally requires the matched Virtual SD v1 firmware/hbl preview.
3. Open the homebrew menu in full-memory application mode, not Album applet mode
   (normally hold R while launching a game), then select **USB Loader**.
4. The dashboard has Installed Games, SD Source, USB Source, and Cleanup cards.
   Three widgets show physical microSD, System and USB free space/status.
   USB status refreshes asynchronously; polling pauses during modal operations.
   Press minus for Updates; only TechRepairs4U/USB-Loader is contacted.
   A opens, B goes back, X refreshes, and the direction buttons select items.

Settings are separate: `sdmc:/config/usbloader/`. App data and the bounded USB
operation log are in `sdmc:/switch/usbloader/`; the log is `nexus.log`.
The general Sphaira log may also include file names, title IDs and error details.
Review logs before sharing them. No console keys, tickets or tokens are bundled.

## Installing packages

- Select **SD source** or **USB source**, browse to a package, and press A.
- Choose **SD**, **Internal**, or **USB**, then confirm.
- For a batch, press **Y** to select/deselect files, or **X** to select/deselect
  all supported packages in the current folder (not its subfolders). Selections
  remain while browsing other folders on the same source.
- Press **+** to review the queue. Selection order is installation order; use
  **L/R** to move a package earlier/later and **Y** to remove it. Arrange the base,
  update and DLC in that order. Types are not guessed from filenames.
- Press **+** on the queue to choose one destination for every selected package.
  Files cannot be mixed from both source drives in a single queue. Internal
  Storage is an install destination, not a package-file source browser.
- Before writing, the queue checks every source file and destination availability.
  Actual expanded-space checks remain per package: the displayed combined
  package size is not the required installed size, especially for NSZ/XCZ.
- Results show Installed, Failed, Cancelled and Not attempted separately.
  Successful files are deselected. Failed/unattempted selections stay available
  under **+ Remaining** for manual review; nothing retries automatically.
- The queue is in memory, not a persistent resume journal. The last report is
  saved after each result and overwritten by the next batch. Cancellation/failure
  can leave the current package partially installed. It does not roll back earlier
  successful packages. Source files are always kept.
- The installer accepts the Sphaira NSP/XCI/NSZ/XCZ container formats; this
  preview's USB integration for each format still needs hardware testing.
- USB source and USB destination may be the **same drive**. Both use the shared
  Nexus service instead of competing for the USB hardware.
- Install a game's base, update and DLC to the same destination. Metadata
  relationships are preserved and existing application records are merged.
- Original package files are kept. Existing registered content is not deleted
  to make a reinstall succeed. Matching IDs must match byte-for-byte before reuse.
- The shared service negotiates up to 4 MiB transfers. No speed is promised:
  source reads, decompression, hashing and same-drive writes all consume time.
- USB metadata/control parsing temporarily uses an owned, unregistered internal
  placeholder, removed afterward. Allow up to 64 MiB of internal scratch space.
  Game payloads still go to the chosen target.

Native SD/internal installs are intended to work without the USB companion when
its absence is positively established. USB requires the Nexus R166-compatible
`ams:usb` interface and isolated native-SD command 65030. Unknown service errors
stop the operation; they never silently redirect a USB choice to SD.

## Installed icon grid and moving games

**Installed Games** checks physical content, then shows a regular 4-by-2 icon grid
with names and SD/Internal/USB labels. All icons are the same size; there is no
cover flow. Up/down moves by a row, left/right moves by an item, and pages change
as the selection advances. Tap to select a tile, then tap again to open its actions. These are local installed-app icons, not downloaded cover
art. Mixed locations show multiple labels; unavailable storage is not guessed.

Choose a title, then a move direction. All six distinct routes are supported:
SD to/from USB, Internal Storage to/from USB, and SD to/from Internal Storage.
Only physically detected sources are offered. A move includes complete
base/update/DLC records on that source. If parts of a title are on multiple
sources, handle each source separately; incomplete records abort.

The copy operation:

1. Checks the chosen physical destination and free space, then copies missing content.
2. Reads both copies completely and compares SHA-256 digests.
3. Commits full content metadata and updates the launch records.
4. Asks **Keep original** (default) or **Finish move**.

Finish move re-verifies both copies and metadata, checks launch records, and
retains content referenced by other records. Only unshared content on the selected physical source is removed; content still
referenced by another record is retained. This move operation never deletes saves, tickets or system
content. SD and USB share a logical metadata database; its records are retained, even
when the other copy may be on a third storage location. USB source removal uses
the owner's generation-checked file deletion, never a routed SD delete.

For initial testing, choose **Keep original**, exit, and verify launch, updates
and DLC from the destination first. While copies exist on BOTH SD and USB, the
mounted USB copy can take priority. To test the SD copy, exit the app and safely
disconnect USB before launching. Reconnect USB before completing a USB-source
move. When satisfied, repeat the same route: matching destination files are
verified and reused, and the removal choice is offered again.

There is no persistent transaction journal or automatic resume. If an operation
fails or power is lost, do not assume it finished; the original is retained during
copy, but partial target files may remain. Source removal is deliberately separate.
Keep other installers/downloads closed during moves or cleanup.

## Cleanup

Cleanup scans the chosen SD/Internal/USB storage for unfinished NCM placeholders.
The scan is read-only. Each deletion requires review of its ID/size and explicit
confirmation that installers are stopped and the failed install is unwanted.
The app rechecks the candidate and USB media generation before removal.

**A placeholder is not automatically proven stale.** There is no automatic
cleanup, no bulk deletion, and no deletion of arbitrary registered NCAs. Orphaned
*registered* content from a failure after registration is intentionally not
reclaimed by this preview. Removed placeholders are not recoverable; reinstall
from the original package if needed.

## First console test

1. Check the four menu icons, Installed game-icon grid, navigation and storage labels.
2. Check SD and USB source browsing with USB mounted; verify SD still shows SD files.
3. Install a small backed-up/homebrew package from SD to USB and test launch.
4. Repeat with its package on USB (USB to the same USB).
5. Test a title's base/update/DLC together, then test a move with **Keep original**.
6. Test USB to SD and USB to Internal with **Keep original**, and launch the copies.
   Remember the SD/USB duplicate priority above. Check base/update/DLC together.
7. Only after those succeed, test source removal and one known abandoned placeholder.

Tests run here: Switch cross-compilation, host routing/path/range/snapshot and
six-direction policy tests, grid navigation/icon-geometry tests, focused-menu
source contracts, batch ordering/limits/preflight/failure/cancellation/reconnect
tests, source whitespace checks, and packaged NRO/icon/metadata checks. No console UI,
install, migration, cleanup, hot-unplug or power-loss tests have been performed.

For this release, also compare microSD and USB free space with both connected,
then test a small base/update/DLC queue to each destination. Use backed-up or
homebrew packages. Verify the installed update and DLC after launch, not just
the installer result. No firmware change is included in this app update.

## App updates

The manual updater includes compatible preview releases from
https://github.com/TechRepairs4U/USB-Loader/releases. It verifies HTTPS,
SHA-256, asset size, app name and version before installing. Run the canonical
switch/usbloader/USBLoader.nro on SD. The old app is retained as a .nro.bak file.
No firmware or game data is touched. Exit and reopen after an update.
There is no background/automatic update check. See docs/UPDATES.md in the source.

## Source and credits

Based on [Sphaira 1.0.7](https://github.com/NaGaa95/sphaira/releases/tag/1.0.7),
commit `99b6f08fd5c3effeb99df0130850403bd150fb33`. GPL-3.0; see LICENSE and the
upstream README. Existing third-party notices are preserved. This is a separate
fork, not an official Sphaira release. Shared USB client adapted from this
project's Goldleaf Nexus G2 client. The generated app icon's prompt/provenance
is in `assets/branding/ICON-PROMPT.md` in the source archive.

See BUILDING-USB-LOADER.md for the build recipe. The source archive includes the
exact fetched dependency trees used for this build; system SDK/toolchain libraries
are external requirements.
