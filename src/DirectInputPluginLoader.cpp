#include "DirectInputPluginLoader.hpp"

#include <dlfcn.h>
#include <cstdlib>

namespace {

static constexpr const char* kEnvPluginPath = "SLOPPY_UI_INPUT_PLUGIN_PATH";
static constexpr const char* kDefaultPluginName = "libsloppy_input.so";

}  // namespace

DirectInputPluginLoader::~DirectInputPluginLoader() {
    this->shutdown();
}

bool DirectInputPluginLoader::init() {
    this->shutdown();

    std::string libraryPath = this->resolveLibraryPath();
    fLibraryHandle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!fLibraryHandle) {
        fError = std::string("Failed to load input plugin '") + libraryPath + "': " + dlerror();
        return false;
    }

    if (!this->resolveSymbols()) {
        this->shutdown();
        return false;
    }
    if (!this->createInstance()) {
        this->shutdown();
        return false;
    }
    return true;
}

void DirectInputPluginLoader::shutdown() {
    if (fInstance && fDestroyFn) {
        fDestroyFn(fInstance);
    }
    fInstance = nullptr;
    fCreateFn = nullptr;
    fPollEventFn = nullptr;
    fDestroyFn = nullptr;

    if (fLibraryHandle) {
        dlclose(fLibraryHandle);
        fLibraryHandle = nullptr;
    }
}

bool DirectInputPluginLoader::pollEvent(SloppyInputEvent* eventOut) const {
    if (!fInstance || !fPollEventFn || !eventOut) {
        return false;
    }
    return fPollEventFn(fInstance, eventOut) != 0;
}

bool DirectInputPluginLoader::resolveSymbols() {
    dlerror();
    fCreateFn = reinterpret_cast<SloppyInputCreateFn>(dlsym(fLibraryHandle, "sloppy_input_create"));
    const char* symbolErr = dlerror();
    if (symbolErr) {
        fError = std::string("Missing symbol sloppy_input_create: ") + symbolErr;
        return false;
    }

    dlerror();
    fPollEventFn = reinterpret_cast<SloppyInputPollEventFn>(
        dlsym(fLibraryHandle, "sloppy_input_poll_event"));
    symbolErr = dlerror();
    if (symbolErr) {
        fError = std::string("Missing symbol sloppy_input_poll_event: ") + symbolErr;
        return false;
    }

    dlerror();
    fDestroyFn = reinterpret_cast<SloppyInputDestroyFn>(dlsym(fLibraryHandle, "sloppy_input_destroy"));
    symbolErr = dlerror();
    if (symbolErr) {
        fError = std::string("Missing symbol sloppy_input_destroy: ") + symbolErr;
        return false;
    }

    return true;
}

bool DirectInputPluginLoader::createInstance() {
    SloppyInputConfig config = {};
    config.structSize = sizeof(SloppyInputConfig);
    fInstance = fCreateFn(&config);
    if (!fInstance) {
        fError = "Input plugin returned null from sloppy_input_create.";
        return false;
    }
    return true;
}

std::string DirectInputPluginLoader::resolveLibraryPath() const {
    const char* path = std::getenv(kEnvPluginPath);
    if (path && path[0] != '\0') {
        return std::string(path);
    }
    return std::string(kDefaultPluginName);
}
