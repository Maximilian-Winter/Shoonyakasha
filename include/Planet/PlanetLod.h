#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Shoonyakasha::Planet {

using Vec3 = std::array<double, 3>;

/// A body's state for one update: its index (the trees are kept by it), its
/// centre from the planet's centre, the radius of the sphere its terrain
/// stays above (for horizon culling), and whether the ocean is drawn on it.
struct LodBody {
    int index = 0;
    Vec3 centre{0.0, 0.0, 0.0};
    double occluder = 0.0;
    bool ocean = false;
};

/// The camera for one update: its position (planet space, metres), its unit
/// forward direction, its vertical field of view in degrees and the
/// screen's aspect (width over height).
struct LodView {
    Vec3 position{0.0, 0.0, 0.0};
    Vec3 forward{0.0, 0.0, -1.0};
    double fovDegrees = 70.0;
    double aspect = 16.0 / 9.0;
};

/// A copy of a node's state.
struct LodNodeInfo {
    int face = 0;
    int level = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    int slot = -1;             ///< its chunk's slot in the pool, or -1 while it has none
    Vec3 centre{0.0, 0.0, 0.0};
    double edge = 0.0;
    double low = 0.0;
    double high = 0.0;
    bool measured = false;
    Vec3 boundCentre{0.0, 0.0, 0.0};
    double boundRadius = 0.0;
};

/// A chunk to build this frame: (face, level, x, y) and (slot, skirt depth,
/// stamp, body), as chunk_build.comp reads them.
struct LodJob {
    std::array<double, 4> a{};
    std::array<double, 4> b{};
};

/// The level of detail of bodies drawn as cube-spheres of chunks from one
/// pool of GPU slots, as shoonyakasha.planet's Python quadtree, chunk pool
/// and draw lists do it (quadtree.py, world.py):
///  - per body a quadtree over the six cube faces; a node splits into its
///    four children within splitFactor times its edge length of its
///    bounding sphere and merges again beyond mergeRatio times that; it is
///    drawn until all four children have a chunk;
///  - the missing chunks are built, up to the jobs left this frame, those in
///    view first, nearest first; subtrees unused for evictFrames frames are
///    released, children before their parent;
///  - the chunks' slots, the jobs that build them (each with a stamp that
///    the heights read back carry) and the chunks waiting for their heights;
///  - the chunks to build again (for a new sun), nearest first, in the jobs
///    new chunks leave over, a slot that has a job this frame getting none;
///  - the draw lists: every selected chunk in view and not behind its
///    body's horizon, those of them reaching below sea level on a body with
///    an ocean, and every selected chunk within the shadows' reach, with
///    each slot's centre relative to the camera, colour and morph values.
class PlanetLod {
public:
    PlanetLod(int slots, int maxJobs, int verticesPerChunk, double skirtFraction, int evictFrames = 120,
              double mergeRatio = 1.25);

    /// A tree for `body` with its six roots, each given a slot and a job.
    void createTree(int body, double radius, double low, double high, int maxLevel, double splitFactor,
                    double heightPad);
    /// The body's tree gone, its chunks released in the order of its nodes.
    void dropTree(int body);
    bool hasTree(int body) const;
    double splitFactor(int body) const;
    void setSplitFactor(int body, double splitFactor);

    /// Unless `frozen`: each body's tree updated for the camera, its missing
    /// chunks built and its unused ones released; with `requeue` every
    /// built chunk is queued to be built again; then the queue is worked
    /// through with the jobs left.
    void update(const LodView& view, const std::vector<LodBody>& bodies, int64_t frame, bool frozen,
                bool requeue);
    /// Heights read back: `count` rows of (lowest, highest, stamp, unused),
    /// one per slot. Each chunk waiting for its job's stamp takes its range.
    void takeHeights(const float* rows, size_t count);
    /// The draw lists and the instances for the view.
    void writeDraws(const LodView& view, const std::vector<LodBody>& bodies, int colourMode, double shadowReach,
                    uint32_t indexCount);

    /// The colours of the cube's faces for colour mode 1 (0 is the terrain's
    /// own colours, 2 one colour per level).
    void setFaceColours(const std::array<std::array<float, 3>, 6>& colours);

    const std::vector<LodJob>& jobs() const { return m_jobs; }
    /// This frame's jobs handed over: the list starts again.
    void clearJobs();

    /// Per slot three vec4: centre less the camera (xyz), colour (rgba), and
    /// the parent's split distance, the level and the body (xyz).
    const std::vector<float>& instances() const { return m_instances; }
    /// A count, padding to 16 bytes, then five uint32 per draw.
    const std::vector<uint32_t>& chunkDraws() const { return m_chunkDraws; }
    const std::vector<uint32_t>& oceanDraws() const { return m_oceanDraws; }
    const std::vector<uint32_t>& shadowDraws() const { return m_shadowDraws; }
    uint32_t culled() const { return m_culled; }

