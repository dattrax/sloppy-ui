#pragma once

#include "DirectInputPluginApi.h"

#include <string>

class DirectInputPluginLoader {
public:
    DirectInputPluginLoader() = default;
    ~DirectInputPluginLoader();

    DirectInputPluginLoader(const DirectInputPluginLoader&) = delete;
    DirectInputPluginLoader& operator=(const DirectInputPluginLoader&) = delete;

    bool init();
    void shutdown();
    bool pollEvent(SloppyInputEvent* eventOut) const;

    const std::string& error() const { return fError; }

private:
    bool resolveSymbols();
    bool createInstance();
    std::string resolveLibraryPath() const;

    void* fLibraryHandle = nullptr;
    SloppyInputInstance* fInstance = nullptr;
    SloppyInputCreateFn fCreateFn = nullptr;
    SloppyInputPollEventFn fPollEventFn = nullptr;
    SloppyInputDestroyFn fDestroyFn = nullptr;
    std::string fError;
};
