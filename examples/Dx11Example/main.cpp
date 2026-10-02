#define NOMINMAX
#include <chrono>
#include <cstdio>

#include <ReZeroGui/ReZeroGui.h>
#include <ReZeroGui/Ui.h>
#include <ReZeroGui/Renderer/Dx11Impl.h>
#include <ReZeroGui/Platform/win32Impl.h>
#include <wrl/client.h>

namespace {

    struct CAllocator {
      public:
        void *allocate(std::size_t size, [[maybe_unused]] std::size_t alignment) noexcept { return malloc(size); }

        void deallocate(void *ptr) noexcept { free(ptr); }
    };

    constexpr const wchar_t *windowClassName = L"ReZeroGuiDx11Example";

    auto declareDpiAwareness() noexcept {
        const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32 != nullptr) {
            using SetProcessDpiAwarenessContextFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
            const auto setContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                reinterpret_cast<void *>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
            if (setContext != nullptr) {
                // -4 == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2. Spelled out so the
                // helper builds against SDKs that predate the macro.
                const auto perMonitorV2 = static_cast<DPI_AWARENESS_CONTEXT>(reinterpret_cast<void *>(-4));
                if (setContext(perMonitorV2) != FALSE) {
                    return;
                }
            }
        }
        ::SetProcessDPIAware();
    }
    ReZeroGui::Context *uiContext{nullptr};
    ReZeroGui::Dx11Renderer *dx11renderer{nullptr};
    Microsoft::WRL::ComPtr<IDXGISwapChain> hostSwapChain;
    Microsoft::WRL::ComPtr<ID3D11Device> hostDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> hostDeviceContext;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> hostRenderTargetView;

} // namespace

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (uiContext != nullptr && ReZeroGui::Platform::handleMessage(*uiContext, message, wParam, lParam)) {
        return 0;
    }

    switch (message) {
    case WM_SIZE: {
        const auto width = static_cast<std::uint32_t>(LOWORD(lParam));
        const auto height = static_cast<std::uint32_t>(HIWORD(lParam));
        if (width != 0 && height != 0 && hostSwapChain != nullptr && dx11renderer != nullptr) {
            // Release all back buffer references first (backend RTV + host RTV),
            // then resize, then rebuild them lazily.
            dx11renderer->resize(width, height);
            hostRenderTargetView.Reset();
            hostSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
            if (SUCCEEDED(hostSwapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
                hostDevice->CreateRenderTargetView(backbuffer.Get(), nullptr, &hostRenderTargetView);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    default:
        return ::DefWindowProcW(window, message, wParam, lParam);
    }
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    declareDpiAwareness();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    // IDC_ARROW is an ANSI resource id, so use the ANSI entry point here.
    windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = windowClassName;
    if (::RegisterClassExW(&windowClass) == 0) {
        return 1;
    }

    HWND window = ::CreateWindowExW(0, windowClassName, L"ReZeroGui - Direct3D 11 example", WS_OVERLAPPEDWINDOW,
                                    CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return 1;
    }

    RECT clientRect{};
    ::GetClientRect(window, &clientRect);
    const auto width = static_cast<std::uint32_t>(clientRect.right - clientRect.left);
    const auto height = static_cast<std::uint32_t>(clientRect.bottom - clientRect.top);

    // The host owns the device and swap chain; the backend only borrows them.
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_10_0;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags, featureLevels,
                                       static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION, &device,
                                       &featureLevel, &deviceContext);
    if (FAILED(result) && (creationFlags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        // The debug layer is not installed: retry without it.
        creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags, featureLevels,
                                   static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION, &device,
                                   &featureLevel, &deviceContext);
    }
    if (FAILED(result)) {
        ::MessageBoxW(window, L"Failed to create a Direct3D 11 device.", L"ReZeroGui", MB_OK | MB_ICONERROR);
        return 1;
    }

    // The host owns the device and context; the backend only borrows them.
    hostDevice = device;
    hostDeviceContext = deviceContext;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    if (FAILED(device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        ::MessageBoxW(window, L"Failed to reach the DXGI factory.", L"ReZeroGui", MB_OK | MB_ICONERROR);
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = width;
    swapChainDesc.BufferDesc.Height = height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = window;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    if (FAILED(factory->CreateSwapChain(device.Get(), &swapChainDesc, &hostSwapChain))) {
        ::MessageBoxW(window, L"Failed to create the swap chain.", L"ReZeroGui", MB_OK | MB_ICONERROR);
        return 1;
    }

    ReZeroGui::RendererParams params;

    params.windowsHandle = window;
    params.deviceHandle = device.Get();
    params.swapChainHandle = hostSwapChain.Get();
    CAllocator cAllocator{};
    ReZeroGui::AllocatorView allocatorView(cAllocator);
    ReZeroGui::Dx11Renderer renderer(allocatorView);
    renderer.initialize(&params);
    dx11renderer = &renderer;

    ReZeroGui::Context ctx(allocatorView);
    ctx.withDisplaySize(ReZeroGui::Vector2(static_cast<float>(width), static_cast<float>(height)));

    {
        // Style is writable from here: sizes, colors and spacing are all read while
        // a frame is built. The bundled Proggy faces fill their glyph cell almost
        // completely, so a little tracking is what stops the letters from touching.
        // Set this to 0 to see the font's raw metrics.
        ReZeroGui::Style &style = ctx.currentStyle();
        style.letterSpacing = 1.0f;
    }

    // The window procedure forwards messages to the context, which is also where
    // WM_SIZE updates the display size used for layout.
    uiContext = &ctx;

    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
        if (SUCCEEDED(hostSwapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
            hostDevice->CreateRenderTargetView(backbuffer.Get(), nullptr, &hostRenderTargetView);
        }
    }

    ::ShowWindow(window, showCommand);
    ::UpdateWindow(window);

    auto previousTime = std::chrono::steady_clock::now();
    bool running = true;

    // The UI is immediate mode, so the host owns every widget's value.
    ReZeroGui::Ui ui(ctx);
    bool wireframe = true;
    bool showGrid = false;
    int shading = 1;
    float exposure = 0.5f;
    int samples = 8;
    char name[32] = "ReZero";
    float progress = 0.35f;
    int clickCount = 0;
    bool flag;
    while (running) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }

        const auto currentTime = std::chrono::steady_clock::now();
        const auto deltaTime = std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;

        // Hosts that render a 3D scene would do it here; the UI is blended on top
        // of whatever the back buffer already holds, so give it a defined base.
        // Clear colors are 0..1 floats, not 0..255.
        if (hostRenderTargetView != nullptr) {
            const float clearColor[4] = {0.09f, 0.09f, 0.12f, 1.0f};
            hostDeviceContext->ClearRenderTargetView(hostRenderTargetView.Get(), clearColor);
        }

        ctx.beginFrame(deltaTime);
        {
            // The glyph atlas is rasterized on the first frame. Say so once if it
            // failed, otherwise a missing font file just looks like a UI without
            // any labels.
            static bool reportedFont = false;
            if (!reportedFont) {
                reportedFont = true;
                if (!ctx.fonts().isBuilt()) {
                    std::fprintf(stderr, "ReZeroGui: no usable font found, text will not be drawn.\n");
                }
            }

            // A builder draws itself when the temporary dies, i.e. at the end of
            // the statement declaring it, so plain calls are enough for widgets
            // whose result is not needed.
            ReZeroGui::WindowScope panel(ctx, (char *)"ReZeroGui");
            panel.position(40.0f, 40.0f).width(280.0f);
            if (panel.isVisible()) {
                char counter[48];
                std::snprintf(counter, sizeof(counter), "clicks: %d", clickCount);
                ui.text(counter);

                if (ui.button("Click me").rounding(8.f).onClick([&clickCount] { clickCount += 1; }).clicked()) {
                    std::fprintf(stderr, "button clicked\n");
                }
                ui.switchToggle("Wireframe").checked(&flag);
                // ui.switchToggle("Wireframe").smoothing(0.05f);
                ui.checkbox("Wireframe1").checked(&wireframe);
                ui.checkbox("Show grid").checked(&showGrid).disabled(!wireframe);

                ui.text("Shading");
                ui.radioButton("Flat").selected(&shading).optionValue(0);
                ui.radioButton("Smooth").selected(&shading).optionValue(1);
                ui.separator();              // 横贯内容区的分隔线
                ui.separatorText("Section"); // 左侧文字 + 右侧延伸的线（小节标题
                ui.sliderFloat("Exposure").value(&exposure).range(0.0f, 2.0f);
                ui.sliderInt("Samples").value(&samples).range(1, 16);
                ui.sliderInt("Samples1").value(&samples).range(1, 16).grabShape(ReZeroGui::SliderGrabShape::Rectangle);
                ui.sliderInt("Samples12").value(&samples).range(1, 16).grabShape(ReZeroGui::SliderGrabShape::Rectangle).valueOnLabel()   ;
                ui.inputText("Name").buffer(name).hint("type a name");
                ui.progressBar("Loading").fraction(progress).overlay("35%");
            }
        }
        ctx.endFrame();

        dx11renderer->renderDrawData(ctx.drawItems());
        hostSwapChain->Present(1u, 0u);
    }

    uiContext = nullptr;
    dx11renderer->shutdown();
    return 0;
}