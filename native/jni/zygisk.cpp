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
    zygisk::Api *api;
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
        for (const auto &region : maps) {
            if (region.path == lib_path && region.perms.find("x") != std::string::npos) {
                LOGI("Hiding libssl region: %lx-%lx", region.start, region.end);
                void *addr = reinterpret_cast<void *>(region.start);
                size_t size = region.end - region.start;
                if (mprotect(addr, size, PROT_NONE) != 0) {
                    LOGE("Failed to hide region: %lx-%lx", region.start, region.end);
                }
            }
        }
    }

    void unhideLibssl(const std::string &lib_path) {
        auto maps = lsplt::MapInfo::Scan();
        for (const auto &region : maps) {
            if (region.path == lib_path && region.perms.find("x") != std::string::npos) {
                LOGI("Restoring libssl region: %lx-%lx", region.start, region.end);
                void *addr = reinterpret_cast<void *>(region.start);
                size_t size = region.end - region.start;
                if (mprotect(addr, size, PROT_READ | PROT_EXEC) != 0) {
                    LOGE("Failed to restore region: %lx-%lx", region.start, region.end);
                }
            }
        }
    }
};

static void companion_handler(int client) {
    return;
}

REGISTER_ZYGISK_MODULE(MapHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
