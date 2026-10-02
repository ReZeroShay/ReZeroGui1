// Headless checks for the bitmap text path: atlas construction, glyph metrics,
// glyph quads, and the texture requests the backend receives.
//
// No device is involved. The pixel checks read the atlas the same way the GPU
// would - through the UVs of the emitted quads - so a wrong UV, a wrong pixel
// origin or a blank atlas all fail here instead of showing up as missing text on
// screen.
#include <ReZeroGui/ReZeroGui.h>
#include <ReZeroGui/Ui.h>
#include <ReZeroGui/Renderer/Dx11Impl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

namespace {

    struct TestAllocator {
        void *allocate(std::size_t size, [[maybe_unused]] std::size_t alignment) noexcept { return std::malloc(size); }
        void deallocate(void *ptr) noexcept { std::free(ptr); }
    };

    int failures = 0;
    int checks = 0;

    void report(bool passed, const char *expression, int line) {
        checks += 1;
        if (!passed) {
            failures += 1;
            std::printf("FAIL(line %d): %s\n", line, expression);
        }
    }

#define CHECK(expression) report((expression), #expression, __LINE__)

    using ReZeroGui::Context;
    using ReZeroGui::FontAtlas;
    using ReZeroGui::GlyphQuad;
    using ReZeroGui::Rect;
    using ReZeroGui::TextureId;
    using ReZeroGui::Vector2;

    /// A UTF-8 view over a byte string. Text is decoded from bytes, so the char
    /// spelling is only a naming detail.
    auto utf8(std::string_view text) noexcept -> ReZeroGui::Z::u8StringView {
        return ReZeroGui::Z::u8StringView(reinterpret_cast<const char8_t *>(text.data()), text.size());
    }

    /// Composites glyphs out of the atlas into a coverage buffer, which is what
    /// the pixel shader does on the GPU (color * sampled).
    struct CoverageBitmap {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels;

        void reset(int newWidth, int newHeight) {
            width = newWidth;
            height = newHeight;
            pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
        }

        void blit(const ReZeroTrueType::Image &grid, Rect destination, Vector2 uv0, Vector2 uv1) {
            const int x0 = static_cast<int>(destination.x);
            const int y0 = static_cast<int>(destination.y);
            const int spanX = static_cast<int>(destination.width);
            const int spanY = static_cast<int>(destination.height);
            if (spanX <= 0 || spanY <= 0) {
                return;
            }

            for (int y = 0; y < spanY; ++y) {
                const int targetY = y0 + y;
                if (targetY < 0 || targetY >= height) {
                    continue;
                }
                for (int x = 0; x < spanX; ++x) {
                    const int targetX = x0 + x;
                    if (targetX < 0 || targetX >= width) {
                        continue;
                    }

                    // Same mapping the hardware uses for a 1:1 aligned quad:
                    // pixel centre -> texel centre.
                    const float u = uv0.x + (static_cast<float>(x) + 0.5f) / static_cast<float>(spanX) *
                                                 (uv1.x - uv0.x);
                    const float v = uv0.y + (static_cast<float>(y) + 0.5f) / static_cast<float>(spanY) *
                                                 (uv1.y - uv0.y);
                    const auto texelX = static_cast<std::uint32_t>(u * static_cast<float>(grid.width));
                    const auto texelY = static_cast<std::uint32_t>(v * static_cast<float>(grid.height));
                    if (texelX >= grid.width || texelY >= grid.height) {
                        continue;
                    }

                    const ReZeroTrueType::Pixel &texel = grid[texelX, texelY];
                    std::uint8_t &target = pixels[static_cast<std::size_t>(targetY) * width + targetX];
                    target = target > texel.a ? target : texel.a;
                }
            }
        }

        std::size_t inkedPixels() const {
            std::size_t count = 0;
            for (const std::uint8_t value : pixels) {
                if (value != 0) {
                    count += 1;
                }
            }
            return count;
        }

