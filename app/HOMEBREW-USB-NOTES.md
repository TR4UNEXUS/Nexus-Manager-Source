# USB homebrew / USB Loader 0.6.5-preview

0.6.5 adds startup/shutdown time limits and interactive busy/cancellation
handling. Use its matching HOME NSP and NRO; no firmware/HOC changes.
The guard stops before launching another NRO and never times out a running
homebrew game. It does not reset USB or force-close an active install/write.
See the top of NEXUS-README.md for installation, test and recovery limits.

## Previous 0.6.4 logger repair

0.6.4 repairs undersized filesystem IPC path buffers in both startup loggers
and the diagnostic forwarder export. It adds a visible app logging error when
possible. Install the matching 0.6.4 HOME NSP and NRO; keep firmware/HOC/system
hbl.nsp unchanged. See the current test at the top of NEXUS-README.md.
The app-switch and News freezes have NOT yet been explained or confirmed fixed.

## Previous 0.6.3 diagnostics

0.6.3 adds bounded app-startup traces plus earlier paired service markers in
the embedded HOME launcher. Install the matching 0.6.3 HOME NSP as well as the
NRO for this diagnostic. Keep package3, HOC and atmosphere/hbl.nsp unchanged.
See NEXUS-README.md for the one baseline/one after-install test and both log
directories. This does not claim to fix the freeze or USB game routing.

## Previous diagnostics

0.6.2 is a targeted diagnostic release for the remaining HOME shortcut freeze.
Generated shortcuts are also saved as inspectable NSPs; the embedded HBL logs
startup stages to `SD:/switch/usbloader/forwarder-debug/`. Recreate the shortcut
using a matching build updates that embedded diagnostic code.
Keep working firmware unchanged. See NEXUS-README.md for exact steps/limitations.

0.6.1 fixes malformed RomFS file sizes in app-created USB HOME shortcuts.
Replace the app NRO, then remove/recreate only the broken shortcut. Existing
shortcuts contain their own metadata and are not repaired by an NRO update.
Firmware stays installed; use the 0.6.5 HOME launcher for the current startup test.
Newly created shortcuts grant 39-bit address space and CPU cores 0-3 (mask 0xF).
See NEXUS-README.md for the exact upgrade steps and test limitations.

USB Loader can request an opt-in virtual SD view for one NRO launch. Keep the
working **0.5.1 Nexus Virtual SD v1 companion package3**. Start this release's
USB Loader HOME NSP (matching loader embedded), or use full-memory R-title
override with the modified atmosphere/hbl.nsp. The NRO alone cannot enable USB.
This is not a VM: the unmodified NRO runs on the Switch normally, while its
SD filesystem requests are routed to the confirmed USB root or isolated profile.
The new **Create HOME forwarder** option installs a small launcher, not the NRO
or its data, to Internal Storage. USB `/switch/**/*.nro` entries also appear in
Installed Games as **Homebrew/USB**. See NEXUS-README.md for setup.

## Built-in diagnostics: no separate NRO to copy

Open **USB Homebrew -> Run USB diagnostics -> Prepare test -> Launch**.
The probe is embedded in USBLoader.nro. The app copies it to a NEW private
`USB:/homebrew/usbloader-probe-<random>/sdmc/switch/VsdProbe.nro` and verifies
every byte before launch. It checks for an active USB mapping before writing
test data; it never silently tests the real SD. Photograph PASS/FAIL results.
B returns to USB Loader when using its new HOME NSP (hbmenu with R override).
Test folders/probe copies remain on USB
for inspection; the probe only deletes test files it created during a passing
run. They are not installed games and can be removed later while no port runs.

## Normal USB layout: nfsmw-nx

Your existing `USB:/switch/nfsmw-nx/nfsmw-nx.nro` is now accepted. Open
**USB Homebrew -> switch -> nfsmw-nx -> nfsmw-nx.nro**, then confirm Launch.
Keep the port's full data/config layout on USB just as it would be on SD.
During that launch, `sdmc:/switch/...` maps to `USB:/switch/...` and all other
ordinary SD paths map to the same relative USB paths. No file move is needed.
The 0.5.0 preview rejected this path before launch with 0x1759; install the
**matched 0.5.1 companion** for the new route and reboot, not only the app.

