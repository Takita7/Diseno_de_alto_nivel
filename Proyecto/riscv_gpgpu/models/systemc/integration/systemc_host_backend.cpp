#include "kernel_bridge.h"
#include "systemc_host_backend.h"
#include "../../../software/host_api/host_api_backend.h"
#include "../../../driver/src/loader.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace riscv_gpgpu {
namespace {

class TemporaryElf {
public:
    ~TemporaryElf() {
        if (fd_ >= 0) close(fd_);
        if (!path_.empty()) unlink(path_.c_str());
    }

    bool create(const std::vector<uint8_t>& image, std::string& error) {
        char path[] = "/tmp/riscv_gpgpu_XXXXXX.elf";
        fd_ = mkstemps(path, 4);
        if (fd_ < 0) {
            error = std::string("Cannot create temporary ELF: ") + std::strerror(errno);
            return false;
        }
        path_ = path;
        size_t written = 0;
        while (written < image.size()) {
            ssize_t count = write(fd_, image.data() + written, image.size() - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) {
                error = std::string("Cannot write temporary ELF: ") + std::strerror(errno);
                return false;
            }
            written += static_cast<size_t>(count);
        }
        if (close(fd_) != 0) {
            fd_ = -1;
            error = std::string("Cannot close temporary ELF: ") + std::strerror(errno);
            return false;
        }
        fd_ = -1;
        return true;
    }

    const std::string& path() const { return path_; }

private:
    int fd_ = -1;
    std::string path_;
};

bool executeWithSystemC(const KernelLaunchArgs& launch,
                        const std::vector<uint8_t>& elf_image,
                        std::string& error) {
    if (elf_image.empty()) {
        error = "SystemC backend received an empty ELF image";
        return false;
    }

    TemporaryElf elf;
    if (!elf.create(elf_image, error)) return false;

    KernelBridge::Config config;
    config.threads_per_warp = 1;
    config.print_stats = false;
    KernelBridge bridge(config);

    try {
        if (!bridge.runOnHardware(launch.kernel_name,
                                  elf.path(),
                                  launch.args,
                                  getAllocatedDeviceBufferAddresses())) {
            error = bridge.lastError().empty() ? "KernelBridge execution failed" : bridge.lastError();
            return false;
        }
    } catch (const std::exception& exception) {
        error = std::string("SystemC backend exception: ") + exception.what();
        return false;
    } catch (...) {
        error = "SystemC backend raised an unknown exception";
        return false;
    }
    return true;
}

}

void installSystemCKernelExecutionBackend() {
    installKernelExecutionBackend(&executeWithSystemC);
}

}