        Rect inkBounds() const {
            int minX = width;
            int minY = height;
            int maxX = -1;
            int maxY = -1;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (pixels[static_cast<std::size_t>(y) * width + x] != 0) {
                        minX = x < minX ? x : minX;
                        minY = y < minY ? y : minY;
                        maxX = x > maxX ? x : maxX;
                        maxY = y > maxY ? y : maxY;
                    }
                }
            }
            if (maxX < 0) {
                return Rect();
            }
            return Rect(static_cast<float>(minX), static_cast<float>(minY), static_cast<float>(maxX - minX + 1),
                        static_cast<float>(maxY - minY + 1));
        }
    };

    /// Walks a string exactly like Context::renderText does and composites every
    /// glyph it would emit.
    CoverageBitmap rasterize(const FontAtlas &atlas, ReZeroGui::FontId font, std::string_view text, Vector2 origin) {
        CoverageBitmap bitmap;
        bitmap.reset(256, 64);

        const auto *image = atlas.atlasImage(font);
        if (image == nullptr) {
            return bitmap;
        }

        float penX = origin.x;
        float baselineY = origin.y + atlas.ascent(font);
        const float lineHeight = atlas.lineHeight(font);
        const float startX = origin.x;
        const ReZeroGui::Z::u8StringView bytes = utf8(text);

        std::size_t offset = 0;
        while (offset < bytes.size()) {
            auto codepoint = ReZeroGui::unicode::decodeUtf8(bytes, offset);
            if (!codepoint) {
                break;
            }
            if (*codepoint == '\n') {
                penX = startX;
                baselineY += lineHeight;
                continue;
            }
            float nextX = penX;
            GlyphQuad quad{};
            if (!atlas.getGlyphQuad(font, *codepoint, &nextX, &baselineY, quad)) {
                continue;
            }
            if (quad.uv0.x != quad.uv1.x && quad.uv0.y != quad.uv1.y) {
                bitmap.blit(*image, Rect::fromPoints(quad.position0, quad.position1), quad.uv0, quad.uv1);
            }
            penX = nextX;
        }
        return bitmap;
    }

    /// Horizontal extent of every textured quad, in draw order.
    struct QuadSpan {
        float left = 0.0f;
        float right = 0.0f;
    };

    std::vector<QuadSpan> texturedQuadSpans(const ReZeroGui::DrawList &list, TextureId texture) {
        std::vector<QuadSpan> spans;
        const auto vertices = list.currentVertices();
        const auto indices = list.currentIndices();
        for (const ReZeroGui::DrawCommand &command : list.currentCommands()) {
            if (command.textureId != texture) {
                continue;
            }
            const std::uint32_t quadCount = command.indexCount / 6;
            for (std::uint32_t quad = 0; quad < quadCount; ++quad) {
                const std::uint32_t base = indices[command.indexOffset + quad * 6];
                spans.push_back(QuadSpan{vertices[base].position.x, vertices[base + 2].position.x});
            }
        }
        return spans;
    }

    /// Does the atlas hold any ink inside a quad's UV rectangle?
    bool uvRectHasInk(const ReZeroTrueType::Image &grid, Vector2 uv0, Vector2 uv1) {
        const auto x0 = static_cast<std::uint32_t>(uv0.x * static_cast<float>(grid.width));
        const auto y0 = static_cast<std::uint32_t>(uv0.y * static_cast<float>(grid.height));
        const auto x1 = static_cast<std::uint32_t>(uv1.x * static_cast<float>(grid.width));
        const auto y1 = static_cast<std::uint32_t>(uv1.y * static_cast<float>(grid.height));

        for (std::uint32_t y = y0; y < y1 && y < grid.height; ++y) {
            for (std::uint32_t x = x0; x < x1 && x < grid.width; ++x) {
                if (grid[x, y].a != 0) {
                    return true;
                }
            }
        }
        return false;
    }

    void testDefaultFontPath() {
#if !defined(REZEROGUI_DEFAULT_FONT_PATH)
        CHECK(false && "REZEROGUI_DEFAULT_FONT_PATH should come from the build system");
#else
        std::printf("default font path: %s\n", REZEROGUI_DEFAULT_FONT_PATH);
        std::ifstream file(REZEROGUI_DEFAULT_FONT_PATH, std::ios::binary);
        CHECK(file.is_open());
#endif
    }

    void testAtlasBuilds(Context &ctx) {
        // The atlas is built lazily from the frame loop.
        CHECK(ctx.atlasTexture() == ReZeroGui::InvalidTexture);
        CHECK(!ctx.fonts().isBuilt());

        ctx.beginFrame(1.0f / 60.0f);

        const FontAtlas &atlas = ctx.fonts();
        CHECK(atlas.isBuilt());
        CHECK(atlas.fontCount() == 1);

        const ReZeroGui::FontId font = atlas.defaultFont();
        CHECK(font != ReZeroGui::InvalidFont);
        CHECK(font == 0);

        // Vertical metrics have to be plausible: a 16 px face is neither 0 px tall
        // nor absurdly large, and the ascent lives inside the line height.
        CHECK(atlas.lineHeight(font) > 8.0f);
        CHECK(atlas.lineHeight(font) < 32.0f);
        CHECK(atlas.ascent(font) > 0.0f);
        CHECK(atlas.ascent(font) < atlas.lineHeight(font));

        // Advances come from the horizontal metrics.
        CHECK(atlas.glyphAdvance(font, ' ') > 0.0f);
        CHECK(atlas.glyphAdvance(font, 'M') > 0.0f);
        CHECK(atlas.glyphAdvance(font, 'i') > 0.0f);
        // Monospace faces give both the same width; proportional ones give the 'i'
        // less. Either way the wide glyph must not be narrower.
        CHECK(atlas.glyphAdvance(font, 'M') >= atlas.glyphAdvance(font, 'i'));

        const auto *image = atlas.atlasImage(font);
        CHECK(image != nullptr);
        if (image != nullptr) {
            CHECK(image->width > 0);
            CHECK(image->height > 0);
            CHECK(image->width <= 4096);
            // The packer builds a square, power of two atlas.
            CHECK(image->width == image->height);
            CHECK((image->width & (image->width - 1)) == 0);
        }

        // Publishing the draw lists is what makes drawItems() usable.
        ctx.endFrame();
    }

    void testFontTextureRequest(Context &ctx) {
        const auto commands = ctx.textureCommands();
        CHECK(commands.size() == 1);
        if (commands.empty()) {
            return;
        }

        const ReZeroGui::ResourceCmd &command = commands[0];
        CHECK(command.type == ReZeroGui::ResourceCmdType::CreateTexture);
        CHECK(command.handle == ctx.atlasTexture());
        CHECK(command.handle != ReZeroGui::InvalidTexture);

        const auto *image = ctx.fonts().atlasImage(ctx.fonts().defaultFont());
        CHECK(image != nullptr);
        if (image == nullptr) {
            return;
        }

        // The backend uploads exactly the atlas the UI measured against.
        CHECK(command.desc.width == image->width);
        CHECK(command.desc.height == image->height);
        CHECK(command.pixels == reinterpret_cast<const std::uint8_t *>(image->data()));

        // The atlas must contain glyph coverage, not just zeroes.
        std::size_t inked = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(image->width) * image->height; ++i) {
            if (image->data()[i].a != 0) {
                inked += 1;
            }
        }
        CHECK(inked > 100);
    }

    void testGlyphQuads(Context &ctx) {
        const FontAtlas &atlas = ctx.fonts();
        const ReZeroGui::FontId font = atlas.defaultFont();
        const auto *image = atlas.atlasImage(font);
        if (image == nullptr) {
            CHECK(false && "atlas image missing");
            return;
        }

        for (const char glyph : std::string_view("AZMgljQ")) {
            float penX = 100.0f;
            float baselineY = 40.0f;
            GlyphQuad quad{};
            CHECK(atlas.getGlyphQuad(font, static_cast<std::uint32_t>(glyph), &penX, &baselineY, quad));

            const float maskWidth = quad.position1.x - quad.position0.x;
            const float maskHeight = quad.position1.y - quad.position0.y;
            CHECK(maskWidth > 0.0f);
            CHECK(maskHeight > 0.0f);

            // The pen advanced by the advance width, the baseline did not move.
            CHECK(penX > 100.0f);
            CHECK(baselineY == 40.0f);

            // Quads are pixel aligned, so the atlas is sampled 1:1 and the glyph is
            // not resampled.
            CHECK(quad.position0.x == static_cast<float>(static_cast<int>(quad.position0.x)));
            CHECK(quad.position0.y == static_cast<float>(static_cast<int>(quad.position0.y)));

            // The quad covers exactly as many pixels as the glyph has texels. The
            // atlas side is a power of two, so these products are exact.
            CHECK((quad.uv1.x - quad.uv0.x) * static_cast<float>(image->width) == maskWidth);
            CHECK((quad.uv1.y - quad.uv0.y) * static_cast<float>(image->height) == maskHeight);

            // UVs stay inside the atlas.
            CHECK(quad.uv0.x >= 0.0f && quad.uv0.x <= 1.0f);
            CHECK(quad.uv1.x >= 0.0f && quad.uv1.x <= 1.0f);
            CHECK(quad.uv1.x > quad.uv0.x);
            CHECK(quad.uv1.y > quad.uv0.y);

            // The ink is where the quad says it is.
            CHECK(uvRectHasInk(*image, quad.uv0, quad.uv1));

            // Upright letters sit above the baseline.
            CHECK(quad.position0.y < baselineY);
            CHECK(quad.position1.y > quad.position0.y);
        }
    }

    void testBlankGlyphAdvances() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(400.0f, 300.0f));
        ctx.beginFrame(1.0f / 60.0f);

        const FontAtlas &atlas = ctx.fonts();
        const ReZeroGui::FontId font = atlas.defaultFont();

        const float advance = atlas.glyphAdvance(font, ' ');
        CHECK(advance > 0.0f);

        float penX = 10.0f;
        float baselineY = 20.0f;
        GlyphQuad quad{};
        CHECK(atlas.getGlyphQuad(font, ' ', &penX, &baselineY, quad));
        // A space moves the pen but produces no geometry.
        CHECK(penX == 10.0f + advance);
        CHECK(baselineY == 20.0f);
        CHECK(quad.uv0.x == quad.uv1.x);
        CHECK(quad.uv0.y == quad.uv1.y);
        CHECK(quad.position0 == quad.position1);

        // A code point the atlas does not carry is rejected outright.
        float unknownPen = 5.0f;
        GlyphQuad unknownQuad{};
        CHECK(!atlas.getGlyphQuad(font, 0x4E2D, &unknownPen, &baselineY, unknownQuad));
        CHECK(unknownPen == 5.0f);
    }

    void testDrawListReceivesText(Context &ctx) {
        auto *foreground = ctx.drawItems().foreground;
        CHECK(foreground != nullptr);
        if (foreground == nullptr) {
            return;
        }

        const std::size_t vertexCountBefore = foreground->currentVertices().size();
        const std::size_t indexCountBefore = foreground->currentIndices().size();

        constexpr std::string_view Text = "Hello, ReZero!";
        const Vector2 size = ctx.calculateTextSize(utf8(Text));
        CHECK(size.x > 0.0f);
        CHECK(size.y > 0.0f);

        // A space advances the pen without emitting a quad.
        std::size_t expectedQuads = Text.size();
        for (const char character : Text) {
            if (character == ' ') {
                expectedQuads -= 1;
            }
        }

        ctx.renderText(Vector2(8.0f, 8.0f), utf8(Text), ReZeroGui::Pixel(1.0f, 1.0f, 1.0f, 1.0f),
                       ReZeroGui::InvalidFont, 0.0f);

        const auto vertices = foreground->currentVertices();
        const auto indices = foreground->currentIndices();
        const auto commands = foreground->currentCommands();

        // Four vertices and six indices per drawn glyph.
        CHECK(vertices.size() - vertexCountBefore == 4 * expectedQuads);
        CHECK(indices.size() - indexCountBefore == 6 * expectedQuads);

        CHECK(!commands.empty());
        if (commands.empty()) {
            return;
        }

        // Text has to be tagged with the atlas texture, otherwise the renderer
        // would sample the white 1x1 texture and draw solid blocks for every glyph.
        const ReZeroGui::DrawCommand &last = commands[commands.size() - 1];
        CHECK(last.textureId == ctx.atlasTexture());
        CHECK(last.textureId != ReZeroGui::InvalidTexture);
        CHECK(last.indexCount > 0);

        const auto *image = ctx.fonts().atlasImage(ctx.fonts().defaultFont());
        CHECK(image != nullptr);
        if (image == nullptr) {
            return;
        }

        // Every emitted quad has to land on real ink and stay inside the atlas.
        std::size_t quadsChecked = 0;
        for (const ReZeroGui::DrawCommand &command : commands) {
            if (command.textureId != ctx.atlasTexture()) {
                continue;
            }
            const std::uint32_t quadCount = command.indexCount / 6;
            for (std::uint32_t quad = 0; quad < quadCount; ++quad) {
                // The index buffer is the authoritative link between a command and
                // its vertices.
                const std::uint32_t base = indices[command.indexOffset + quad * 6];
                const ReZeroGui::DrawVertex &minimum = vertices[base];
                const ReZeroGui::DrawVertex &maximum = vertices[base + 2];

                CHECK(maximum.uv.x > minimum.uv.x);
                CHECK(maximum.uv.y > minimum.uv.y);
                CHECK(uvRectHasInk(*image, minimum.uv, maximum.uv));
                CHECK(maximum.position.x > minimum.position.x);
                CHECK(maximum.position.y > minimum.position.y);
                quadsChecked += 1;
            }
        }
        CHECK(quadsChecked == expectedQuads);
    }

    void testSolidGeometryKeepsWhiteTexture(Context &ctx) {
        auto *foreground = ctx.drawItems().foreground;
        CHECK(foreground != nullptr);
        if (foreground == nullptr) {
            return;
        }

        const std::size_t commandCountBefore = foreground->currentCommands().size();

        ctx.renderArrow(Vector2(50.0f, 50.0f), ReZeroGui::ArrowDirection::Down,
                        ReZeroGui::Pixel(1.0f, 1.0f, 1.0f, 1.0f), 6.0f);

        const auto commands = foreground->currentCommands();
        CHECK(commands.size() > commandCountBefore);
        // Solid geometry must NOT be tagged with the atlas texture; that is what
        // the 1x1 white texture in the backend is for.
        CHECK(commands[commands.size() - 1].textureId == ReZeroGui::InvalidTexture);
    }

    void testCoverageRasterization(Context &ctx) {
        const FontAtlas &atlas = ctx.fonts();
        const ReZeroGui::FontId font = atlas.defaultFont();
        const float baselineY = 8.0f + atlas.ascent(font);

        const CoverageBitmap bitmap = rasterize(atlas, font, "ReZero", Vector2(4.0f, 8.0f));
        const std::size_t ink = bitmap.inkedPixels();
        // Six letters at 16 px cannot be ink free, and cannot fill the whole buffer.
        CHECK(ink > 40);
        CHECK(ink < bitmap.pixels.size() / 4);

        const Rect bounds = bitmap.inkBounds();
        // The string starts at the pen and stays inside the measured width.
        CHECK(bounds.x >= 4.0f);
        const Vector2 measured = ctx.calculateTextSize(utf8("ReZero"));
        CHECK(bounds.maxX() <= 4.0f + measured.x + 1.0f);
        // "ReZero" has no descenders, so all of its ink is above the baseline.
        CHECK(bounds.maxY() <= baselineY + 1.0f);

        // A space only string draws nothing.
        const CoverageBitmap blank = rasterize(atlas, font, "   ", Vector2(4.0f, 8.0f));
        CHECK(blank.inkedPixels() == 0);

        // Two lines put ink at two different heights.
        const CoverageBitmap twoLines = rasterize(atlas, font, "MMMM\nMMMM", Vector2(4.0f, 8.0f));
        CHECK(twoLines.inkedPixels() > bitmap.inkedPixels());
        CHECK(twoLines.inkBounds().height > atlas.lineHeight(font));
    }

    void testMeasureAndWrap(Context &ctx) {
        const Vector2 oneLine = ctx.calculateTextSize(ReZeroGui::Z::StringView("Hello"));
        const Vector2 twoLines = ctx.calculateTextSize(ReZeroGui::Z::StringView("Hello\nWorld"));
        CHECK(oneLine.x > 0.0f);
        CHECK(oneLine.y > 0.0f);
        CHECK(twoLines.y > oneLine.y * 1.9f);
        CHECK(twoLines.y < oneLine.y * 2.1f);

        // The two spellings of the same string have to measure the same.
        CHECK(ctx.calculateTextSize(utf8("Hello")).x == oneLine.x);

        // Wrapping adds lines and never exceeds the requested width.
        const Vector2 wrapped =
            ctx.calculateTextSize(ReZeroGui::Z::StringView("Hello World Hello World"), ReZeroGui::InvalidFont, 40.0f);
        CHECK(wrapped.y > oneLine.y);
        CHECK(wrapped.x <= 40.0f + 1.0f);
    }

    /// Style::letterSpacing has to move the drawn glyphs and the measured width by
    /// exactly the same amount, otherwise layout and pixels would disagree.
    void testLetterSpacing(Context &ctx) {
        constexpr std::string_view Sample = "abcde";
        auto *foreground = ctx.drawItems().foreground;
        CHECK(foreground != nullptr);
        if (foreground == nullptr) {
            return;
        }

        CHECK(ctx.currentStyle().letterSpacing == 0.0f);

        ctx.beginFrame(1.0f / 60.0f);
        const Vector2 plainSize = ctx.calculateTextSize(utf8(Sample));
        ctx.renderText(Vector2(0.0f, 0.0f), utf8(Sample), ReZeroGui::Pixel(1.0f, 1.0f, 1.0f, 1.0f),
                       ReZeroGui::InvalidFont, 0.0f);
        ctx.endFrame();
        const std::vector<QuadSpan> plainSpans = texturedQuadSpans(*foreground, ctx.atlasTexture());
        CHECK(plainSpans.size() == Sample.size());

        constexpr float Spacing = 2.0f;
        ctx.currentStyle().letterSpacing = Spacing;

        ctx.beginFrame(1.0f / 60.0f);
        const Vector2 spacedSize = ctx.calculateTextSize(utf8(Sample));
        ctx.renderText(Vector2(0.0f, 0.0f), utf8(Sample), ReZeroGui::Pixel(1.0f, 1.0f, 1.0f, 1.0f),
                       ReZeroGui::InvalidFont, 0.0f);
        ctx.endFrame();
        const std::vector<QuadSpan> spacedSpans = texturedQuadSpans(*foreground, ctx.atlasTexture());
        CHECK(spacedSpans.size() == plainSpans.size());

        // Measuring adds one spacing per glyph. The two sums accumulate in a
        // different order, so allow for float rounding.
        const float widthDelta = spacedSize.x - plainSize.x;
        CHECK(widthDelta > Spacing * static_cast<float>(Sample.size()) - 0.01f);
        CHECK(widthDelta < Spacing * static_cast<float>(Sample.size()) + 0.01f);

        // Drawing shifts glyph i by exactly i * spacing: the first glyph does not
        // move, and the pen carries the shift with no rounding drift because
        // rounding is invariant under an integer shift.
        for (std::size_t i = 0; i < plainSpans.size() && i < spacedSpans.size(); ++i) {
            CHECK(spacedSpans[i].left - plainSpans[i].left == Spacing * static_cast<float>(i));
            CHECK(spacedSpans[i].right - plainSpans[i].right == Spacing * static_cast<float>(i));
        }
        if (!spacedSpans.empty()) {
            CHECK(spacedSpans[0].left == plainSpans[0].left);
        }

        // The drawn text still fits inside what was measured.
        if (!spacedSpans.empty()) {
            CHECK(spacedSpans.back().right <= spacedSize.x + 1.0f);
            CHECK(plainSpans.back().right <= plainSize.x + 1.0f);
        }

        ctx.currentStyle().letterSpacing = 0.0f;
        CHECK(ctx.calculateTextSize(utf8(Sample)).x == plainSize.x);
    }

    void testStyleScaling() {
        ReZeroGui::Style style;
        CHECK(style.letterSpacing == 0.0f);

        style.letterSpacing = 2.0f;
        const ReZeroGui::Style scaled = style.scaled(1.5f);
        // Pixel measurements scale with the UI.
        CHECK(scaled.letterSpacing == 3.0f);
        CHECK(scaled.fontSize == style.fontSize * 1.5f);
        CHECK(scaled.windowPadding.x == style.windowPadding.x * 1.5f);
    }

    void testWindowTitleDrawsText() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(800.0f, 600.0f));
        ctx.beginFrame(1.0f / 60.0f);

        {
            ReZeroGui::WindowScope panel(ctx, (char *)"Title Text");
            panel.height(120.0f).width(240.0f);
        }
        ctx.endFrame();

        const auto *foreground = ctx.drawItems().foreground;
        CHECK(foreground != nullptr);
        if (foreground == nullptr) {
            return;
        }

        // The title bar is solid geometry plus the title's glyphs, so at least one
        // command has to be textured with the atlas.
        bool foundText = false;
        for (const ReZeroGui::DrawCommand &command : foreground->currentCommands()) {
            if (command.textureId == ctx.atlasTexture() && command.indexCount >= 6) {
                foundText = true;
            }
        }
        CHECK(foundText);

        // The title has to sit in the title bar, which starts at the window origin.
        bool titleInTitleBar = false;
        for (const ReZeroGui::DrawVertex &vertex : foreground->currentVertices()) {
            if (vertex.uv.x == 0.0f && vertex.uv.y == 0.0f) {
                continue;
            }
            CHECK(vertex.position.y >= 32.0f);
            CHECK(vertex.position.y <= 32.0f + 32.0f);
            CHECK(vertex.position.x >= 32.0f);
            titleInTitleBar = true;
        }
        CHECK(titleInTitleBar);
    }

    void testTextureLifetimeQueue() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(320.0f, 240.0f));

        const std::uint8_t pixels[4] = {1, 2, 3, 4};
        const TextureId first = ctx.createTexture(1, 1, pixels);
        const TextureId second = ctx.createTexture(2, 2, pixels);
        CHECK(first != ReZeroGui::InvalidTexture);
        CHECK(second != ReZeroGui::InvalidTexture);
        // Handles are unique and never reused.
        CHECK(first != second);

        // Degenerate sizes are refused instead of reaching the device.
        CHECK(ctx.createTexture(0, 4, pixels) == ReZeroGui::InvalidTexture);
        CHECK(ctx.createTexture(4, 0, pixels) == ReZeroGui::InvalidTexture);

        ctx.destroyTexture(first);

        ctx.beginFrame(1.0f / 60.0f);
        ctx.endFrame();

        // The font atlas adds one create; the queue keeps its order.
        const auto commands = ctx.textureCommands();
        CHECK(commands.size() == 4);
        if (commands.size() == 4) {
            CHECK(commands[0].type == ReZeroGui::ResourceCmdType::CreateTexture);
            CHECK(commands[0].handle == first);
            CHECK(commands[0].desc.width == 1);
            CHECK(commands[0].desc.height == 1);
            CHECK(commands[1].type == ReZeroGui::ResourceCmdType::CreateTexture);
            CHECK(commands[1].handle == second);
            CHECK(commands[1].desc.width == 2);
            CHECK(commands[1].desc.height == 2);
            CHECK(commands[2].type == ReZeroGui::ResourceCmdType::DestroyTexture);
            CHECK(commands[2].handle == first);
            CHECK(commands[3].type == ReZeroGui::ResourceCmdType::CreateTexture);
            CHECK(commands[3].handle == ctx.atlasTexture());
        }
        CHECK(ctx.atlasTexture() != first);
        CHECK(ctx.atlasTexture() != second);

        // The queue is handed to the backend through the draw data.
        CHECK(ctx.drawItems().resourceCommands.size() == commands.size());
        CHECK(ctx.drawItems().resourceCommands.data() == commands.data());
    }

    void testAtlasTextureIsOnlyCreatedOnce() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(320.0f, 240.0f));

        for (int frame = 0; frame < 3; ++frame) {
            ctx.beginFrame(1.0f / 60.0f);
            ctx.endFrame();
        }

        const auto commands = ctx.textureCommands();
        CHECK(commands.size() == 1);
        CHECK(ctx.atlasTexture() != ReZeroGui::InvalidTexture);
    }

    /// The renderer has to survive being driven without a device, because that is
    /// what the effect of a failed initialize() looks like to the UI layer.
    void testRendererWithoutDevice(ReZeroGui::Dx11Renderer &renderer) {
        CHECK(!renderer.ensureTexture(1, 4, 4, nullptr));
        CHECK(!renderer.updateTexture(1, nullptr));
        renderer.destroyTexture(1);
        renderer.destroyTexture(ReZeroGui::InvalidTexture);
        renderer.applyResourceCommands(std::span<const ReZeroGui::ResourceCmd>());
    }

} // namespace

int main() {
    // Unbuffered so that a crash still shows how far the checks got.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    testDefaultFontPath();

    TestAllocator allocator;
    ReZeroGui::AllocatorView allocatorView(allocator);
    ReZeroGui::Dx11Renderer renderer(allocatorView);
    Context ctx(allocatorView);
    ctx.withDisplaySize(Vector2(800.0f, 600.0f));

    testAtlasBuilds(ctx);
    testFontTextureRequest(ctx);
    testGlyphQuads(ctx);
    testDrawListReceivesText(ctx);
    testSolidGeometryKeepsWhiteTexture(ctx);
    testCoverageRasterization(ctx);
    testMeasureAndWrap(ctx);

    testBlankGlyphAdvances();
    testLetterSpacing(ctx);
    testStyleScaling();
    testWindowTitleDrawsText();
    testTextureLifetimeQueue();
    testAtlasTextureIsOnlyCreatedOnce();
    testRendererWithoutDevice(renderer);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
