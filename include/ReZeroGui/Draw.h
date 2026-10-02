#pragma once
#include <cstdint>
#include <span>
#include <ReZeroGui/Allocator.h>
#include <ReZeroGui/ReZeroLib.h>
#include <cmath>
#include <limits>
#include <vector>
namespace ReZeroGui {
    using DrawIndex = std::uint32_t;
    using TextureId = std::uintptr_t;

    constexpr TextureId InvalidTexture = std::numeric_limits<TextureId>::max();

    /// non-owning
    struct TextureDescription {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    enum class ResourceCmdType : uint8_t { CreateTexture, UpdateTexture, DestroyTexture };

    /// A texture lifetime request travelling from the UI layer to the backend.
    ///
    /// Handles are handed out by the UI context (never by the backend and never
    /// reused), so the backend only has to map a handle to a device object. The
    /// pixel data is borrowed: it has to stay alive until the renderer has
    /// processed the command.
    struct ResourceCmd {
        ResourceCmdType type = ResourceCmdType::CreateTexture;
        TextureId handle = InvalidTexture;
        TextureDescription desc;
        const std::uint8_t *pixels = nullptr; // RGBA8
    };
    enum class DrawCorner : std::uint8_t {
        None = 0,
        TopLeft = 1,
        TopRight = 2,
        BottomLeft = 4,
        BottomRight = 8,
        Top = TopLeft | TopRight,
        Bottom = BottomLeft | BottomRight,
        Left = TopLeft | BottomLeft,
        Right = TopRight | BottomRight,
        All = TopLeft | TopRight | BottomLeft | BottomRight,
    };
    constexpr DrawCorner operator|(DrawCorner lhs, DrawCorner rhs) noexcept {
        return static_cast<DrawCorner>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
    }

    constexpr DrawCorner operator&(DrawCorner lhs, DrawCorner rhs) noexcept {
        return static_cast<DrawCorner>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
    }

    constexpr bool hasCorner(DrawCorner corners, DrawCorner corner) noexcept {
        return (static_cast<std::uint8_t>(corners) & static_cast<std::uint8_t>(corner)) != 0;
    }

    struct DrawVertex {
        Vector2 position;
        Vector2 uv;
        Pixel32 pixel{0};
    };

    /// A single indexed draw call with its own scissor rectangle and texture.
    struct DrawCommand {
        Rect clipRect;
        std::uint32_t indexOffset{0};
        std::uint32_t indexCount{0};
        TextureId textureId = InvalidTexture;
    };

    struct DrawList {
        explicit DrawList(AllocatorView allocator) noexcept
            : vertices(allocator), indices(allocator), commands(allocator), clipStack(allocator) {}

        DrawList(const DrawList &) = delete;
        DrawList &operator=(const DrawList &) = delete;

        void onFrameBegin() noexcept {
            vertices.clear();
            indices.clear();
            commands.clear();
            clipStack.clear();
        }

        /// Called at the end of a frame to remember how much geometry we produced.
        void onFrameEnd() noexcept {}

        void setDisplayRect(Rect rect) noexcept { displayRect = rect; }

        // -- clipping ----------------------------------------------------------

        auto pushClipRect(Rect rect) noexcept {
            if (clipStack.empty()) {
                clipStack.push_back(rect.intersected(displayRect));
            } else {
                clipStack.push_back(rect.intersected(clipStack.back()));
            }
        }

        auto popClipRect() noexcept {
            if (!clipStack.empty()) {
                clipStack.pop_back();
            }
        }

        auto currentClipRect() const noexcept -> Rect { return clipStack.empty() ? displayRect : clipStack.back(); }

        auto clipDepth() const noexcept -> std::size_t { return clipStack.size(); }

        // -- accessors ---------------------------------------------------------

        std::span<const DrawVertex> currentVertices() const noexcept { return vertices; }
        std::span<const DrawIndex> currentIndices() const noexcept { return indices; }
        std::span<const DrawCommand> currentCommands() const noexcept { return commands; }
        bool isEmpty() const noexcept { return indices.empty(); }
        std::uint32_t vertexCount() const noexcept { return static_cast<std::uint32_t>(vertices.size()); }
        std::uint32_t indexCount() const noexcept { return static_cast<std::uint32_t>(indices.size()); }

        // -- primitives --------------------------------------------------------

