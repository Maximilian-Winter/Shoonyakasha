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

/// Directory containing the running executable, or empty if it cannot be
/// determined. Searched in addition to the working directory, which carries no
/// information about the project when an executable is run from the build tree.
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

/// Whether `candidate` contains the asset-root marker file.
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

    // MSVC deprecates getenv in favour of the non-portable _dupenv_s. The
    // returned pointer is copied into a path immediately and not retained, so a
    // later environment change cannot invalidate it.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    if (const char* fromEnv = std::getenv(kEnvVar)) {
        std::filesystem::path candidate(fromEnv);
        if (std::filesystem::is_directory(candidate, ec)) {
            g_origin = std::string(kEnvVar) + "=" + candidate.string();
            return candidate;
        }
        // Reported rather than ignored: a mistyped value would otherwise be
        // indistinguishable from the variable not being set.
        g_origin = std::string(kEnvVar) + " is set to '" + fromEnv
                 + "' but that is not a directory; fell back to searching";
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

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

bool AssetPaths::exists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(locate(path), ec);
}

std::string AssetPaths::describe() {
    root();  // ensure the search has run, so g_origin is meaningful
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_origin;
}

} // namespace Shoonyakasha
