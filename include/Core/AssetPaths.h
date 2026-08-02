//
// AssetPaths.h - Finding the shared asset directory from wherever you started
//
// एकं मूलं बहवः पन्थानः — one root, many paths to it.
//
// Examples run from three different working directories depending on how they
// were launched: their own source directory (the CMake debugger setting), the
// build tree (running the .exe directly), or wherever a user happened to be.
// A relative path like "cubemaps_hdrs/sky.hdr" only works from one of those, so
// assets ended up copied into every example directory that needed them — eight
// copies of one 99 MB environment map — and two examples grew hand-written lists
// of fallback paths to try.
//
// This finds one shared asset root instead, from any of those starting points.
//

#pragma once

#include <filesystem>
#include <string>

namespace Shoonyakasha {

class AssetPaths {
public:
    /// The shared asset root, or an empty path if none was found.
    ///
    /// Searched once, in this order:
    ///   1. $SHOONYAKASHA_ASSET_DIR, if set and it exists
    ///   2. an `assets/` directory in the working directory or any ancestor
    ///   3. an `assets/` directory beside the executable or any ancestor
    ///
    /// A candidate only counts if it contains the marker file
    /// `.shoonyakasha-assets`, so an unrelated `assets/` belonging to some other
    /// project higher up the tree is not mistaken for ours.
    static const std::filesystem::path& root();

    /// Set the root explicitly, bypassing the search. Mainly for tests and for
    /// applications that ship assets somewhere of their own choosing.
    static void setRoot(const std::filesystem::path& path);

    /// Undo setRoot() and search again on the next call.
    static void resetForTesting();

    /// Resolve `relative` against the asset root. Returns an empty path if the
    /// root was not found, without checking whether the file exists.
    static std::filesystem::path resolve(const std::string& relative);

    /// Best guess at where `path` actually is — the forgiving entry point.
    ///
    /// Returns, in order of preference: `path` itself if it exists (so absolute
    /// paths and per-example layouts keep working untouched), then the asset-root
    /// resolution if that exists, and otherwise `path` unchanged — so a failure
    /// is reported against the path the caller actually asked for rather than
    /// against some rewritten one they never wrote.
    static std::filesystem::path locate(const std::string& path);

    /// Is there a file where locate() would look? For optional assets — the
    /// large downloads in tools/fetch_assets.py — so an example can fall back to
    /// something that ships with the repo instead of refusing to start.
    static bool exists(const std::string& path);

    /// Human-readable account of where the root came from, for startup logs and
    /// for the error message when an asset is missing.
    static std::string describe();

private:
    static std::filesystem::path search();
};

} // namespace Shoonyakasha