        auto addLine(Vector2 first, Vector2 second, Pixel color, float thickness = 1.0f) noexcept {
            if (color.a <= 0.0f) {
                return;
            }
            const Vector2 delta = second - first;
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (length <= 0.0f) {
                return;
            }
            // Lines rasterize as quads centered on the segment, clamped to at least
            // one pixel: a zero width "line" degenerates into a zero area triangle
            // that covers no pixel centers and would be invisible.
            const Vector2 unit = delta / length;
            const Vector2 normal = Vector2(-unit.y, unit.x) * (std::max(thickness, 1.0f) * 0.5f);
            addQuadFilled(first + normal, second + normal, second - normal, first - normal, color);
        }

        /// Rectangle outline, anti aliased along its whole path: the corners and the
        /// straight edges fade over a pixel on both sides of the border.
        auto addRect(Vector2 minimum, Vector2 maximum, Pixel color, float rounding = 0.0f,
                     DrawCorner corners = DrawCorner::All, float thickness = 1.0f) noexcept {
            if (color.a <= 0.0f || thickness <= 0.0f) {
                return;
            }
            addRoundedRectOutline(minimum, maximum, color, rounding, corners, thickness);
        }

        /// Rectangle fill, feathered along its whole outline. A square rectangle is
        /// feathered too: a widget sitting on a fractional coordinate would otherwise
        /// still show a staircase along its sides.
        void addRectFilled(Vector2 minimum, Vector2 maximum, Pixel color, float rounding = 0.0f,
                           DrawCorner corners = DrawCorner::All) noexcept {
            if (color.a <= 0.0f) {
                return;
            }
            addRoundedRectFilled(minimum, maximum, color, rounding, corners);
        }

        void addQuadFilled(Vector2 first, Vector2 second, Vector2 third, Vector2 fourth, Pixel color) noexcept {
            if (color.a <= 0.0f) {
                return;
            }
            beginPrimitive(InvalidTexture);
            const std::uint32_t base = vertexCount();
            const Vector2 uv(0.0f, 0.0f);
            addVertex(first, uv, color);
            addVertex(second, uv, color);
            addVertex(third, uv, color);
            addVertex(fourth, uv, color);
            addQuadIndices(base);
        }

        void addTriangleFilled(Vector2 first, Vector2 second, Vector2 third, Pixel color) noexcept {
            if (color.a <= 0.0f) {
                return;
            }
            beginPrimitive(InvalidTexture);
            const std::uint32_t base = vertexCount();
            const Vector2 uv(0.0f, 0.0f);
            addVertex(first, uv, color);
            addVertex(second, uv, color);
            addVertex(third, uv, color);
            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            commands.back().indexCount += 3;
        }

        void addTexturedQuad(Vector2 minimum, Vector2 maximum, Vector2 uvMinimum, Vector2 uvMaximum, Pixel color,
                             TextureId texture) noexcept {
            if (color.a <= 0.0f) {
                return;
            }
            beginPrimitive(texture);
            const std::uint32_t base = vertexCount();
            addVertex(minimum, uvMinimum, color);
            addVertex(Vector2(maximum.x, minimum.y), Vector2(uvMaximum.x, uvMinimum.y), color);
            addVertex(maximum, uvMaximum, color);
            addVertex(Vector2(minimum.x, maximum.y), Vector2(uvMinimum.x, uvMaximum.y), color);
            addQuadIndices(base);
        }

        /// Width of a circle's anti-aliased edge, in pixels. The coverage rides in the
        /// vertex alpha, which the blend state already honours, so this needs neither
        /// multisampling nor a post pass.
        static constexpr float FeatherWidth = 1.0f;

        void addCircle(Vector2 center, float radius, Pixel color, int segmentCount = 0,
                       float thickness = 1.0f) noexcept {
            if (color.a <= 0.0f || radius <= 0.0f || thickness <= 0.0f) {
                return;
            }
            const int segments = segmentCount > 0 ? segmentCount : circleSegmentCount(radius);
            beginPrimitive(InvalidTexture);

            // Four rings - transparent, solid, solid, transparent - clamped so that a
            // thin or a thick stroke can never fold them over each other. Without the
            // clamp a hairline ring would fade twice over the same pixels and look
            // washed out.
            const float half = thickness * 0.5f;
            const float halfFeather = FeatherWidth * 0.5f;
            const float innerFade = std::max(0.0f, radius - half - halfFeather);
            const float innerSolid = std::max(innerFade, radius - half + halfFeather);
            const float outerSolid = std::max(innerSolid, radius + half - halfFeather);
            const float outerFade = std::max(outerSolid, radius + half + halfFeather);

            addFeatheredRing(center, innerFade, 0.0f, innerSolid, color.a, segments, color);
            addFeatheredRing(center, innerSolid, color.a, outerSolid, color.a, segments, color);
            addFeatheredRing(center, outerSolid, color.a, outerFade, 0.0f, segments, color);
        }