**Only use USB-root mode for trusted homebrew.** It can write the whole USB
filesystem, including installed-game directories, just as it could write SD.
This is not a security sandbox. The real microSD is not merged or used as a
fallback. Existing SD saves/configs are NOT copied. Back them up before testing.

## Optional isolated profile

Copy (do not move) the port's complete SD folder layout onto USB:

```
homebrew/nfsmw/sdmc/switch/nfsmw-nx/nfsmw-nx.nro
homebrew/nfsmw/sdmc/switch/nfsmw-nx/nfsmw.toml
homebrew/nfsmw/sdmc/switch/nfsmw-nx/nfsmw_shaders.nfsp
homebrew/nfsmw/sdmc/switch/nfsmw-nx/game_root/...
```

Everything below `homebrew/nfsmw/sdmc` is the port's private virtual SD root.
Include any other folders the port normally requires on its SD root.
The app sees `sdmc:/switch/nfsmw-nx/nfsmw-nx.nro`, not a `usb:/` argument.
Use letters, numbers, hyphens and underscores for the profile name (`nfsmw`).

1. Back up saves and install the preview with its checked, reversible installer.
2. Fully reboot if changing the companion. Open the new **USB Loader HOME NSP**,
   or use full-memory R-title override. Old embedded-loader forwarders may not
   support Virtual SD. Do not use Album applet mode.
3. Choose **USB Homebrew**, browse through homebrew to that NRO, press **A**,
   then **Launch**. USB Source also retains NRO launch when its queue is empty.
4. Verify game data, a disposable new save, load, exit, and normal hbmenu/SD
   behaviour afterward. Never unplug USB while the port is using it.

All ordinary `sdmc:/` files for this NRO, including saves/config/cache, use the
USB profile. Existing SD saves are **not** copied automatically. Back up and
copy them into the same relative location yourself if needed. This mode does
not merge SD and USB. A missing USB file remains missing; no silent SD fallback.
System saves accessed through other Horizon interfaces are not redirected.

## Scope and limits

- User confirmed the USB NRO route and the USB Loader HOME app work.
  App-generated 0.6.1 shortcuts still froze; 0.6.2 adds diagnostics, not a
  claimed fix for that remaining failure.
  No promise of all-NRO compatibility.
- Normal reads, writes, seeking, stat, directory listings, creation, deletion,
  renaming, size changes, flush and free-space queries are implemented.
- Recursive delete/clean and advanced range/query operations are unsupported
  in v1. Timestamps are reported as unavailable. USB filesystem rules still apply.
- The shared backend limits complete physical USB paths to 255 bytes, including
  the profile prefix. Deep or long port filenames can exceed that limit.
- Ports must use the normal SD filesystem service. Raw SD-sector tools,
  applications with their own USB driver, unusual loaders or process restarts,
  and apps requiring unavailable permissions may not work.
- NRO-to-NRO chainloads revoke the view. Only the explicitly selected NRO gets
  the profile; this is not a multi-process container or a security sandbox.
- RAM, 39-bit address-space, firmware and GPU/CPU requirements still apply.
  The inspected nfsmw-nx upstream requires full application memory and normally
  terminates the host process on exit instead of returning to hbmenu.
- An unplugged/replaced USB device or expired launch request fails; it does not
  switch writes to SD. An interrupted port can still lose unsaved USB data.
- File handles use path-based operations on the existing shared backend.
  Do not rename/delete active port files from another app while it runs.

The modified loader introduces its PID using a real process handle and performs
an explicit handoff before each SD mount. A matching request expires after 30
seconds. Leases bind filesystem/file/directory objects to that session and USB
generation. Return/chainload clears the session; terminated processes are reaped
on the next service access. No mapping is persisted across reboot.

Installed-game routing and the existing shared USB owner are retained. No raw
USB driver is started by this launcher. Normal app installs still work without
Virtual SD support; only the NRO launch option requires this matched preview.

No keys, tokens or game files are bundled. Nothing was uploaded to GitHub.
