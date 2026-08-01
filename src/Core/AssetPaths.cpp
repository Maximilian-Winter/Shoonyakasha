//
// AssetPaths.cpp
//

#include "Core/AssetPaths.h"

#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace Shoonyakasha {

namespace {

constexpr const char* kMarkerFile = ".shoonyakasha-assets";
constexpr const char* kEnvVar     = "SHOONYAKASHA_ASSET_DIR";

std::mutex            g_mutex;
bool                  g_searched = false;
std::filesystem::path g_root;
std::string           g_origin = "not searched yet";

/// Directory containing the running executable, or empty if it cannot be found.
///
/// Worth having in addition to the working directory: running the built .exe
/// straight out of the build tree is the one case where the working directory
/// tells you nothing about where the project is.
std::filesystem::path executableDirectory() {
    std::error_code ec;
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }
    std::filesystem::path exe(buffer, buffer + length);
#else
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    buffer[length] = '\0';
    std::filesystem::path exe(buffer);
#endif
    auto dir = exe.parent_path();
    return std::filesystem::exists(dir, ec) ? dir : std::filesystem::path{};
}

/// Is this an asset root rather than some unrelated directory called "assets"?
bool isAssetRoot(const std::filesystem::path& candidate) {
    std::error_code ec;
    return std::filesystem::is_directory(candidate, ec)
        && std::filesystem::exists(candidate / kMarkerFile, ec);
}

/// Walk from `start` up to the filesystem root looking for `assets/`.
std::filesystem::path searchUpward(std::filesystem::path start) {
    std::error_code ec;
    start = std::filesystem::absolute(start, ec);
    if (ec) {
        return {};
    }

    for (auto dir = start; !dir.empty(); dir = dir.parent_path()) {
        auto candidate = dir / "assets";
        if (isAssetRoot(candidate)) {
            return candidate;
        }
        if (dir == dir.parent_path()) {
            break;  // reached the root; parent_path() stops changing
        }
    }
    return {};
}

} // namespace

std::filesystem::path AssetPaths::search() {
    std::error_code ec;

    if (const char* fromEnv = std::getenv(kEnvVar)) {
        std::filesystem::path candidate(fromEnv);
        if (std::filesystem::is_directory(candidate, ec)) {
            g_origin = std::string(kEnvVar) + "=" + candidate.string();
            return candidate;
        }
        // Deliberately not silent: someone who set the variable meant it, and a
        // typo would otherwise look exactly like the variable not being set.
        g_origin = std::string(kEnvVar) + " is set to '" + fromEnv
                 + "' but that is not a directory; fell back to searching";
    }

    auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        if (auto found = searchUpward(cwd); !found.empty()) {
            g_origin = "found above the working directory: " + found.string();
            return found;
        }
    }

    if (auto exeDir = executableDirectory(); !exeDir.empty()) {
        if (auto found = searchUpward(exeDir); !found.empty()) {
            g_origin = "found above the executable: " + found.string();
            return found;
        }
    }

    g_origin = "no assets/ directory with a " + std::string(kMarkerFile)
             + " marker was found above the working directory or the executable";
    return {};
}

const std::filesystem::path& AssetPaths::root() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_searched) {
        g_root = search();
        g_searched = true;
    }
    return g_root;
}

void AssetPaths::setRoot(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_root = path;
    g_searched = true;
    g_origin = "set explicitly: " + path.string();
}

void AssetPaths::resetForTesting() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_root.clear();
    g_searched = false;
    g_origin = "not searched yet";
}

std::filesystem::path AssetPaths::resolve(const std::string& relative) {
    const auto& base = root();
    if (base.empty()) {
        return {};
    }
    return base / relative;
}

std::filesystem::path AssetPaths::locate(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path asGiven(path);
    if (std::filesystem::exists(asGiven, ec)) {
        return asGiven;
    }

    auto resolved = resolve(path);
    if (!resolved.empty() && std::filesystem::exists(resolved, ec)) {
        return resolved;
    }

    return asGiven;
}

std::string AssetPaths::describe() {
    root();  // ensure the search has run, so g_origin is meaningful
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_origin;
}

} // namespace Shoonyakasha
