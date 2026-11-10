// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace nexus::usbimport {
// The UI runs one import worker. Only that worker may pass the app's import
// journal gate; an interrupted import blocks subsequent installs and moves.
inline thread_local bool worker_authorized=false;
struct WorkerScope {
    bool previous=worker_authorized;
    WorkerScope(){worker_authorized=true;}
    ~WorkerScope(){worker_authorized=previous;}
    WorkerScope(const WorkerScope&)=delete;
};
}
