#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <vector>
#include <iostream>
#include <sys/mman.h>
#include <string>
#include <sys/stat.h>

#include "zygisk.hpp"
#include <lsplt.hpp>
#include <android/log.h>

#define LOG_TAG "MapHide"

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)

// Helper Function to Apply Maps
static void applyMaps(const std::vector<lsplt::MapInfo> &maps) {
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

// Helper Function to Hide libssl
static void hideLibssl(const std::string &lib_path) {
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

// Helper Function to Unhide libssl
static void unhideLibssl(const std::string &lib_path) {
    auto maps = lsplt::MapInfo::Scan();
    for (const auto &region : maps) {
        if (region.path == lib_path) {
            LOGI("Unhiding libssl region: %zx-%zx", region.start, region.end);
            break; // Region already in maps; no explicit restoration needed
        }
    }
    applyMaps(maps);
}

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MapHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        uint32_t flags = api->getFlags();
        if ((flags & zygisk::PROCESS_ON_DENYLIST) && args->uid > 1000) {
            LOGI("App is on denylist, toggling visibility of libssl");
            toggleLibsslVisibility();
        }
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api;
    JNIEnv *env;

    void toggleLibsslVisibility() {
        const std::string libssl_path = "/apex/com.android.conscrypt/lib64/libssl.so";

        for (int i = 0; i < 5; ++i) { // Perform 5 hide-unhide cycles
            LOGI("Cycle %d: Hiding libssl", i + 1);
            hideLibssl(libssl_path);
            usleep(500 * 1000); // Hide for 500ms

            LOGI("Cycle %d: Unhiding libssl", i + 1);
            unhideLibssl(libssl_path);
            usleep(500 * 1000); // Unhide for 500ms
        }
    }
};

static void companion_handler(int client) {
    return;
}

REGISTER_ZYGISK_MODULE(MapHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
