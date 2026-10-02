# The tests are plain executables: they print their own checks and return a non
# zero exit code on failure, so they need no test framework.
add_executable(ReZeroGuiTextRenderingTest testTextRendering.cpp)
target_link_libraries(ReZeroGuiTextRenderingTest PRIVATE ReZero::ReZeroGui)
add_test(NAME ReZeroGui.TextRendering COMMAND ReZeroGuiTextRenderingTest)

add_executable(ReZeroGuiWidgetTest testWidgets.cpp)
target_link_libraries(ReZeroGuiWidgetTest PRIVATE ReZero::ReZeroGui)
add_test(NAME ReZeroGui.Widgets COMMAND ReZeroGuiWidgetTest)

# The Direct3D test renders offscreen and reads the frame back, so it needs a
# device: keep it to platforms that have one.
if(WIN32)
    add_executable(ReZeroGuiDx11TextRenderingTest testDx11TextRendering.cpp)
    target_link_libraries(ReZeroGuiDx11TextRenderingTest PRIVATE
        ReZero::ReZeroGui
        d3d11.lib
        dxgi.lib
        d3dcompiler.lib)
    add_test(NAME ReZeroGui.Dx11TextRendering COMMAND ReZeroGuiDx11TextRenderingTest)
endif()
