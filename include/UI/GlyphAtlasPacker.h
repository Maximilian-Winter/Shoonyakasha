//
// GlyphAtlasPacker.h - Shelf packing of glyph bitmaps into an atlas
//
// Rectangles are placed left to right on horizontal shelves. A rectangle goes
// on the existing shelf that wastes the least height, or on a new shelf below
// the last one. Packed rectangles are never moved or freed individually;
// reset() empties the atlas.
//

#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace Shoonyakasha {
namespace UI {

class GlyphAtlasPacker {
public:
    /// `spacing` empty pixels are kept to the right of and below every
    /// rectangle, so bilinear sampling does not pick up a neighbour.
    GlyphAtlasPacker(int width, int height, int spacing = 1)
        : m_width(width), m_height(height), m_spacing(spacing) {}

    /// Top-left corner for a `width` x `height` rectangle, or nothing when the
    /// atlas has no room for it.
    std::optional<glm::ivec2> insert(int width, int height) {
        if (width <= 0 || height <= 0) return glm::ivec2(0);

        const int w = width + m_spacing;
        const int h = height + m_spacing;

        Shelf* best = nullptr;
        for (auto& shelf : m_shelves) {
            if (h > shelf.height || shelf.x + w > m_width) continue;
            if (!best || shelf.height < best->height) best = &shelf;
        }

        if (!best) {
            if (h > m_height - m_nextY || w > m_width) return std::nullopt;
            m_shelves.push_back({m_nextY, h, 0});
            m_nextY += h;
            best = &m_shelves.back();
        }

        const glm::ivec2 position(best->x, best->y);
        best->x += w;
        return position;
    }

    void reset() {
        m_shelves.clear();
        m_nextY = 0;
    }

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    struct Shelf {
        int y;
        int height;
        int x;  // next free column
    };

    int m_width;
    int m_height;
    int m_spacing;
    int m_nextY = 0;
    std::vector<Shelf> m_shelves;
};

} // namespace UI
} // namespace Shoonyakasha