        void addCircleFilled(Vector2 center, float radius, Pixel color, int segmentCount = 0) noexcept {
            if (color.a <= 0.0f || radius <= 0.0f) {
                return;
            }
            const int segments = segmentCount > 0 ? segmentCount : circleSegmentCount(radius);
            beginPrimitive(InvalidTexture);

            // Solid core up to the feather, then the fading rim. A circle smaller than
            // the feather is all rim, which is what a sub pixel dot should look like.
            const float solidRadius = radius - FeatherWidth * 0.5f;
            const float fadeRadius = radius + FeatherWidth * 0.5f;
            if (solidRadius > 0.0f) {
                addFeatheredRing(center, 0.0f, color.a, solidRadius, color.a, segments, color);
                addFeatheredRing(center, solidRadius, color.a, fadeRadius, 0.0f, segments, color);
            } else {
                addFeatheredRing(center, 0.0f, color.a, fadeRadius, 0.0f, segments, color);
            }
        }

      private:
        /// Emits `segments` quads between two concentric rings, ramping the alpha from
        /// `innerAlpha` to `outerAlpha`. Both rings share their angles so the strip is
        /// watertight, and an inner radius of zero collapses it into a fan.
        void addFeatheredRing(Vector2 center, float innerRadius, float innerAlpha, float outerRadius, float outerAlpha,
                              int segments, Pixel color) {
            constexpr float TwoPi = 6.28318530718f;
            const Vector2 uv(0.0f, 0.0f);
            const std::uint32_t base = vertexCount();
            for (int index = 0; index < segments; ++index) {
                const float angle = (static_cast<float>(index) / static_cast<float>(segments)) * TwoPi;
                const Vector2 direction(std::cos(angle), std::sin(angle));
                addVertex(center + direction * innerRadius, uv, color.withAlpha(innerAlpha));
                addVertex(center + direction * outerRadius, uv, color.withAlpha(outerAlpha));
            }
            for (int index = 0; index < segments; ++index) {
                const std::uint32_t current = base + static_cast<std::uint32_t>(index) * 2;
                const std::uint32_t next = base + static_cast<std::uint32_t>((index + 1) % segments) * 2;
                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(next + 1);
                indices.push_back(current);
                indices.push_back(next + 1);
                indices.push_back(current + 1);
            }
            commands.back().indexCount += static_cast<std::uint32_t>(segments) * 6;
        }

        void addVertex(Vector2 position, Vector2 uv, Pixel pixel) {
            vertices.push_back(DrawVertex{position, uv, pixel.toPixel32()});
        }

        void addQuadIndices(std::uint32_t base) {
            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            commands.back().indexCount += 6;
        }

        /// Ensures a trailing draw command matching the current texture and clip
        /// rectangle exists.
        void beginPrimitive(TextureId texture) {
            const Rect clip = currentClipRect();
            if (!commands.empty()) {
                DrawCommand &command = commands.back();
                if (command.indexCount == 0) {
                    command.textureId = texture;
                    command.clipRect = clip;
                    command.indexOffset = static_cast<std::uint32_t>(indices.size());
                    return;
                }
                if (command.textureId == texture && command.clipRect.x == clip.x && command.clipRect.y == clip.y &&
                    command.clipRect.width == clip.width && command.clipRect.height == clip.height) {
                    return;
                }
            }
            DrawCommand command;
            command.textureId = texture;
            command.clipRect = clip;
            command.indexOffset = static_cast<std::uint32_t>(indices.size());
            commands.push_back(command);
        }

        static int circleSegmentCount(float radius) noexcept {
            const int count = static_cast<int>(radius * 0.6f) + 8;
            return count > 64 ? 64 : count;
        }

        /// Segments per 90 degree corner: enough to keep the deviation from the true arc
        /// under about a fifth of a pixel, capped so a huge rounding cannot explode the
        /// vertex count.
        static int roundedRectSegments(float radius) noexcept {
            if (radius <= 0.0f) {
                return 2;
            }
            const float ratio = 1.0f - 0.2f / radius;
            if (ratio <= -1.0f) {
                return 2;
            }
            const int count = static_cast<int>(1.57079632679f / std::acos(ratio)) + 1;
            return count < 2 ? 2 : (count > 32 ? 32 : count);
        }

