// End to end check of the Direct3D 11 text path.
//
// A real device, a real swap chain and the real Dx11Renderer are used; the frame
// is rendered into the hidden window's back buffer and read back on the CPU, so
// the assertions look at the pixels the GPU actually produced. That covers the
// parts the headless test cannot reach: shader compilation, resource command
// replay, texture creation and the sampler/blend/scissor state.
#include <ReZeroGui/ReZeroGui.h>
#include <ReZeroGui/Ui.h>
#include <ReZeroGui/Renderer/Dx11Impl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

    template <typename T> void release(T *&object) noexcept {
        if (object != nullptr) {
            object->Release();
            object = nullptr;
        }
    }

    /// A CPU copy of the back buffer, RGBA8.
    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> pixels;

        const std::uint8_t *at(std::uint32_t x, std::uint32_t y) const {
            return pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
        }

        /// Pixels where the red channel dominates, i.e. the red text we drew.
        std::size_t countRedDominant(std::uint8_t threshold) const {
            std::size_t count = 0;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::uint8_t *texel = at(x, y);
                    if (texel[0] >= threshold && texel[0] > texel[1] && texel[0] > texel[2]) {
                        count += 1;
                    }
                }
            }
            return count;
        }

        std::size_t countGreenDominant(std::uint8_t threshold) const {
            std::size_t count = 0;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::uint8_t *texel = at(x, y);
                    if (texel[1] >= threshold && texel[1] > texel[0] && texel[1] > texel[2]) {
                        count += 1;
                    }
                }
            }
            return count;
        }

        std::uint8_t maximumRed() const {
            std::uint8_t maximum = 0;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::uint8_t *texel = at(x, y);
                    maximum = texel[0] > maximum ? texel[0] : maximum;
                }
            }
            return maximum;
        }

        /// Bounding box of the red dominant pixels, as [minX, minY, maxX, maxY].
        bool redBounds(int &minX, int &minY, int &maxX, int &maxY, std::uint8_t threshold) const {
            minX = static_cast<int>(width);
            minY = static_cast<int>(height);
            maxX = -1;
            maxY = -1;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::uint8_t *texel = at(x, y);
                    if (texel[0] >= threshold && texel[0] > texel[1] && texel[0] > texel[2]) {
                        minX = static_cast<int>(x) < minX ? static_cast<int>(x) : minX;
                        minY = static_cast<int>(y) < minY ? static_cast<int>(y) : minY;
                        maxX = static_cast<int>(x) > maxX ? static_cast<int>(x) : maxX;
                        maxY = static_cast<int>(y) > maxY ? static_cast<int>(y) : maxY;
                    }
                }
            }
            return maxX >= 0;
        }
    };

    constexpr wchar_t WindowClass[] = L"ReZeroGuiTextTest";

    LRESULT CALLBACK testWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    /// Creates a device and a swap chain on a hidden window and wires the renderer
    /// to them. Returns false when no device is available.
    bool createDevice(int width, int height, HWND &window, ReZeroGui::Dx11Renderer &renderer,
                      ID3D11Device *&device, ID3D11DeviceContext *&context, IDXGISwapChain *&swapChain) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = testWindowProcedure;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.lpszClassName = WindowClass;
        if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            std::printf("RegisterClassExW failed\n");
            return false;
        }

        // Never shown: the swap chain only needs somewhere to hang the back buffer.
        window = ::CreateWindowExW(0, WindowClass, L"hidden", WS_OVERLAPPEDWINDOW, 0, 0, width, height, nullptr,
                                   nullptr, windowClass.hInstance, nullptr);
        if (window == nullptr) {
            std::printf("CreateWindowExW failed\n");
            return false;
        }

        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT result = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                             static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, &level,
                                             &context);
        if (FAILED(result)) {
            // Software rasterizer: still a real D3D11 pipeline, just slower.
            result = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                                         static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, &level,
                                         &context);
        }
        if (FAILED(result)) {
            std::printf("D3D11CreateDevice failed: 0x%08lX\n", static_cast<unsigned long>(result));
            return false;
        }

        IDXGIDevice *dxgiDevice = nullptr;
        IDXGIAdapter *adapter = nullptr;
        IDXGIFactory *factory = nullptr;
        const bool haveFactory = SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
                                 SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
                                 SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)));
        release(dxgiDevice);
        release(adapter);
        if (!haveFactory) {
            std::printf("no DXGI factory\n");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferCount = 2;
        description.BufferDesc.Width = static_cast<UINT>(width);
        description.BufferDesc.Height = static_cast<UINT>(height);
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = window;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        const HRESULT swapResult = factory->CreateSwapChain(device, &description, &swapChain);
        release(factory);
        if (FAILED(swapResult)) {
            std::printf("CreateSwapChain failed: 0x%08lX\n", static_cast<unsigned long>(swapResult));
            return false;
        }

        ReZeroGui::RendererParams params;
        params.windowsHandle = window;
        params.deviceHandle = device;
        params.swapChainHandle = swapChain;
        renderer.initialize(&params);
        return true;
    }

    /// Reads the swap chain's current back buffer back into CPU memory.
    Frame readBack(ID3D11Device *device, ID3D11DeviceContext *context, IDXGISwapChain *swapChain) {
        Frame frame;

        ID3D11Texture2D *backbuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
            return frame;
        }

        D3D11_TEXTURE2D_DESC description{};
        backbuffer->GetDesc(&description);
        frame.width = description.Width;
        frame.height = description.Height;
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height * 4, 0);

        D3D11_TEXTURE2D_DESC stagingDescription = description;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;

        ID3D11Texture2D *staging = nullptr;
        if (SUCCEEDED(device->CreateTexture2D(&stagingDescription, nullptr, &staging))) {
            context->CopyResource(staging, backbuffer);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                for (std::uint32_t y = 0; y < frame.height; ++y) {
                    const auto *source = static_cast<const std::uint8_t *>(mapped.pData) +
                                         static_cast<std::size_t>(y) * mapped.RowPitch;
                    std::uint8_t *destination = frame.pixels.data() + static_cast<std::size_t>(y) * frame.width * 4;
                    std::memcpy(destination, source, static_cast<std::size_t>(frame.width) * 4);
                }
                context->Unmap(staging, 0);
            }
            staging->Release();
        }

        backbuffer->Release();
        return frame;
    }

    void clearBackBuffer(ID3D11Device *device, ID3D11DeviceContext *context, IDXGISwapChain *swapChain) {
        ID3D11Texture2D *backbuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
            return;
        }
        ID3D11RenderTargetView *view = nullptr;
        if (SUCCEEDED(device->CreateRenderTargetView(backbuffer, nullptr, &view))) {
            const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            context->ClearRenderTargetView(view, black);
            view->Release();
        }
        backbuffer->Release();
    }

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    constexpr std::uint32_t Width = 512;
    constexpr std::uint32_t Height = 256;

    TestAllocator allocator;
    ReZeroGui::AllocatorView allocatorView(allocator);
    ReZeroGui::Dx11Renderer renderer(allocatorView);

    HWND window = nullptr;
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    IDXGISwapChain *swapChain = nullptr;
    if (!createDevice(static_cast<int>(Width), static_cast<int>(Height), window, renderer, device, context,
                      swapChain)) {
        std::printf("skipped: no Direct3D 11 device available\n");
        return 0;
    }

    ReZeroGui::Context ctx(allocatorView);
    ctx.withDisplaySize(ReZeroGui::Vector2(static_cast<float>(Width), static_cast<float>(Height)));

    // ---- frame 1: empty UI, the control -------------------------------------
    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame blank = readBack(device, context, swapChain);

    CHECK(blank.width == Width);
    CHECK(blank.height == Height);
    // Nothing was drawn, so no red pixel may exist. This is what makes the text
    // assertions below meaningful.
    CHECK(blank.countRedDominant(1) == 0);

    // ---- frame 2: red text on black -----------------------------------------
    constexpr std::string_view Text = "Hello, ReZero!";
    const ReZeroGui::Z::u8StringView text = ReZeroGui::Z::u8StringView(
        reinterpret_cast<const char8_t *>(Text.data()), Text.size());
    const ReZeroGui::Vector2 measured = ctx.calculateTextSize(text);
    CHECK(measured.x > 0.0f);
    CHECK(measured.y > 0.0f);

    // The atlas has to have been published as a texture by beginFrame.
    CHECK(ctx.atlasTexture() != ReZeroGui::InvalidTexture);
    CHECK(ctx.drawItems().resourceCommands.size() >= 1);

    const float textX = 20.0f;
    const float textY = 30.0f;

    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.renderText(ReZeroGui::Vector2(textX, textY), text, ReZeroGui::Pixel(1.0f, 0.0f, 0.0f, 1.0f),
                   ReZeroGui::InvalidFont, 0.0f);
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame textFrame = readBack(device, context, swapChain);

    const std::size_t redPixels = textFrame.countRedDominant(1);
    const std::size_t solidRedPixels = textFrame.countRedDominant(128);
    std::printf("text: %zu red pixels (%zu near solid), max red = %u\n", redPixels, solidRedPixels,
                static_cast<unsigned>(textFrame.maximumRed()));

    // Glyphs must be on screen...
    CHECK(redPixels > 50);
    CHECK(solidRedPixels > 20);
    CHECK(textFrame.maximumRed() > 200);

    // The glyphs have to carry real ink rather than being hairline outlines. Both
    // numbers are deterministic (the atlas is rasterized on the CPU), so they also
    // pin the rasterizer: a dropped sampling bit or a vanished glyph moves them.
    CHECK(redPixels > 350);
    CHECK(redPixels < 480);
    CHECK(solidRedPixels * 4 >= redPixels);

    // ...but as glyphs, not as solid blocks: the text box cannot be filled edge to
    // edge, which is what sampling the atlas is supposed to prevent.
    const float boxArea = measured.x * measured.y;
    CHECK(static_cast<float>(redPixels) < boxArea * 0.8f);

    // And they must be where the layout says they are.
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    CHECK(textFrame.redBounds(minX, minY, maxX, maxY, 1));
    std::printf("text bounds: (%d,%d)-(%d,%d), layout box (%.0f,%.0f)-(%.0f,%.0f)\n", minX, minY, maxX, maxY,
                static_cast<double>(textX), static_cast<double>(textY),
                static_cast<double>(textX + measured.x), static_cast<double>(textY + measured.y));
    CHECK(minX >= static_cast<int>(textX) - 2);
    CHECK(minY >= static_cast<int>(textY) - 2);
    CHECK(maxX <= static_cast<int>(textX + measured.x) + 2);
    CHECK(maxY <= static_cast<int>(textY + measured.y) + 2);

    // ---- frame 2b: the same text with letter spacing ------------------------
    // The bundled face fills its glyph cell, so tracking is the knob that gives
    // the letters room. It has to reach the pixels, not just the measurement.
    constexpr float Spacing = 2.0f;
    ctx.currentStyle().letterSpacing = Spacing;
    const ReZeroGui::Vector2 spacedMeasured = ctx.calculateTextSize(text);

    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.renderText(ReZeroGui::Vector2(textX, textY), text, ReZeroGui::Pixel(1.0f, 0.0f, 0.0f, 1.0f),
                   ReZeroGui::InvalidFont, 0.0f);
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame spacedFrame = readBack(device, context, swapChain);

    int spacedMinX = 0;
    int spacedMinY = 0;
    int spacedMaxX = 0;
    int spacedMaxY = 0;
    CHECK(spacedFrame.redBounds(spacedMinX, spacedMinY, spacedMaxX, spacedMaxY, 1));

    // 14 glyphs means 13 gaps: everything after the first glyph moves along, so
    // the first ink column stays put and the last one moves by 13 * spacing.
    const int expectedGrowth = static_cast<int>(Text.size() - 1) * static_cast<int>(Spacing);
    std::printf("spaced text bounds: (%d,%d)-(%d,%d), measured width %.1f -> %.1f\n", spacedMinX, spacedMinY,
                spacedMaxX, spacedMaxY, static_cast<double>(measured.x),
                static_cast<double>(spacedMeasured.x));

    CHECK(spacedMinX == minX);
    CHECK(spacedMaxX - maxX >= expectedGrowth - 1);
    CHECK(spacedMaxX - maxX <= expectedGrowth + 1);
    // Measuring grew by one spacing per glyph, and the ink still fits inside it.
    CHECK(spacedMeasured.x - measured.x > Spacing * static_cast<float>(Text.size()) - 0.01f);
    CHECK(spacedMeasured.x - measured.x < Spacing * static_cast<float>(Text.size()) + 0.01f);
    CHECK(spacedMaxX <= static_cast<int>(textX + spacedMeasured.x) + 2);
    ctx.currentStyle().letterSpacing = 0.0f;

    // ---- frame 3: solid geometry samples the white 1x1 texture ---------------
    const ReZeroGui::Rect box(300.0f, 40.0f, 60.0f, 40.0f);
    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.drawList().addRectFilled(box.minPoint(), box.maxPoint(), ReZeroGui::Pixel(0.0f, 1.0f, 0.0f, 1.0f));
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame rectFrame = readBack(device, context, swapChain);

    const std::size_t greenPixels = rectFrame.countGreenDominant(200);
    std::printf("rect: %zu green pixels for a %.0fx%.0f box\n", greenPixels, static_cast<double>(box.width),
                static_cast<double>(box.height));
    // Untextured geometry has to come out as a flat, fully opaque fill.
    CHECK(greenPixels >= static_cast<std::size_t>(box.width * box.height) - 4);
    CHECK(greenPixels <= static_cast<std::size_t>(box.width * box.height) + 4);
    CHECK(rectFrame.countRedDominant(1) == 0);

    // ---- frame 4: a textured quad from a user supplied texture --------------
    // Four texels, two colours, uploaded through the UI texture queue.
    const std::uint8_t checker[16] = {0, 0, 255, 255, 255, 255, 0, 255, 255, 255, 0, 255, 0, 0, 255, 255};
    const ReZeroGui::TextureId checkerTexture = ctx.createTexture(2, 2, checker);
    CHECK(checkerTexture != ReZeroGui::InvalidTexture);

    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.drawList().addTexturedQuad(ReZeroGui::Vector2(100.0f, 20.0f), ReZeroGui::Vector2(140.0f, 60.0f),
                                     ReZeroGui::Vector2(0.0f, 0.0f), ReZeroGui::Vector2(1.0f, 1.0f),
                                     ReZeroGui::Pixel(1.0f, 1.0f, 1.0f, 1.0f), checkerTexture);
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame textured = readBack(device, context, swapChain);

    // The blue texels must have made it through, which they cannot if the handle
    // did not resolve to the uploaded texture.
    std::size_t bluePixels = 0;
    for (std::uint32_t y = 0; y < textured.height; ++y) {
        for (std::uint32_t x = 0; x < textured.width; ++x) {
            const std::uint8_t *texel = textured.at(x, y);
            if (texel[2] > 128 && texel[2] > texel[0] && texel[2] > texel[1]) {
                bluePixels += 1;
            }
        }
    }
    std::printf("textured quad: %zu blue pixels\n", bluePixels);
    CHECK(bluePixels > 100);

    // ---- frame 5: widgets --------------------------------------------------
    // The same path a host uses: a window, a checkbox and a button, driven by the
    // Ui facade. This is what proves the widgets reach the screen rather than
    // just the draw list.
    ReZeroGui::Ui ui(ctx);
    auto noop = [] {};
    ReZeroGui::Rect checkboxRect;
    ReZeroGui::Rect buttonRect;

    auto declareWindow = [&] {
        ReZeroGui::WindowScope window(ctx, (char *)"Widgets");
        window.position(0.0f, 0.0f).size(240.0f, 150.0f);
        if (window.isVisible()) {
            bool option = true;
            ReZeroGui::CheckboxBuilder checkbox = ui.checkbox("Option");
            checkbox.checked(&option);
            checkboxRect = checkbox.rect();
            ReZeroGui::ButtonBuilder button = ui.button("Press");
            button.onClick(noop);
            buttonRect = button.rect();
        }
    };

    // One frame to settle the chained position/size: the window chrome is emitted
    // before those options arrive, so it only moves on the following frame.
    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    declareWindow();
    ctx.endFrame();

    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    declareWindow();
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame widgetFrame = readBack(device, context, swapChain);

    CHECK(checkboxRect.width > 0.0f);
    CHECK(buttonRect.width > 0.0f);
    CHECK(buttonRect.y > checkboxRect.maxY() - 1.0f);
    std::printf("widgets: checkbox (%.0f,%.0f)-(%.0f,%.0f) button (%.0f,%.0f)-(%.0f,%.0f)\n",
                static_cast<double>(checkboxRect.x), static_cast<double>(checkboxRect.y),
                static_cast<double>(checkboxRect.maxX()), static_cast<double>(checkboxRect.maxY()),
                static_cast<double>(buttonRect.x), static_cast<double>(buttonRect.y),
                static_cast<double>(buttonRect.maxX()), static_cast<double>(buttonRect.maxY()));

    // Inside the button, clear of its border and of the centred label: the button
    // fill colour is 0x2F6FB2, i.e. strongly blue. This only holds if the widget
    // was both laid out and clipped against the window's final rectangle.
    const auto sampleX = static_cast<std::uint32_t>(buttonRect.x + 5.0f);
    const auto sampleY = static_cast<std::uint32_t>(buttonRect.center().y);
    const std::uint8_t *buttonPixel = widgetFrame.at(sampleX, sampleY);
    std::printf("button pixel: %u %u %u\n", buttonPixel[0], buttonPixel[1], buttonPixel[2]);
    CHECK(buttonPixel[2] > 120);
    CHECK(buttonPixel[2] > buttonPixel[1]);
    CHECK(buttonPixel[1] > buttonPixel[0]);

    // The checked box has to show a tick, which is much brighter than the window
    // background and its border.
    bool tickFound = false;
    for (std::uint32_t y = static_cast<std::uint32_t>(checkboxRect.y);
         y < static_cast<std::uint32_t>(checkboxRect.maxY()) && y < widgetFrame.height; ++y) {
        for (std::uint32_t x = static_cast<std::uint32_t>(checkboxRect.x);
             x < static_cast<std::uint32_t>(checkboxRect.maxX()) && x < widgetFrame.width; ++x) {
            const std::uint8_t *texel = widgetFrame.at(x, y);
            if (texel[2] > 180 && texel[1] > 150) {
                tickFound = true;
            }
        }
    }
    CHECK(tickFound);

    // The window body sits on the dark background: a point inside the window but
    // past the widgets must show the window background rather than the clear colour
    // or the title bar.
    const auto darkX = static_cast<std::uint32_t>(120.0f);
    const auto darkY = static_cast<std::uint32_t>(145.0f);
    const std::uint8_t *bodyPixel = widgetFrame.at(darkX, darkY);
    std::printf("window body pixel: %u %u %u\n", bodyPixel[0], bodyPixel[1], bodyPixel[2]);
    CHECK(bodyPixel[0] > 8);
    CHECK(bodyPixel[0] < 80);
    CHECK(bodyPixel[1] < 80);
    CHECK(bodyPixel[2] < 90);

    // ---- frame 6: an anti-aliased filled circle ------------------------------
    // The rim has to fade over about a pixel. A hard edged polygon only ever shows
    // fully covered or fully uncovered pixels, which is what "the circles look
    // rough" looks like on screen, so the partial pixels are the whole point here.
    const ReZeroGui::Vector2 circleCenter(200.0f, 150.0f);
    constexpr float CircleRadius = 16.0f;
    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.drawList().addCircleFilled(circleCenter, CircleRadius, ReZeroGui::Pixel(1.0f, 0.0f, 0.0f, 1.0f));
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame circleFrame = readBack(device, context, swapChain);

    const auto circleX = static_cast<std::uint32_t>(circleCenter.x);
    const auto circleY = static_cast<std::uint32_t>(circleCenter.y);
    const std::uint8_t *corePixel = circleFrame.at(circleX + 8, circleY);
    std::size_t coveredPixels = 0;
    std::size_t partialPixels = 0;
    const int reach = static_cast<int>(CircleRadius) + 4;
    for (int y = static_cast<int>(circleCenter.y) - reach; y <= static_cast<int>(circleCenter.y) + reach; ++y) {
        for (int x = static_cast<int>(circleCenter.x) - reach; x <= static_cast<int>(circleCenter.x) + reach; ++x) {
            const std::uint8_t *texel = circleFrame.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            if (texel[0] > 127) {
                coveredPixels += 1;
            }
            if (texel[0] > 8 && texel[0] < 247) {
                partialPixels += 1;
            }
        }
    }
    std::printf("circle: %zu covered pixels (area %.0f), %zu partial rim pixels\n", coveredPixels,
                3.14159265358979 * static_cast<double>(CircleRadius * CircleRadius), partialPixels);
    // The core is plain opaque red: the feather must not bleed inwards.
    CHECK(corePixel[0] >= 254);
    CHECK(corePixel[1] == 0);
    CHECK(corePixel[2] == 0);
    // Roughly the true area, so the polygon is not cutting the circle short.
    CHECK(coveredPixels > 700);
    CHECK(coveredPixels < 900);
    // ... and a faded ring of partial coverage all the way around it.
    CHECK(partialPixels > 40);
    CHECK(partialPixels < 260);

    // ---- frame 7: an anti-aliased rounded rectangle --------------------------
    // The corners and the straight edges both have to fade over a pixel. A hard edged
    // fill only ever produces fully covered or fully uncovered pixels, so the partial
    // count is what separates a curve from a staircase.
    const ReZeroGui::Rect rounded(320.0f, 120.0f, 120.0f, 70.0f);
    constexpr float RoundedRadius = 16.0f;
    clearBackBuffer(device, context, swapChain);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.drawList().addRectFilled(rounded.minPoint(), rounded.maxPoint(), ReZeroGui::Pixel(1.0f, 0.0f, 0.0f, 1.0f),
                                 RoundedRadius);
    ctx.endFrame();
    renderer.renderDrawData(ctx.drawItems());
    const Frame roundedFrame = readBack(device, context, swapChain);

    const std::uint8_t *roundedCore =
        roundedFrame.at(static_cast<std::uint32_t>(rounded.center().x), static_cast<std::uint32_t>(rounded.center().y));
    std::size_t roundedCovered = 0;
    std::size_t roundedPartial = 0;
    for (std::uint32_t y = 0; y < roundedFrame.height; ++y) {
        for (std::uint32_t x = 0; x < roundedFrame.width; ++x) {
            const std::uint8_t *texel = roundedFrame.at(x, y);
            if (texel[0] > 127) {
                roundedCovered += 1;
            }
            if (texel[0] > 8 && texel[0] < 247) {
                roundedPartial += 1;
            }
        }
    }
    // A rectangle with corner radius r covers w * h - (4 - pi) * r * r.
    const double roundedArea = 120.0 * 70.0 - (4.0 - 3.14159265358979) * 16.0 * 16.0;
    std::printf("rounded rect: %zu covered pixels (area %.0f), %zu partial edge pixels\n", roundedCovered, roundedArea,
                roundedPartial);
    CHECK(roundedCore[0] >= 254);
    CHECK(roundedCore[1] == 0);
    CHECK(roundedCore[2] == 0);
    CHECK(roundedCovered > static_cast<std::size_t>(roundedArea) - 100);
    CHECK(roundedCovered < static_cast<std::size_t>(roundedArea) + 100);
    CHECK(roundedPartial > 150);
    CHECK(roundedPartial < 700);

    // The top right corner on its own: the arc there has to carry the fade, not just
    // the straight edges.
    std::size_t cornerPartial = 0;
    for (int y = 120; y < 120 + static_cast<int>(RoundedRadius); ++y) {
        for (int x = 440 - static_cast<int>(RoundedRadius); x < 440; ++x) {
            const std::uint8_t *texel =
                roundedFrame.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            if (texel[0] > 8 && texel[0] < 247) {
                cornerPartial += 1;
            }
        }
    }
    std::printf("rounded rect corner: %zu partial pixels\n", cornerPartial);
    CHECK(cornerPartial > 8);

    // ---- shutdown -----------------------------------------------------------
    renderer.shutdown();
    release(swapChain);
    release(context);
    release(device);
    if (window != nullptr) {
        ::DestroyWindow(window);
    }
    ::UnregisterClassW(WindowClass, ::GetModuleHandleW(nullptr));

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