    int slots() const { return m_slots; }
    int used() const { return m_slots - static_cast<int>(m_free.size()); }
    size_t pending() const { return m_pendingCount; }
    size_t rebuildQueued() const { return m_rebuild.size(); }
    size_t missing(int body) const;
    size_t selectedCount(int body) const;
    std::vector<LodNodeInfo> selected(int body) const;
    /// Every node of the body's tree, in the order of a stack seeded with the roots.
    std::vector<LodNodeInfo> nodes(int body) const;
    /// The missing chunks of the body's tree, nearest first, with their distances.
    std::vector<std::pair<double, LodNodeInfo>> missingNodes(int body) const;

private:
    struct Ref {
        uint32_t index = 0;
        uint32_t generation = 0;
    };
    struct Node {
        int face = 0;
        int level = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        int body = 0;
        double radius = 0.0;
        Vec3 centre{};
        double horizontalRadius = 0.0;
        double edge = 0.0;
        double heightPad = 0.0;
        double low = 0.0;
        double high = 0.0;
        bool measured = false;
        Vec3 boundCentre{};
        double boundRadius = 0.0;
        int32_t children = -1;   ///< index of the first of four, or -1
        int slot = -1;
        int64_t lastUsed = 0;
        bool splitLast = false;  ///< drawn through its children in the last update
        uint32_t generation = 0;
        bool alive = false;
    };
    struct Tree {
        int body = 0;
        double radius = 0.0;
        int maxLevel = 12;
        double splitFactor = 1.25;
        double heightPad = 0.0;
        std::array<uint32_t, 6> roots{};
        std::vector<uint32_t> selected;
        std::vector<std::pair<double, uint32_t>> missing;
        std::vector<uint32_t> splitLast;   ///< the nodes drawn through their children in the last update
    };
    struct Pending {
        Ref node;
        double stamp = 0.0;
    };

    uint32_t allocateBlock(int count);
    void freeBlock(uint32_t first);
    void initNode(uint32_t index, int body, int face, int level, uint32_t x, uint32_t y, double radius,
                  double low, double high, double heightPad);
    void setRange(Node& node, double low, double high);
    LodNodeInfo info(const Node& node) const;
    Tree* tree(int body);
    const Tree* tree(int body) const;

    void select(Tree& tree, uint32_t index, const Vec3& camera, int64_t frame,
                std::vector<std::pair<double, uint32_t>>& missing);
    void build(Tree& tree, const LodView& view, const Vec3& centre);
    void evict(Tree& tree, uint32_t index, int64_t frame);
    void releaseSubtree(uint32_t index);
    void collectNodes(const Tree& tree, std::vector<uint32_t>& out) const;

    int make(uint32_t index);
    bool rebuild(uint32_t index);
    void addJob(uint32_t index, int slot);
    void release(uint32_t index);

    bool inView(const LodView& view, const Vec3& centre, double radius) const;

    int m_slots;
    int m_maxJobs;
    int m_verticesPerChunk;
    double m_skirtFraction;
    int m_evictFrames;
    double m_mergeRatio;

    std::vector<Node> m_nodes;
    std::vector<uint32_t> m_freeBlocks;   ///< first indices of free blocks of four
    std::vector<Tree> m_trees;
    std::vector<uint32_t> m_splitNow;     ///< the tree being updated: the nodes drawn through their children
    std::array<std::array<float, 3>, 6> m_faceColours{};

    std::vector<int> m_free;              ///< free slots, taken from the back
    std::vector<int> m_slotBody;          ///< per slot the body of its chunk, or -1
    std::vector<LodJob> m_jobs;
    std::vector<char> m_jobSlots;         ///< per slot whether this frame's jobs build it
    std::vector<Pending> m_pending;       ///< per slot the chunk waiting for its heights; stamp < 0 for none
    size_t m_pendingCount = 0;
    int64_t m_stamp = 0;
    std::vector<Ref> m_rebuild;           ///< chunks to build again, nearest last

    std::vector<float> m_instances;
    std::vector<uint32_t> m_chunkDraws;
    std::vector<uint32_t> m_oceanDraws;
    std::vector<uint32_t> m_shadowDraws;
    uint32_t m_culled = 0;
};

/// The cube-sphere's geometry, as shoonyakasha.planet.cube_sphere computes it.
Vec3 cubeSphereDirection(int face, double u, double v);
/// Whether a sphere lies wholly behind a sphere of radius `occluder` around
/// the origin, seen from `camera` (quadtree.hidden_by_horizon).
bool hiddenByHorizon(const Vec3& centre, double radius, const Vec3& camera, double occluder);

} // namespace Shoonyakasha::Planet
