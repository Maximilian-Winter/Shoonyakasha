//
// AssetPaths.h - Locate the shared asset directory
//
// Resolves asset paths against a single asset root, found by searching from the
// working directory and from the executable's location. This makes a relative
// path such as "env/sky.hdr" resolve identically whether a program is started
// from its source directory, from the build tree, or from anywhere else.
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
    /// A candidate is accepted only if it contains the marker file
    /// `.shoonyakasha-assets`, so an unrelated `assets/` directory further up
    /// the tree is not matched.
    static const std::filesystem::path& root();

    /// Set the root explicitly, bypassing the search. For tests, and for
    /// applications that ship assets elsewhere.
    static void setRoot(const std::filesystem::path& path);

    /// Undo setRoot() and search again on the next call.
    static void resetForTesting();

    /// Resolve `relative` against the asset root. Returns an empty path if the
    /// root was not found, without checking whether the file exists.
    static std::filesystem::path resolve(const std::string& relative);

    /// Resolve `path` to a file that exists, if one can be found.
    ///
    /// Returns, in order: `path` itself if it exists, which covers absolute
    /// paths and files beside the working directory; then `path` resolved
    /// against the asset root, if that exists; otherwise `path` unchanged, so
    /// an error names the path the caller passed in.
    static std::filesystem::path locate(const std::string& path);

    /// Whether locate() finds an existing file for `path`. Used to test for
    /// optional assets, such as the large downloads in tools/fetch_assets.py.
    static bool exists(const std::string& path);

    /// Description of where the root was found, for startup logs and error
    /// messages.
    static std::string describe();

private:
    static std::filesystem::path search();
};

} // namespace Shoonyakasha
