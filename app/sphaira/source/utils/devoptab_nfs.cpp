#include "utils/devoptab_common.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "nexus/nx_Client.hpp"

#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <string>
#include <cstring>
#include <libnfs.h>
#include <minIni.h>

namespace sphaira::devoptab {
namespace {

struct Device final : common::MountDevice {
    using MountDevice::MountDevice;
    ~Device();
    bool Mount() override;
    bool CheckRootAccess();
    const std::string& FailureDetail() const { return failure_detail; }

private:
    bool Fail(const char* phase, int error, const std::string& message);
    int devoptab_open(void *fileStruct, const char *path, int flags, int mode) override;
    int devoptab_close(void *fd) override;
    ssize_t devoptab_read(void *fd, char *ptr, size_t len) override;
    ssize_t devoptab_write(void *fd, const char *ptr, size_t len) override;
    ssize_t devoptab_seek(void *fd, off_t pos, int dir) override;
    int devoptab_fstat(void *fd, struct stat *st) override;
    int devoptab_unlink(const char *path) override;
    int devoptab_rename(const char *oldName, const char *newName) override;
    int devoptab_mkdir(const char *path, int mode) override;
    int devoptab_rmdir(const char *path) override;
    int devoptab_diropen(void* fd, const char *path) override;
    int devoptab_dirreset(void* fd) override;
    int devoptab_dirnext(void* fd, char *filename, struct stat *filestat) override;
    int devoptab_dirclose(void* fd) override;
    int devoptab_lstat(const char *path, struct stat *st) override;
    int devoptab_ftruncate(void *fd, off_t len) override;
    int devoptab_statvfs(const char *path, struct statvfs *buf) override;
    int devoptab_fsync(void *fd) override;
    int devoptab_utimes(const char *path, const struct timeval times[2]) override;

private:
    nfs_context* nfs{};
    bool mounted{};
    std::string failure_detail;
};

struct File {
    nfsfh* fd;
};

struct Dir {
    nfsdir* dir;
};

std::string NfsFailureMessage(int error, bool root) {
    std::string message = root
        ? "Connected to the NFS server, but could not read the exported folder. "
        : "Could not connect to the NFS export. ";
    switch (error) {
        case EACCES:
            message += "The server denied access. Allow the Switch's IP address in the NFS export permissions and allow reading its folders.";
            break;
        case ENOENT:
        case ENOTDIR:
            message += "The exported folder was not found. Enter the full export path exactly as shown by your NAS or NFS server.";
            break;
        case ETIMEDOUT:
        case EHOSTUNREACH:
        case ENETUNREACH:
        case ECONNREFUSED:
        case ENOTCONN:
            message += "The server did not respond or its NFS service is unavailable. Check the server address, enable NFS, and keep both devices on the same network.";
            break;
        default:
            // libnfs also uses -1 for generic setup failures, so it must not
            // be interpreted as proof of an EPERM permission denial.
            message += "Check the server address and full export path, and allow the Switch's IP address in the server's NFS permissions.";
            break;
    }
    return message;
}

bool Device::Fail(const char* phase, int error, const std::string& message) {
    failure_detail = message;
    if (error > 0) failure_detail += "\n\nNFS error: " + std::to_string(error) + ".";
    // The ordinary debug log may be disabled. Keep connection-stage results
    // in the same durable report as transfers, without addresses or logins.
    nexus::Log(phase, MAKERESULT(Module_Libnx, LibnxError_IoError), error > 0 ? error : 0);
    return false;
}

Device::~Device() {
    if (nfs) {
        if (mounted) {
            nfs_umount(nfs);
        }

        nfs_destroy_context(nfs);
    }
}

bool Device::Mount() {
    if (mounted) {
        return true;
    }

    log_write("[NFS] Mounting %s\n", this->config.url.c_str());
    failure_detail.clear();
    nexus::Log("network-nfs-connect");

    if (!nfs) {
        nfs = nfs_init_context();
        if (!nfs) {
            log_write("[NFS] nfs_init_context() failed\n");
            return Fail("network-nfs-context", ENOMEM, "Nexus Manager could not create the NFS connection. Close other network browsers and try again.");
        }

        const auto uid = this->config.extra.find("uid");
        if (uid != this->config.extra.end()) {
            const auto uid_val = ini_parse_getl(uid->second.c_str(), -1);
            if (uid_val < 0) {
                log_write("[NFS] Invalid uid value: %s\n", uid->second.c_str());
            } else {
                log_write("[NFS] Setting uid: %ld\n", uid_val);
                nfs_set_uid(nfs, uid_val);
            }
        }

        const auto gid = this->config.extra.find("gid");
        if (gid != this->config.extra.end()) {
            const auto gid_val = ini_parse_getl(gid->second.c_str(), -1);
            if (gid_val < 0) {
                log_write("[NFS] Invalid gid value: %s\n", gid->second.c_str());
            } else {
                log_write("[NFS] Setting gid: %ld\n", gid_val);
                nfs_set_gid(nfs, gid_val);
            }
        }

        const auto version = this->config.extra.find("version");
        if (version != this->config.extra.end()) {
            const auto version_val = ini_parse_getl(version->second.c_str(), -1);
            if (version_val != 3 && version_val != 4) {
                log_write("[NFS] Invalid version value: %s\n", version->second.c_str());
            } else {
                log_write("[NFS] Setting version: %ld\n", version_val);
                nfs_set_version(nfs, version_val);
            }
        }

        if (this->config.timeout > 0) {
            nfs_set_timeout(nfs, this->config.timeout);
        }
        nfs_set_readonly(nfs, this->config.read_only);
        if (this->config.read_only) {
            // The default hard mount retries forever even after the RPC
            // timeout. Read-only copy/install sessions must return an error
            // on disconnect so their protected worker can finish safely.
            nfs_set_autoreconnect(nfs, 0);
            nfs_set_retrans(nfs, 0);
        }
        // nfs_set_mountport(nfs, url->port);
    }

    // fix the url if needed.
    auto url = this->config.url;
    if (!url.starts_with("nfs://")) {
        log_write("[NFS] Prepending nfs:// to url: %s\n", url.c_str());
        url = "nfs://" + url;
    }

    auto nfs_url = nfs_parse_url_dir(nfs, url.c_str());
    if (!nfs_url) {
        log_write("[NFS] nfs_parse_url_dir() failed for url: %s error: %s\n", url.c_str(), nfs_get_error(nfs));
        return Fail("network-nfs-url", EINVAL, "The NFS server or export address could not be read. Enter a hostname or IPv4 address in Server and the full exported folder path in Share / export.");
    }
    ON_SCOPE_EXIT(nfs_destroy_url(nfs_url));

    const auto ret = nfs_mount(nfs, nfs_url->server, nfs_url->path);
    if (ret) {
        log_write("[NFS] nfs_mount() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        const int error = ret < 0 ? -ret : ret;
        if (std::strstr(nfs_get_error(nfs), "RPC authentication rejected by server")) {
            return Fail("network-nfs-auth", error, "The NFS server rejected this connection's access. NFS uses the server's export permissions, not the SMB username or password. Allow the Switch's IP address and its NFS access method on the server.");
        }
        return Fail("network-nfs-mount", error, NfsFailureMessage(error, false));
    }

    log_write("[NFS] Mounted %s\n", this->config.url.c_str());
    nexus::Log("network-nfs-mounted", 0, nfs_get_version(nfs));
    return mounted = true;
}

bool Device::CheckRootAccess() {
    nfsdir* directory{};
    const auto ret = nfs_opendir(nfs, "/", &directory);
    if (ret || !directory) {
        const int error = ret < 0 ? -ret : ret ? ret : EIO;
        return Fail("network-nfs-root", error, NfsFailureMessage(error, true));
    }
    nfs_closedir(nfs, directory);
    nexus::Log("network-nfs-root");
    return true;
}

int Device::devoptab_open(void *fileStruct, const char *path, int flags, int mode) {
    auto file = static_cast<File*>(fileStruct);

    const auto ret = nfs_open(nfs, path, flags, &file->fd);
    if (ret) {
        log_write("[NFS] nfs_open() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_close(void *fd) {
    auto file = static_cast<File*>(fd);

    nfs_close(nfs, file->fd);
    return 0;
}

ssize_t Device::devoptab_read(void *fd, char *ptr, size_t len) {
    auto file = static_cast<File*>(fd);

    // todo: uncomment this when it's fixed upstream.
    #if 0
    const auto ret = nfs_read(nfs, file->fd, ptr, len);
    if (ret < 0) {
        log_write("[NFS] nfs_read() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return ret;

    #else
    // work around for bug upsteam.
    const auto max_read = nfs_get_readmax(nfs);
    if (len && !max_read) return -EIO;
    size_t bytes_read = 0;

    while (bytes_read < len) {
        const auto to_read = std::min<size_t>(len - bytes_read, max_read);
        const auto ret = nfs_read(nfs, file->fd, ptr, to_read);

        if (ret < 0) {
            log_write("[NFS] nfs_read() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
            return ret;
        }
        if (static_cast<size_t>(ret) > to_read) return -EIO;

        ptr += ret;
        bytes_read += ret;

        if (ret < to_read) {
            break;
        }
    }

    return bytes_read;
    #endif
}

ssize_t Device::devoptab_write(void *fd, const char *ptr, size_t len) {
    auto file = static_cast<File*>(fd);

    // unlike read, writing the max size seems to work fine.
    const auto max_write = nfs_get_writemax(nfs);
    size_t written = 0;

    while (written < len) {
        const auto to_write = std::min<size_t>(len - written, max_write);
        const auto ret = nfs_write(nfs, file->fd, ptr, to_write);

        if (ret < 0) {
            log_write("[NFS] nfs_write() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
            return ret;
        }

        ptr += ret;
        written += ret;

        if (ret < to_write) {
            break;
        }
    }

    return written;
}

ssize_t Device::devoptab_seek(void *fd, off_t pos, int dir) {
    auto file = static_cast<File*>(fd);

    u64 current_offset = 0;
    const auto ret = nfs_lseek(nfs, file->fd, pos, dir, &current_offset);
    if (ret < 0) {
        log_write("[NFS] nfs_lseek() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return current_offset;
}

int Device::devoptab_fstat(void *fd, struct stat *st) {
    auto file = static_cast<File*>(fd);

    const auto ret = nfs_fstat(nfs, file->fd, st);
    if (ret) {
        log_write("[NFS] nfs_fstat() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_unlink(const char *path) {
    const auto ret = nfs_unlink(nfs, path);
    if (ret) {
        log_write("[NFS] nfs_unlink() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_rename(const char *oldName, const char *newName) {
    const auto ret = nfs_rename(nfs, oldName, newName);
    if (ret) {
        log_write("[NFS] nfs_rename() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_mkdir(const char *path, int mode) {
    const auto ret = nfs_mkdir(nfs, path);
    if (ret) {
        log_write("[NFS] nfs_mkdir() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_rmdir(const char *path) {
    const auto ret = nfs_rmdir(nfs, path);
    if (ret) {
        log_write("[NFS] nfs_rmdir() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_diropen(void* fd, const char *path) {
    auto dir = static_cast<Dir*>(fd);

    const auto ret = nfs_opendir(nfs, path, &dir->dir);
    if (ret) {
        log_write("[NFS] nfs_opendir() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_dirreset(void* fd) {
    auto dir = static_cast<Dir*>(fd);

    nfs_rewinddir(nfs, dir->dir);
    return 0;
}

int Device::devoptab_dirnext(void* fd, char *filename, struct stat *filestat) {
    auto dir = static_cast<Dir*>(fd);

    const auto entry = nfs_readdir(nfs, dir->dir);
    if (!entry) {
        return -ENOENT;
    }

    if (std::strlen(entry->name) >= NAME_MAX) return -ENAMETOOLONG;
    std::strcpy(filename, entry->name);

    // not everything is needed, however we may as well fill it all in.
    filestat->st_dev = entry->dev;
    filestat->st_ino = entry->inode;
    filestat->st_mode = entry->mode;
    filestat->st_nlink = entry->nlink;
    filestat->st_uid = entry->uid;
    filestat->st_gid = entry->gid;
    filestat->st_size = entry->size;
    filestat->st_atime = entry->atime.tv_sec;
    filestat->st_mtime = entry->mtime.tv_sec;
    filestat->st_ctime = entry->ctime.tv_sec;
    filestat->st_blksize = entry->blksize;
    filestat->st_blocks = entry->blocks;

    return 0;
}

int Device::devoptab_dirclose(void* fd) {
    auto dir = static_cast<Dir*>(fd);

    nfs_closedir(nfs, dir->dir);
    return 0;
}

int Device::devoptab_lstat(const char *path, struct stat *st) {
    nfs_stat_64 info{};
    const auto ret = nfs_lstat64(nfs, path, &info);
    if (ret) {
        log_write("[NFS] nfs_lstat64() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }
    if (info.nfs_size > INT64_MAX) return -EOVERFLOW;
    st->st_dev=info.nfs_dev;st->st_ino=info.nfs_ino;st->st_mode=info.nfs_mode;
    st->st_nlink=info.nfs_nlink;st->st_uid=info.nfs_uid;st->st_gid=info.nfs_gid;
    st->st_size=info.nfs_size;st->st_blksize=info.nfs_blksize;st->st_blocks=info.nfs_blocks;
    st->st_atime=info.nfs_atime;st->st_mtime=info.nfs_mtime;st->st_ctime=info.nfs_ctime;
    return 0;
}

int Device::devoptab_ftruncate(void *fd, off_t len) {
    auto file = static_cast<File*>(fd);

    const auto ret = nfs_ftruncate(nfs, file->fd, len);
    if (ret) {
        log_write("[NFS] nfs_ftruncate() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_statvfs(const char *path, struct statvfs *buf) {
    const auto ret = nfs_statvfs(nfs, path, buf);
    if (ret) {
        log_write("[NFS] nfs_statvfs() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_fsync(void *fd) {
    auto file = static_cast<File*>(fd);

    const auto ret = nfs_fsync(nfs, file->fd);
    if (ret) {
        log_write("[NFS] nfs_fsync() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_utimes(const char *path, const struct timeval times[2]) {
    // todo: nfs should accept const times, pr the fix.
    struct timeval times_copy[2];
    std::memcpy(times_copy, times, sizeof(times_copy));

    const auto ret = nfs_utimes(nfs, path, times_copy);
    if (ret) {
        log_write("[NFS] nfs_utimes() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

} // namespace

Result MountNfsAll() {
    return common::MountNetworkDevice([](const common::MountConfig& cfg) {
            return std::make_unique<Device>(cfg);
        },
        sizeof(File), sizeof(Dir),
        "NFS"
    );
}

Result common::ConnectNfsShare(const common::MountConfig& config, fs::FsPath& mount, std::string& detail) {
    auto device = std::make_unique<Device>(config);
    if (!device->Mount() || !device->CheckRootAccess()) {
        detail = device->FailureDetail();
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    detail = "Too many network locations are open. Close the other network browsers and try again.";
    if (!common::MountNetworkSession(std::move(device), config, sizeof(File), sizeof(Dir), mount)) {
        nexus::Log("network-nfs-register", MAKERESULT(Module_Libnx, LibnxError_IoError));
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    nexus::Log("network-nfs-register");
    detail.clear();
    return 0;
}

} // namespace sphaira::devoptab