        static float clampRoundedRectRadius(Vector2 minimum, Vector2 maximum, float rounding) noexcept {
            float radius = rounding < 0.0f ? 0.0f : rounding;
            const float halfWidth = (maximum.x - minimum.x) * 0.5f;
            const float halfHeight = (maximum.y - minimum.y) * 0.5f;
            if (radius > halfWidth) {
                radius = halfWidth;
            }
            if (radius > halfHeight) {
                radius = halfHeight;
            }
            return radius < 0.0f ? 0.0f : radius;
        }

        /// One point of a rounded rectangle outline, plus the direction that points out
        /// of the shape there.
        struct PathPoint {
            Vector2 position;
            Vector2 normal;
        };

        /// Walks the outline clockwise, handing `emit` every point and its outward
        /// normal. The straight edges are the gaps between consecutive arcs, and the two
        /// ends of a gap share a normal, so they need no special case. A corner that is
        /// not rounded emits its sharp point twice, once per adjacent edge normal; the
        /// gap between those two becomes the miter triangle of the feather band.
        template <class Emit>
        static void walkRoundedRect(Vector2 minimum, Vector2 maximum, float radius, DrawCorner corners, int segments,
                                    Emit &&emit) {
            constexpr float QuarterTurn = 1.57079632679f;
            const Vector2 centers[4] = {
                Vector2(maximum.x - radius, minimum.y + radius), // top right
                Vector2(maximum.x - radius, maximum.y - radius), // bottom right
                Vector2(minimum.x + radius, maximum.y - radius), // bottom left
                Vector2(minimum.x + radius, minimum.y + radius), // top left
            };
            const float startAngles[4] = {-QuarterTurn, 0.0f, QuarterTurn, 2.0f * QuarterTurn};
            const DrawCorner masks[4] = {DrawCorner::TopRight, DrawCorner::BottomRight, DrawCorner::BottomLeft,
                                         DrawCorner::TopLeft};

            for (size_t corner = 0; corner < 4; corner += 1) {
                const Vector2 center = centers[corner];
                const float start = startAngles[corner];
                if (hasCorner(corners, masks[corner])) {
                    for (size_t step = 0; step <= segments; step += 1) {
                        const float sweep = QuarterTurn * static_cast<float>(step) / static_cast<float>(segments);
                        const Vector2 direction(std::cos(start + sweep), std::sin(start + sweep));
                        emit(PathPoint{center + direction * radius, direction});
                    }
                } else {
                    const Vector2 incoming(std::cos(start), std::sin(start));
                    const Vector2 outgoing(std::cos(start + QuarterTurn), std::sin(start + QuarterTurn));
                    const Vector2 sharp = center + incoming * radius + outgoing * radius;
                    emit(PathPoint{sharp, incoming});
                    emit(PathPoint{sharp, outgoing});
                }
            }
        }

