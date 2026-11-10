#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace haze { struct FileSystemProxyImpl; }

namespace sphaira::libhaze {

bool Init(const std::vector<std::shared_ptr<haze::FileSystemProxyImpl>>* extensions = nullptr);
// Nexus foreground MTP uses Sphaira's native file browser, without enabling
// the legacy auto-start server, raw NAND views or a second USB host owner.
std::shared_ptr<haze::FileSystemProxyImpl> MakeNexusSdProxy();
bool IsInit();
void Exit();

using OnInstallStart = std::function<bool(const char* path)>;
using OnInstallWrite = std::function<bool(const void* buf, size_t size)>;
using OnInstallClose = std::function<void()>;

void InitInstallMode(const OnInstallStart& on_start, const OnInstallWrite& on_write, const OnInstallClose& on_close);

// false once install mode has been (or is being) torn down. polled by the
// install callbacks so that they never block indefinitely.
bool IsInstallModeEnabled();

// stops accepting installs and waits for every in-flight install callback to
// return. never call this whilst holding a lock that a callback may need.
void DisableInstallMode();
void FinishInstallProgress();

} // namespace sphaira::libhaze
