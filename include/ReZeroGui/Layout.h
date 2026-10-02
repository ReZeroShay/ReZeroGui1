#pragma once
#include <ReZeroGui/ReZeroLib.h>

namespace ReZeroGui {

    struct Region {
        /// Region bounds.
        Vector2 min;
        Vector2 max;
        /// The current position where the next item is placed.
        Vector2 cursor;
        /// Maximum content position.
        Vector2 cursorMax;
        /// The X position where the current line starts.
        float lineStartX = 0.0f;
        /// The height of the tallest item on the current line.
        float lineHeight = 0.0f;
        /// A wrap was requested; the next placed item starts on a fresh line.
        bool pendingWrap = false;

        /// Resets the region to cover [minPoint, maxPoint] with a fresh cursor.
        void begin(Vector2 minPoint, Vector2 maxPoint) noexcept {
            min = minPoint;
            max = maxPoint;
            cursor = minPoint;
            cursorMax = minPoint;
            lineStartX = minPoint.x;
            lineHeight = 0.0f;
            pendingWrap = false;
        }

        /// Places one item and returns the rectangle used by the item.
        auto place(Vector2 size, Vector2 itemSpacing) noexcept -> Rect {
            if (pendingWrap) {
                wrap(itemSpacing);
            }
            const Rect rect(cursor, size);
            cursor.x += size.x + itemSpacing.x;
            lineHeight = std::max(lineHeight, size.y);
            cursorMax.x = std::max(cursorMax.x, rect.maxX());
            cursorMax.y = std::max(cursorMax.y, rect.maxY());
            // Every item ends its own line, so the next one lands below it.
            pendingWrap = true;
            return rect;
        }

        /// Ends the current line and moves the cursor to the next line.
        void wrap(Vector2 itemSpacing) noexcept {
            cursor.x = lineStartX;
            cursor.y += lineHeight + itemSpacing.y;
            lineHeight = 0.0f;
            pendingWrap = false;
        }

        /// Returns the width left from the current position to the region edge.
        float availableWidth() const noexcept {
            const float startX = pendingWrap ? lineStartX : cursor.x;
            return std::max(0.0f, max.x - startX);
        }

        /// Returns the size of all items placed in the region so far.
        auto contentSize() const noexcept -> Vector2 {
            return Vector2(std::max(0.0f, cursorMax.x - min.x), std::max(0.0f, cursorMax.y - min.y));
        }
    };
} // namespace ReZeroGui