        /// Fills the outline and feathers the coverage over a pixel just outside it. The
        /// path is the shape's edge, so the fade makes a widget about half a pixel wider
        /// than the same rectangle drawn hard edged - that half pixel of blur is what
        /// turns a stair step into a curve.
        void addRoundedRectFilled(Vector2 minimum, Vector2 maximum, Pixel color, float rounding, DrawCorner corners) {
            if (maximum.x <= minimum.x || maximum.y <= minimum.y) {
                return;
            }
            const float radius = clampRoundedRectRadius(minimum, maximum, rounding);
            const int segments = roundedRectSegments(radius);
            const Vector2 uv(0.0f, 0.0f);

            beginPrimitive(InvalidTexture);
            const std::uint32_t center = vertexCount();
            addVertex(Vector2((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f), uv, color);

            std::uint32_t firstInner = 0;
            std::uint32_t firstOuter = 0;
            std::uint32_t previousInner = 0;
            std::uint32_t previousOuter = 0;
            int pointCount = 0;
            walkRoundedRect(minimum, maximum, radius, corners, segments, [&](const PathPoint &point) {
                const std::uint32_t inner = vertexCount();
                addVertex(point.position, uv, color);
                const std::uint32_t outer = vertexCount();
                addVertex(point.position + point.normal * FeatherWidth, uv, color.withAlpha(0.0f));

                if (pointCount == 0) {
                    firstInner = inner;
                    firstOuter = outer;
                } else {
                    // A triangle of the interior fan, then the two of the fading band.
                    indices.push_back(center);
                    indices.push_back(previousInner);
                    indices.push_back(inner);
                    indices.push_back(previousInner);
                    indices.push_back(inner);
                    indices.push_back(outer);
                    indices.push_back(previousInner);
                    indices.push_back(outer);
                    indices.push_back(previousOuter);
                    commands.back().indexCount += 9;
                }

                previousInner = inner;
                previousOuter = outer;
                pointCount += 1;
            });

            if (pointCount > 1) {
                indices.push_back(center);
                indices.push_back(previousInner);
                indices.push_back(firstInner);
                indices.push_back(previousInner);
                indices.push_back(firstInner);
                indices.push_back(firstOuter);
                indices.push_back(previousInner);
                indices.push_back(firstOuter);
                indices.push_back(previousOuter);
                commands.back().indexCount += 9;
            }
        }

        /// Draws the border along that same path, keeping the band it always had - from
        /// the path inwards by `thickness` - and fading over a pixel on each side of it.
        /// A stroke thicker than the shape it hugs folds over itself, exactly like any
        /// stroked polyline.
        // Four vertex layers create three bands:
        //
        //        outer                         inner
        //          ↓                             ↓
        //
        //   A0 ============================ B0    alpha = 0
        //    │                              │
        //    │       transparent / feather  │    0 → 1
        //    │                              │
        //   A1 ============================ B1    alpha = 1
        //    │                              │
        //    │          solid stroke        │    1 → 1
        //    │                              │
        //   A2 ============================ B2    alpha = 1
        //    │                              │
        //    │       transparent / feather  │    1 → 0
        //    │                              │
        //   A3 ============================ B3    alpha = 0
        //
        //        ←────── stroke width ──────→
        //
        // A0-A1 : outer feather
        // A1-A2 : solid stroke
        // A2-A3 : inner feather
        //
        // Each adjacent pair of rings is connected by triangles.
        void addRoundedRectOutline(Vector2 minimum, Vector2 maximum, Pixel color, float rounding, DrawCorner corners,
                                   float thickness) {
            if (maximum.x <= minimum.x || maximum.y <= minimum.y || thickness <= 0.0f) {
                return;
            }
            const float radius = clampRoundedRectRadius(minimum, maximum, rounding);
            const int segments = roundedRectSegments(radius);
            const Vector2 uv(0.0f, 0.0f);

            // Four offsets across the border: fading in, solid, solid, fading out.
            const float offsets[4] = {-thickness - FeatherWidth, -thickness, 0.0f, FeatherWidth};
            const float alphas[4] = {0.0f, color.a, color.a, 0.0f};

            beginPrimitive(InvalidTexture);
            std::uint32_t previous[4] = {0, 0, 0, 0};
            std::uint32_t first[4] = {0, 0, 0, 0};
            int pointCount = 0;
            walkRoundedRect(minimum, maximum, radius, corners, segments, [&](const PathPoint &point) {
                std::uint32_t ring[4] = {0, 0, 0, 0};
                for (size_t band = 0; band < 4; band += 1) {
                    ring[band] = vertexCount();
                    addVertex(point.position + point.normal * offsets[band], uv, color.withAlpha(alphas[band]));
                }

                if (pointCount == 0) {
                    for (int band = 0; band < 4; ++band) {
                        first[band] = ring[band];
                    }
                } else {
                    for (size_t band = 0; band + 1 < 4; band += 1) {
                        indices.push_back(previous[band]);
                        indices.push_back(ring[band]);
                        indices.push_back(ring[band + 1]);
                        indices.push_back(previous[band]);
                        indices.push_back(ring[band + 1]);
                        indices.push_back(previous[band + 1]);
                    }
                    commands.back().indexCount += 18;
                }

                for (int band = 0; band < 4; ++band) {
                    previous[band] = ring[band];
                }
                pointCount += 1;
            });

            if (pointCount > 1) {
                for (int band = 0; band + 1 < 4; ++band) {
                    indices.push_back(previous[band]);
                    indices.push_back(first[band]);
                    indices.push_back(first[band + 1]);
                    indices.push_back(previous[band]);
                    indices.push_back(first[band + 1]);
                    indices.push_back(previous[band + 1]);
                }
                commands.back().indexCount += 18;
            }
        }
        Z::ArrayList<DrawVertex> vertices;
        Z::ArrayList<DrawIndex> indices;
        Z::ArrayList<DrawCommand> commands;
        Z::ArrayList<Rect> clipStack;
        Rect displayRect;
    };
    struct DrawData {
        const DrawList *background = nullptr;
        const DrawList *foreground = nullptr;
        /// Texture lifetime requests owned by the UI context. The backend replays
        /// them before drawing, in order.
        std::span<const ResourceCmd> resourceCommands;
        Vector2 displaySize;
        bool valid = false;
    };
} // namespace ReZeroGui