//
// CanvasLayout.h - Resolving UIRect placement for a canvas tree
//

#pragma once

#include "UI/UIComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

namespace Shoonyakasha {
namespace UI {

class FontLibrary;

/// The overlap of two rects; empty (max == min) when they do not overlap.
Rect intersect(const Rect& a, const Rect& b);

/// Where `element` lies inside `parent`.
Rect resolveRect(const Rect& parent, const UIRect& element);

/// Target pixels per canvas unit for a canvas drawn into `targetSize` pixels.
/// Returns 1 for a target with a zero or negative side.
float canvasScale(const UICanvas& canvas, const glm::vec2& targetSize);

/// Lays out one canvas: writes its targetSize, size and scale, and the
/// computed fields of every UIRect below it. Screen-overlay canvases are
/// sized from `screenSize`, world canvases from their pixelSize. Descendants
/// without UIRect, and nested canvases, are not entered. Text is measured for
/// layout groups and size fitters with `fonts`; without it, text measures
/// zero.
void layoutCanvas(entt::registry& registry, entt::entity canvas, const glm::vec2& screenSize,
                  const FontLibrary* fonts = nullptr);

/// One element's layout sizes along one axis.
struct LayoutSizes {
    float min = 0.0f;
    float preferred = 0.0f;
    float flexible = 0.0f;
};

/// Lengths for `children` sharing `available` along a layout group's axis:
/// - less than the sum of mins: each gets its min;
/// - between the sums of mins and preferreds: each moves from min towards
///   preferred by the same fraction;
/// - more: each gets its preferred plus a share of the rest by flexible
///   size, every child counting as at least 1 with `forceExpand`.
void distributeSizes(const std::vector<LayoutSizes>& children, float available, bool forceExpand,
                     std::vector<float>& lengths);

/// Columns of a grid layout for `count` children in `innerWidth`: its fixed
/// columns, or as many cells as fit, from 1 to `count`.
int gridColumns(const UIGridLayout& grid, float innerWidth, size_t count);

} // namespace UI
} // namespace Shoonyakasha
