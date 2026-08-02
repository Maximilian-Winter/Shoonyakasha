//
// AssetPathsTest.cpp - resolving asset paths against a shared root
//
// Tier 1: filesystem only, no GPU.
//
// The behaviour that matters here is the forgiving part. locate() has to leave
// paths that already work untouched, or every example's existing layout breaks;
// and it has to report failures against the path the caller actually wrote,
// rather than against a rewritten one they would not recognise in a log.
//

#include <gtest/gtest.h>

#include "Core/AssetPaths.h"

#include <fstream>

using namespace Shoonyakasha;

namespace {

/// A throwaway directory tree, removed when the test finishes.
class TempTree {
public:
    TempTree() {
        static int counter = 0;
        m_root = std::filesystem::temp_directory_path()
               / ("shoonyakasha_assets_test_" + std::to_string(++counter));
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    const std::filesystem::path& root() const { return m_root; }

    std::filesystem::path touch(const std::string& relative) {
        auto full = m_root / relative;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream(full) << "x";
        return full;
    }

private:
    std::filesystem::path m_root;
};

/// Restores whatever the root was, so one test cannot leak into the next.
class AssetPathsTest : public ::testing::Test {
protected:
    void TearDown() override { AssetPaths::resetForTesting(); }
};

} // namespace

TEST_F(AssetPathsTest, ResolveJoinsAgainstTheRoot) {
    TempTree tree;
    AssetPaths::setRoot(tree.root());

    EXPECT_EQ(tree.root() / "env/sky.hdr", AssetPaths::resolve("env/sky.hdr"));
}

TEST_F(AssetPathsTest, ResolveIsEmptyWithoutARoot) {
    AssetPaths::setRoot({});
    EXPECT_TRUE(AssetPaths::resolve("env/sky.hdr").empty());
}

TEST_F(AssetPathsTest, LocateFindsAFileUnderTheRoot) {
    TempTree tree;
    auto expected = tree.touch("env/sky.hdr");
    AssetPaths::setRoot(tree.root());

    EXPECT_EQ(expected, AssetPaths::locate("env/sky.hdr"));
}

TEST_F(AssetPathsTest, LocateLeavesAWorkingPathAlone) {
    // An absolute path, or one that already resolves from the working directory,
    // must not be rewritten — that is what keeps existing layouts working.
    TempTree tree;
    auto direct = tree.touch("beside_me.hdr");
    AssetPaths::setRoot(tree.root());

    EXPECT_EQ(direct, AssetPaths::locate(direct.string()));
}

TEST_F(AssetPathsTest, LocatePrefersTheFileThatAlreadyResolves) {
    // Same relative name present both as given and under the asset root. The one
    // that already works wins, so pointing an app at a local override does not
    // silently pick up the shared copy instead.
    TempTree tree;
    tree.touch("shared/dupe.hdr");
    AssetPaths::setRoot(tree.root() / "shared");

    auto previous = std::filesystem::current_path();
    std::filesystem::current_path(tree.root());
    std::filesystem::create_directories(tree.root() / "local");
    std::ofstream(tree.root() / "local" / "dupe.hdr") << "x";

    EXPECT_EQ(std::filesystem::path("local/dupe.hdr"),
              AssetPaths::locate("local/dupe.hdr"));

    std::filesystem::current_path(previous);
}

TEST_F(AssetPathsTest, LocateReturnsTheOriginalWhenNothingIsFound) {
    // Not the resolved-but-missing path: an error message naming a path the
    // caller never wrote is worse than one naming the path they did.
    TempTree tree;
    AssetPaths::setRoot(tree.root());

    EXPECT_EQ(std::filesystem::path("env/absent.hdr"),
              AssetPaths::locate("env/absent.hdr"));
}

TEST_F(AssetPathsTest, LocateHandlesAnEmptyPath) {
    AssetPaths::setRoot({});
    EXPECT_TRUE(AssetPaths::locate("").empty());
}

TEST_F(AssetPathsTest, ExistsSeesWhatLocateWouldFind) {
    // The distinction locate() cannot make: it returns the original path when
    // nothing was found, which is indistinguishable from a path that resolved.
    // exists() is what an example asks before falling back to a smaller model.
    TempTree tree;
    tree.touch("models/Box.gltf");
    AssetPaths::setRoot(tree.root());

    EXPECT_TRUE(AssetPaths::exists("models/Box.gltf"));
    EXPECT_FALSE(AssetPaths::exists("models/NewSponza_Main_glTF_003.gltf"));
    EXPECT_FALSE(AssetPaths::exists(""));
}

TEST_F(AssetPathsTest, ExistsIsFalseWithoutARoot) {
    AssetPaths::setRoot({});
    EXPECT_FALSE(AssetPaths::exists("models/Box.gltf"));
}

TEST_F(AssetPathsTest, DescribeReportsAnExplicitRoot) {
    TempTree tree;
    AssetPaths::setRoot(tree.root());

    const auto description = AssetPaths::describe();
    EXPECT_NE(std::string::npos, description.find("set explicitly"));
    EXPECT_NE(std::string::npos, description.find(tree.root().string()));
}
