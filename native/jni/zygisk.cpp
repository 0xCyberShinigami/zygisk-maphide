#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/mman.h>
#include <string>
#include <sys/stat.h>

#include "zygisk.hpp"
#include <lsplt.hpp>

#define LOG_TAG "MapHide"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class MapHide : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        uint32_t flags = api->getFlags();
        if ((flags & zygisk::PROCESS_ON_DENYLIST) && args->uid > 1000) {
            LOGI("App on denylist detected, starting hide/unhide loop");
            std::thread(&MapHide::toggleLibsslVisibility, this).detach();
        }
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api;
    JNIEnv *env;

    void toggleLibsslVisibility() {
        const std::string libssl_path = "/apex/com.android.conscrypt/lib64/libssl.so";

        while (true) {
            hideLibssl(libssl_path);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Hide for 500ms
            unhideLibssl(libssl_path);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Unhide for 500ms
        }
    }

    void hideLibssl(const std::string &lib_path) {
        auto maps = lsplt::MapInfo::Scan();
        for (auto iter = maps.begin(); iter != maps.end();) {
            const auto &region = *iter;
            if (region.path == lib_path) {
                LOGI("Hiding libssl region: %zx-%zx", region.start, region.end);
                iter = maps.erase(iter);
            } else {
                ++iter;
            }
        }
        applyMaps(maps);
    }

    void unhideLibssl(const std::string &lib_path) {
        auto maps = lsplt::MapInfo::Scan();
        for (const auto &region : maps) {
            if (region.path == lib_path) {
                LOGI("Unhiding libssl region: %zx-%zx", region.start, region.end);
                // Re-add the region (no-op for now since it’s handled by applyMaps in practice)
                break;
            }
        }
        applyMaps(maps);
    }

    void applyMaps(const std::vector<lsplt::MapInfo> &maps) {
        for (const auto &region : maps) {
            LOGI("Applying map: %zx-%zx %s", region.start, region.end, region.path.c_str());
            void *addr = reinterpret_cast<void *>(region.start);
            size_t size = region.end - region.start;
            void *copy = mmap(nullptr, size, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (copy == MAP_FAILED) {
                LOGE("Failed to create anonymous mapping for region: %zx-%zx", region.start, region.end);
                continue;
            }
            memcpy(copy, addr, size);
            mremap(copy, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
        }
    }
};

static void companion_handler(int client) {
    return;
}

REGISTER_ZYGISK_MODULE(MapHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
