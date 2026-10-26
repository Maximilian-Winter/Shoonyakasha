#include "Planet/PlanetLod.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace Shoonyakasha::Planet {

namespace {

// math.pi
constexpr double PI = 3.141592653589793;

// (normal, u axis, v axis) per face: +X, -X, +Y, -Y, +Z, -Z (cube_sphere.FACES).
constexpr double FACES[6][3][3] = {
    {{1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}},
    {{-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}},
    {{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
    {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
    {{0.0, 0.0, -1.0}, {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
};

constexpr int TERRAIN_COLOUR_MODE = 0;
constexpr int FACE_COLOUR_MODE = 1;
constexpr int LEVEL_COLOUR_MODE = 2;

// The squares Python writes as `a ** 2` are pow(a, 2) in the C runtime;
// the products it writes as `a * b` are products. Both are kept as they are
// so that the results match to the bit.
double square(double a) { return std::pow(a, 2.0); }

// quadtree._distance
double distance(const Vec3& a, const Vec3& b) {
    return std::sqrt(square(a[0] - b[0]) + square(a[1] - b[1]) + square(a[2] - b[2]));
}

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// vec.length
double length(const Vec3& a) { return std::sqrt(dot(a, a)); }

double clampUnit(double x) { return std::max(-1.0, std::min(1.0, x)); }

// Python's max(0.0, x): 0.0 unless x is greater.
double atLeastZero(double x) { return x > 0.0 ? x : 0.0; }

Vec3 spherify(const Vec3& p) {
    const double x = p[0], y = p[1], z = p[2];
    const double x2 = x * x, y2 = y * y, z2 = z * z;
    return {x * std::sqrt(1.0 - 0.5 * y2 - 0.5 * z2 + y2 * z2 / 3.0),
            y * std::sqrt(1.0 - 0.5 * z2 - 0.5 * x2 + z2 * x2 / 3.0),
            z * std::sqrt(1.0 - 0.5 * x2 - 0.5 * y2 + x2 * y2 / 3.0)};
}

std::array<float, 4> levelColour(int level) {
    const double hue = level * 0.61803398875;
    const double offsets[3] = {0.0, 1.0 / 3.0, 2.0 / 3.0};
    std::array<float, 4> colour{};
    for (int k = 0; k < 3; ++k) {
        colour[k] = static_cast<float>(0.45 + 0.30 * std::cos(2.0 * PI * (hue - offsets[k])));
    }
    colour[3] = 1.0f;
    return colour;
}

} // namespace

Vec3 cubeSphereDirection(int face, double u, double v) {
    const auto& f = FACES[face];
    return spherify({f[0][0] + u * f[1][0] + v * f[2][0], f[0][1] + u * f[1][1] + v * f[2][1],
                     f[0][2] + u * f[1][2] + v * f[2][2]});
}

bool hiddenByHorizon(const Vec3& centre, double radius, const Vec3& camera, double occluder) {
    const double d2 = square(camera[0]) + square(camera[1]) + square(camera[2]);
    if (d2 <= occluder * occluder) return false;
    const double d = std::sqrt(d2);
    const Vec3 toCentre = sub(centre, camera);
    const double dist = distance(centre, camera);
    if (dist <= radius) return false;
    const double along = -(toCentre[0] * camera[0] + toCentre[1] * camera[1] + toCentre[2] * camera[2]) / d;
    if (along - radius < (d2 - occluder * occluder) / d) return false;
    const double cone = std::asin(occluder / d);
    const double angle = std::acos(clampUnit(along / dist));
    return angle + std::asin(std::min(1.0, radius / dist)) <= cone;
}

PlanetLod::PlanetLod(int slots, int maxJobs, int verticesPerChunk, double skirtFraction, int evictFrames,
                     double mergeRatio)
    : m_slots(slots), m_maxJobs(maxJobs), m_verticesPerChunk(verticesPerChunk), m_skirtFraction(skirtFraction),
      m_evictFrames(evictFrames), m_mergeRatio(mergeRatio) {
    if (slots <= 0 || maxJobs <= 0) throw std::invalid_argument("PlanetLod needs slots and jobs");
    m_free.reserve(slots);
    for (int slot = slots - 1; slot >= 0; --slot) m_free.push_back(slot);
    m_slotBody.assign(slots, -1);
    m_jobSlots.assign(slots, 0);
    m_pending.assign(slots, Pending{Ref{}, -1.0});
    m_instances.assign(static_cast<size_t>(slots) * 12, 0.0f);
    m_chunkDraws.assign(4, 0);
    m_oceanDraws.assign(4, 0);
    m_shadowDraws.assign(4, 0);
}

// ── Nodes ──────────────────────────────────────────────────────────────────

uint32_t PlanetLod::allocateBlock(int count) {
    (void)count;    // always four
    uint32_t first;
    if (!m_freeBlocks.empty()) {
        first = m_freeBlocks.back();
        m_freeBlocks.pop_back();
    } else {
        first = static_cast<uint32_t>(m_nodes.size());
        m_nodes.resize(m_nodes.size() + 4);
    }
    return first;
}

void PlanetLod::freeBlock(uint32_t first) {
    for (uint32_t k = 0; k < 4; ++k) {
        Node& node = m_nodes[first + k];
        node.alive = false;
        node.children = -1;
        node.slot = -1;
        node.splitLast = false;
        ++node.generation;
    }
    m_freeBlocks.push_back(first);
}

void PlanetLod::initNode(uint32_t index, int body, int face, int level, uint32_t x, uint32_t y, double radius,
                         double low, double high, double heightPad) {
    Node& node = m_nodes[index];
    const uint32_t generation = node.generation;
    node = Node{};
    node.generation = generation;
    node.alive = true;
    node.face = face;
    node.level = level;
    node.x = x;
    node.y = y;
    node.body = body;
    node.radius = radius;
    // cube_sphere.chunk_bounds and chunk_centre
    const double size = 2.0 / static_cast<double>(1u << level);
    const double u0 = -1.0 + x * size, v0 = -1.0 + y * size;
    const Vec3 c = cubeSphereDirection(face, u0 + 0.5 * size, v0 + 0.5 * size);
    node.centre = {c[0] * radius, c[1] * radius, c[2] * radius};
    double farthest = 0.0;
    const double steps[3] = {0.0, 0.5, 1.0};
    for (double su : steps) {
        for (double sv : steps) {
            const Vec3 d = cubeSphereDirection(face, u0 + su * size, v0 + sv * size);
            const double reach = distance(node.centre, {d[0] * radius, d[1] * radius, d[2] * radius});
            if (reach > farthest) farthest = reach;
        }
    }
    node.horizontalRadius = farthest;
    node.edge = radius * (PI / 2.0) / static_cast<double>(1u << level);
    node.heightPad = heightPad * node.edge;
    node.measured = false;
    setRange(node, low, high);
}

void PlanetLod::setRange(Node& node, double low, double high) {
    node.low = low;
    node.high = high;
    const double lo = low - node.heightPad, hi = high + node.heightPad;
    const double middle = node.radius + 0.5 * (lo + hi);
    for (int k = 0; k < 3; ++k) node.boundCentre[k] = node.centre[k] * middle / node.radius;
    node.boundRadius = node.horizontalRadius * (node.radius + hi) / node.radius + 0.5 * (hi - lo);
}

LodNodeInfo PlanetLod::info(const Node& node) const {
    LodNodeInfo out;
    out.face = node.face;
    out.level = node.level;
    out.x = node.x;
    out.y = node.y;
    out.slot = node.slot;
    out.centre = node.centre;
    out.edge = node.edge;
    out.low = node.low;
    out.high = node.high;
    out.measured = node.measured;
    out.boundCentre = node.boundCentre;
    out.boundRadius = node.boundRadius;
    return out;
}

PlanetLod::Tree* PlanetLod::tree(int body) {
    for (Tree& t : m_trees) {
        if (t.body == body) return &t;
    }
    return nullptr;
}

const PlanetLod::Tree* PlanetLod::tree(int body) const {
    for (const Tree& t : m_trees) {
        if (t.body == body) return &t;
    }
    return nullptr;
}

// ── Trees ──────────────────────────────────────────────────────────────────

void PlanetLod::createTree(int body, double radius, double low, double high, int maxLevel, double splitFactor,
                           double heightPad) {
    if (tree(body)) dropTree(body);
    Tree t;
    t.body = body;
    t.radius = radius;
    t.maxLevel = maxLevel;
    t.splitFactor = splitFactor;
    t.heightPad = heightPad;
    const uint32_t first = allocateBlock(4);
    const uint32_t second = allocateBlock(4);
    for (int face = 0; face < 6; ++face) {
        const uint32_t index = face < 4 ? first + face : second + (face - 4);
        t.roots[face] = index;
        initNode(index, body, face, 0, 0, 0, radius, low, high, heightPad);
    }
    for (uint32_t k = 2; k < 4; ++k) {   // the second block's last two stay unused
        m_nodes[second + k].alive = false;
    }
    m_trees.push_back(t);
    for (int face = 0; face < 6; ++face) {
        const uint32_t index = m_trees.back().roots[face];
        m_nodes[index].slot = make(index);
    }
}

void PlanetLod::dropTree(int body) {
    Tree* t = tree(body);
    if (!t) return;
    std::vector<uint32_t> order;
    collectNodes(*t, order);
    for (uint32_t index : order) {
        if (m_nodes[index].slot >= 0) {
            release(index);
            m_nodes[index].slot = -1;
        }
    }
    // Free the children's blocks, then the roots'.
    for (uint32_t index : order) {
        Node& node = m_nodes[index];
        if (node.children >= 0) {
            const uint32_t first = static_cast<uint32_t>(node.children);
            node.children = -1;
            freeBlock(first);
        }
    }
    const uint32_t first = t->roots[0], second = t->roots[4];
    freeBlock(first);
    freeBlock(second);
    m_trees.erase(m_trees.begin() + (t - m_trees.data()));
}

bool PlanetLod::hasTree(int body) const { return tree(body) != nullptr; }

double PlanetLod::splitFactor(int body) const {
    const Tree* t = tree(body);
    return t ? t->splitFactor : 0.0;
}

void PlanetLod::setSplitFactor(int body, double splitFactor) {
    if (Tree* t = tree(body)) t->splitFactor = splitFactor;
}

void PlanetLod::collectNodes(const Tree& t, std::vector<uint32_t>& out) const {
    std::vector<uint32_t> stack(t.roots.begin(), t.roots.end());
    while (!stack.empty()) {
        const uint32_t index = stack.back();
        stack.pop_back();
        out.push_back(index);
        const Node& node = m_nodes[index];
        if (node.children >= 0) {
            for (uint32_t k = 0; k < 4; ++k) stack.push_back(static_cast<uint32_t>(node.children) + k);
        }
    }
}

void PlanetLod::select(Tree& t, uint32_t index, const Vec3& camera, int64_t frame,
                       std::vector<std::pair<double, uint32_t>>& missing) {
    m_nodes[index].lastUsed = frame;
    const double dist = atLeastZero(distance(camera, m_nodes[index].boundCentre) - m_nodes[index].boundRadius);
    const double factor = t.splitFactor * (m_nodes[index].splitLast ? m_mergeRatio : 1.0);
    if (m_nodes[index].level < t.maxLevel && dist < factor * m_nodes[index].edge) {
        if (m_nodes[index].children < 0) {
            const uint32_t first = allocateBlock(4);     // may move m_nodes
            const Node parent = m_nodes[index];
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    initNode(first + dy * 2 + dx, parent.body, parent.face, parent.level + 1, 2 * parent.x + dx,
                             2 * parent.y + dy, t.radius, parent.low, parent.high, t.heightPad);
                }
            }
            m_nodes[index].children = static_cast<int32_t>(first);
        }
        const uint32_t first = static_cast<uint32_t>(m_nodes[index].children);
        bool ready = true;
        for (uint32_t k = 0; k < 4; ++k) ready = ready && m_nodes[first + k].slot >= 0;
        if (ready) {
            m_splitNow.push_back(index);
            for (uint32_t k = 0; k < 4; ++k) select(t, first + k, camera, frame, missing);
            return;
        }
        for (uint32_t k = 0; k < 4; ++k) {
            Node& child = m_nodes[first + k];
            child.lastUsed = frame;
            if (child.slot < 0) {
                missing.emplace_back(atLeastZero(distance(camera, child.boundCentre) - child.boundRadius), first + k);
            }
        }
    }
    t.selected.push_back(index);
}

void PlanetLod::build(Tree& t, const LodView& view, const Vec3& centre) {
    struct Item {
        bool outOfView;
        double distance;
        uint32_t index;
    };
    std::vector<Item> queue;
    queue.reserve(t.missing.size());
    for (const auto& [dist, index] : t.missing) {
        const Node& node = m_nodes[index];
        queue.push_back({!inView(view, add(centre, node.boundCentre), node.boundRadius), dist, index});
    }
    std::stable_sort(queue.begin(), queue.end(), [](const Item& a, const Item& b) {
        if (a.outOfView != b.outOfView) return !a.outOfView;
        return a.distance < b.distance;
    });
    int built = 0;
    for (const Item& item : queue) {
        if (built == m_maxJobs) break;
        if (m_nodes[item.index].slot < 0) {
            m_nodes[item.index].slot = make(item.index);
            ++built;
        }
    }
}

void PlanetLod::evict(Tree& t, uint32_t index, int64_t frame) {
    if (m_nodes[index].children < 0) return;
    const uint32_t first = static_cast<uint32_t>(m_nodes[index].children);
    bool unused = true;
    for (uint32_t k = 0; k < 4; ++k) unused = unused && frame - m_nodes[first + k].lastUsed > m_evictFrames;
    if (unused) {
        for (uint32_t k = 0; k < 4; ++k) releaseSubtree(first + k);
        m_nodes[index].children = -1;
        freeBlock(first);
        return;
    }
    for (uint32_t k = 0; k < 4; ++k) evict(t, first + k, frame);
}

void PlanetLod::releaseSubtree(uint32_t index) {
    if (m_nodes[index].children >= 0) {
        const uint32_t first = static_cast<uint32_t>(m_nodes[index].children);
        for (uint32_t k = 0; k < 4; ++k) releaseSubtree(first + k);
        m_nodes[index].children = -1;
        freeBlock(first);
    }
    if (m_nodes[index].slot >= 0) {
        release(index);
        m_nodes[index].slot = -1;
    }
}

// ── The pool ───────────────────────────────────────────────────────────────

int PlanetLod::make(uint32_t index) {
    if (m_free.empty() || static_cast<int>(m_jobs.size()) == m_maxJobs) return -1;
    const int slot = m_free.back();
    m_free.pop_back();
    m_slotBody[slot] = m_nodes[index].body;
    addJob(index, slot);
    return slot;
}

bool PlanetLod::rebuild(uint32_t index) {
    const int slot = m_nodes[index].slot;
    if (m_jobSlots[slot]) return true;
    if (static_cast<int>(m_jobs.size()) == m_maxJobs) return false;
    addJob(index, slot);
    return true;
}

void PlanetLod::addJob(uint32_t index, int slot) {
    const Node& node = m_nodes[index];
    ++m_stamp;
    LodJob job;
    job.a = {static_cast<double>(node.face), static_cast<double>(node.level), static_cast<double>(node.x),
             static_cast<double>(node.y)};
    job.b = {static_cast<double>(slot), m_skirtFraction * node.edge, static_cast<double>(m_stamp),
             static_cast<double>(m_slotBody[slot])};
    m_jobs.push_back(job);
    if (m_pending[slot].stamp < 0.0) ++m_pendingCount;
    m_pending[slot] = Pending{Ref{index, node.generation}, static_cast<double>(m_stamp)};
    m_jobSlots[slot] = 1;
}

void PlanetLod::release(uint32_t index) {
    const int slot = m_nodes[index].slot;
    if (m_pending[slot].stamp >= 0.0) {
        m_pending[slot].stamp = -1.0;
        --m_pendingCount;
    }
    m_slotBody[slot] = -1;
    m_free.push_back(slot);
}

void PlanetLod::clearJobs() {
    m_jobs.clear();
    std::fill(m_jobSlots.begin(), m_jobSlots.end(), 0);
}

void PlanetLod::takeHeights(const float* rows, size_t count) {
    const size_t limit = std::min(count, static_cast<size_t>(m_slots));
    for (size_t slot = 0; slot < limit; ++slot) {
        Pending& pending = m_pending[slot];
        if (pending.stamp < 0.0) continue;
        if (static_cast<double>(rows[slot * 4 + 2]) != pending.stamp) continue;
        Node& node = m_nodes[pending.node.index];
        if (node.alive && node.generation == pending.node.generation) {
            node.measured = true;
            setRange(node, static_cast<double>(rows[slot * 4]), static_cast<double>(rows[slot * 4 + 1]));
        }
        pending.stamp = -1.0;
        --m_pendingCount;
    }
}

// ── Per frame ──────────────────────────────────────────────────────────────

bool PlanetLod::inView(const LodView& view, const Vec3& centre, double radius) const {
    const Vec3 toCentre = sub(centre, view.position);
    const double dist = length(toCentre);
    if (dist <= radius) return true;
    const double halfDiagonal = std::atan(std::tan(view.fovDegrees * (PI / 180.0) * 0.5) *
                                          std::sqrt(1.0 + view.aspect * view.aspect));
    const double angle = std::acos(clampUnit(dot(toCentre, view.forward) / dist));
    return angle - std::asin(std::min(1.0, radius / dist)) < halfDiagonal;
}

void PlanetLod::update(const LodView& view, const std::vector<LodBody>& bodies, int64_t frame, bool frozen,
                       bool requeue) {
    bool anyTree = false;
    for (const LodBody& body : bodies) anyTree = anyTree || tree(body.index) != nullptr;
    if (!anyTree || frozen) return;
    for (const LodBody& body : bodies) {
        Tree* t = tree(body.index);
        if (!t) continue;
        const Vec3 camera = sub(view.position, body.centre);
        // QuadTree.update
        t->selected.clear();
        std::vector<std::pair<double, uint32_t>> missing;
        m_splitNow.clear();
        for (uint32_t root : t->roots) select(*t, root, camera, frame, missing);
        for (uint32_t index : t->splitLast) {
            if (m_nodes[index].alive) m_nodes[index].splitLast = false;
        }
        for (uint32_t index : m_splitNow) m_nodes[index].splitLast = true;
        t->splitLast = m_splitNow;
        std::stable_sort(missing.begin(), missing.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        t->missing = std::move(missing);
        build(*t, view, body.centre);
        for (uint32_t root : t->roots) evict(*t, root, frame);
    }
    if (requeue) {
        std::vector<std::pair<double, Ref>> distances;
        for (const LodBody& body : bodies) {
            const Tree* t = tree(body.index);
            if (!t) continue;
            const Vec3 eye = sub(view.position, body.centre);
            std::vector<uint32_t> order;
            collectNodes(*t, order);
            for (uint32_t index : order) {
                const Node& node = m_nodes[index];
                if (node.slot >= 0) distances.emplace_back(length(sub(node.boundCentre, eye)), Ref{index, node.generation});
            }
        }
        std::stable_sort(distances.begin(), distances.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        m_rebuild.clear();
        for (const auto& [dist, ref] : distances) m_rebuild.push_back(ref);
    }
    while (!m_rebuild.empty()) {
        const Ref ref = m_rebuild.back();
        const Node& node = m_nodes[ref.index];
        if (!node.alive || node.generation != ref.generation || node.slot < 0) {
            m_rebuild.pop_back();      // released since
            continue;
        }
        if (!rebuild(ref.index)) break;
        m_rebuild.pop_back();
    }
}

void PlanetLod::writeDraws(const LodView& view, const std::vector<LodBody>& bodies, int colourMode,
                           double shadowReach, uint32_t indexCount) {
    std::fill(m_instances.begin(), m_instances.end(), 0.0f);
    std::vector<int> drawn, casters, wet;
    uint32_t culled = 0;
    for (const LodBody& body : bodies) {
        const Tree* t = tree(body.index);
        if (!t) continue;
        const Vec3 eye = sub(view.position, body.centre);
        for (uint32_t index : t->selected) {
            const Node& node = m_nodes[index];
            const int slot = node.slot;
            if (slot < 0) continue;
            float* instance = &m_instances[static_cast<size_t>(slot) * 12];
            const Vec3 origin = sub(node.centre, eye);
            for (int k = 0; k < 3; ++k) instance[k] = static_cast<float>(origin[k]);
            std::array<float, 4> colour{-1.0f, 0.0f, 0.0f, 0.0f};
            if (colourMode == FACE_COLOUR_MODE) {
                colour = {m_faceColours[node.face][0], m_faceColours[node.face][1], m_faceColours[node.face][2], 1.0f};
            } else if (colourMode == LEVEL_COLOUR_MODE) {
                colour = levelColour(node.level);
            }
            for (int k = 0; k < 4; ++k) instance[4 + k] = colour[k];
            const double parentSplit = node.level > 0 ? t->splitFactor * 2.0 * node.edge : 0.0;
            instance[8] = static_cast<float>(parentSplit);
            instance[9] = static_cast<float>(node.level);
            instance[10] = static_cast<float>(body.index);
            if (length(sub(node.boundCentre, eye)) - node.boundRadius < shadowReach) casters.push_back(slot);
            const bool visible = inView(view, add(body.centre, node.boundCentre), node.boundRadius) &&
                                 !hiddenByHorizon(node.boundCentre, node.boundRadius, eye, body.occluder);
            if (!visible) {
                ++culled;
                continue;
            }
            drawn.push_back(slot);
            if (body.ocean && node.low < 0.0) wet.push_back(slot);
        }
    }
    m_culled = culled;
    const auto fill = [&](std::vector<uint32_t>& out, const std::vector<int>& slots) {
        out.assign(4 + slots.size() * 5, 0);
        out[0] = static_cast<uint32_t>(slots.size());
        for (size_t k = 0; k < slots.size(); ++k) {
            const uint32_t slot = static_cast<uint32_t>(slots[k]);
            uint32_t* command = &out[4 + k * 5];
            command[0] = indexCount;
            command[1] = 1;
            command[2] = 0;
            command[3] = slot * static_cast<uint32_t>(m_verticesPerChunk);
            command[4] = slot;
        }
    };
    fill(m_chunkDraws, drawn);
    fill(m_oceanDraws, wet);
    fill(m_shadowDraws, casters);
}

void PlanetLod::setFaceColours(const std::array<std::array<float, 3>, 6>& colours) { m_faceColours = colours; }

// ── Views ──────────────────────────────────────────────────────────────────

size_t PlanetLod::missing(int body) const {
    const Tree* t = tree(body);
    return t ? t->missing.size() : 0;
}

size_t PlanetLod::selectedCount(int body) const {
    const Tree* t = tree(body);
    return t ? t->selected.size() : 0;
}

std::vector<LodNodeInfo> PlanetLod::selected(int body) const {
    std::vector<LodNodeInfo> out;
    if (const Tree* t = tree(body)) {
        out.reserve(t->selected.size());
        for (uint32_t index : t->selected) out.push_back(info(m_nodes[index]));
    }
    return out;
}

std::vector<LodNodeInfo> PlanetLod::nodes(int body) const {
    std::vector<LodNodeInfo> out;
    if (const Tree* t = tree(body)) {
        std::vector<uint32_t> order;
        collectNodes(*t, order);
        out.reserve(order.size());
        for (uint32_t index : order) out.push_back(info(m_nodes[index]));
    }
    return out;
}

std::vector<std::pair<double, LodNodeInfo>> PlanetLod::missingNodes(int body) const {
    std::vector<std::pair<double, LodNodeInfo>> out;
    if (const Tree* t = tree(body)) {
        for (const auto& [dist, index] : t->missing) out.emplace_back(dist, info(m_nodes[index]));
    }
    return out;
}

} // namespace Shoonyakasha::Planet
