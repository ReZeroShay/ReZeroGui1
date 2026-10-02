// Drives every widget through simulated frames: mouse press/release, drags and
// typed characters, checking the values the host owns and the geometry that
// reaches the draw list.
//
// The frames are fed exactly the way the Win32 layer feeds them, so the tests
// cover the widget behaviour and the input plumbing together.
#include <ReZeroGui/ReZeroGui.h>
#include <ReZeroGui/Ui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    using ReZeroGui::Id;
    using ReZeroGui::Input;
    using ReZeroGui::Rect;
    using ReZeroGui::Ui;
    using ReZeroGui::Vector2;
    using ReZeroGui::WindowScope;

    /// One frame with a fixed size window open, so widgets have a layout region
    /// and a clip rectangle to be placed in.
    template <class Body> void runFrame(Context &ctx, Body &&body) {
        ctx.beginFrame(1.0f / 60.0f);
        {
            WindowScope window(ctx, (char *)"Test");
            window.position(0.0f, 0.0f).size(400.0f, 300.0f);
            if (window.isVisible()) {
                body();
            }
        }
        ctx.endFrame();
    }

    /// Same, with the left mouse button held (or not) at `point` when the frame
    /// starts. Callers press and release across two frames to make a click.
    template <class Body> void runFrameAt(Context &ctx, Vector2 point, bool down, Body &&body) {
        ctx.io().setMousePosition(point);
        ctx.io().setMouseButtonDown(Input::MouseButton::Left, down);
        runFrame(ctx, body);
    }

    /// One frame with a key pressed; the key is released before returning so the
    /// next frame does not see it as held.
    template <class Body> void runFrameWithKey(Context &ctx, Input::KeyCode key, Body &&body) {
        ctx.io().setKeyState(key, true);
        runFrame(ctx, body);
        ctx.io().setKeyState(key, false);
    }

    //===----------------------------------------------------------------------===//

    void testLabelParts() {
        CHECK(ReZeroGui::displayPartOf("Label##id") == std::string_view("Label"));
        CHECK(ReZeroGui::idPartOf("Label##id") == std::string_view("id"));

        // No marker: the label is both the visible text and the id.
        CHECK(ReZeroGui::displayPartOf("Plain") == std::string_view("Plain"));
        CHECK(ReZeroGui::idPartOf("Plain") == std::string_view("Plain"));

        // A bare marker falls back to the visible part instead of an empty id.
        CHECK(ReZeroGui::displayPartOf("Label##") == std::string_view("Label"));
        CHECK(ReZeroGui::idPartOf("Label##") == std::string_view("Label"));

        // Only the first marker splits, so ids may contain "##".
        CHECK(ReZeroGui::displayPartOf("A##B##C") == std::string_view("A"));
        CHECK(ReZeroGui::idPartOf("A##B##C") == std::string_view("B##C"));
    }

    void testText() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        Rect rect;
        runFrame(ctx, [&] {
            rect = ui.text("Hello").rect();
        });
        CHECK(rect.width > 0.0f);
        CHECK(rect.height > 0.0f);
        // Plain text draws glyphs and no frame.
        const auto *foreground = ctx.drawItems().foreground;
        CHECK(foreground != nullptr);
        if (foreground != nullptr) {
            CHECK(foreground->vertexCount() > 0);
        }

    }

    void testButton() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        int calls = 0;
        auto onClick = [&calls] { calls += 1; };

        bool hovered = false;
        bool held = false;
        bool clicked = false;
        bool disabled = false;
        Rect buttonRect;

        auto declare = [&] {
            ReZeroGui::ButtonBuilder button = ui.button("OK");
            button.onClick(onClick).disabled(disabled);
            // clicked() emits and runs the callback; the other queries reuse the
            // state of that same emit.
            clicked = button.clicked();
            hovered = button.hovered();
            held = button.pressed();
            buttonRect = button.rect();
        };

        runFrame(ctx, declare);
        const Rect rect = buttonRect;
        CHECK(rect.width > 0.0f);
        CHECK(rect.height > 0.0f);
        CHECK(rect.x >= 0.0f);
        CHECK(rect.maxX() <= 400.0f);
        CHECK(!clicked);
        CHECK(calls == 0);

        // Away from the button nothing happens. The release matters: leaving the
        // button held would swallow the click that is tested below.
        runFrameAt(ctx, Vector2(rect.maxX() + 40.0f, rect.center().y), true, declare);
        runFrameAt(ctx, Vector2(rect.maxX() + 40.0f, rect.center().y), false, declare);
        CHECK(!hovered);
        CHECK(!held);
        CHECK(!clicked);
        CHECK(calls == 0);

        // Pointing at it hovers, and pressing holds without clicking yet.
        runFrameAt(ctx, rect.center(), true, declare);
        CHECK(hovered);
        CHECK(held);
        CHECK(!clicked);
        CHECK(calls == 0);

        // Releasing on the button is the click.
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(hovered);
        CHECK(!held);
        CHECK(clicked);
        CHECK(calls == 1);

        runFrame(ctx, declare);
        CHECK(!clicked);
        CHECK(calls == 1);

        // A disabled button never reports anything, whatever the mouse does.
        disabled = true;
        runFrameAt(ctx, rect.center(), true, declare);
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(!clicked);
        CHECK(!hovered);
        CHECK(calls == 1);
        disabled = false;
    }

    void testCheckbox() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        bool flag = false;
        bool changed = false;

        Rect boxRect;

        auto declare = [&] {
            ReZeroGui::CheckboxBuilder box = ui.checkbox("Wireframe");
            box.checked(&flag);
            changed = box.changed();
            boxRect = box.rect();
        };

        runFrame(ctx, declare);
        const Rect rect = boxRect;
        CHECK(rect.width > 0.0f);
        CHECK(!flag);
        CHECK(!changed);

        runFrameAt(ctx, rect.center(), true, declare);
        CHECK(!flag);
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(flag);
        CHECK(changed);

        runFrame(ctx, declare);
        CHECK(flag);
        CHECK(!changed);

        runFrameAt(ctx, rect.center(), true, declare);
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(!flag);
        CHECK(changed);
    }

    void testRadioButtons() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        int choice = 0;
        Rect flat;
        Rect smooth;

        auto declare = [&] {
            ReZeroGui::RadioButtonBuilder flatOption = ui.radioButton("Flat");
            flatOption.selected(&choice).optionValue(0);
            flat = flatOption.rect();
            ReZeroGui::RadioButtonBuilder smoothOption = ui.radioButton("Smooth");
            smoothOption.selected(&choice).optionValue(1);
            smooth = smoothOption.rect();
        };

        runFrame(ctx, declare);
        CHECK(choice == 0);
        CHECK(flat.height > 0.0f);
        CHECK(smooth.height > 0.0f);
        // Vertical layout puts the second option below the first.
        CHECK(smooth.y >= flat.maxY());

        runFrameAt(ctx, smooth.center(), true, declare);
        runFrameAt(ctx, smooth.center(), false, declare);
        CHECK(choice == 1);

        // Clicking the already selected option keeps the value.
        runFrameAt(ctx, smooth.center(), true, declare);
        runFrameAt(ctx, smooth.center(), false, declare);
        CHECK(choice == 1);

        runFrameAt(ctx, flat.center(), true, declare);
        runFrameAt(ctx, flat.center(), false, declare);
        CHECK(choice == 0);
    }

    void testSliders() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        float fraction = 0.5f;
        int count = 4;
        Rect floatTrack;
        Rect intTrack;

        auto declare = [&] {
            ReZeroGui::FloatSliderBuilder floatSlider = ui.sliderFloat("Fraction");
            floatSlider.value(&fraction).range(0.0f, 1.0f);
            floatTrack = floatSlider.rect();
            ReZeroGui::IntSliderBuilder intSlider = ui.sliderInt("Count");
            intSlider.value(&count).range(1, 16);
            intTrack = intSlider.rect();
        };

        runFrame(ctx, declare);
        CHECK(floatTrack.height > 0.0f);
        CHECK(intTrack.height > 0.0f);
        // The label takes its own line, so the track starts below it.
        CHECK(floatTrack.y >= 0.0f);
        CHECK(intTrack.y >= floatTrack.maxY());
        CHECK(fraction == 0.5f);

        // Pressing just inside the right end of the track clamps to the maximum.
        // (Rect::contains is half open, so the exact edge pixel is outside.)
        const Vector2 farRight(floatTrack.maxX() - 1.0f, floatTrack.center().y);
        const Vector2 farLeft(floatTrack.x + 1.0f, floatTrack.center().y);

        runFrameAt(ctx, farRight, true, declare);
        CHECK(fraction == 1.0f);
        runFrameAt(ctx, farRight, false, declare);
        CHECK(fraction == 1.0f);

        // ... and the left end to the minimum.
        runFrameAt(ctx, farLeft, true, declare);
        CHECK(fraction == 0.0f);

        // Drag back to the middle while held: the grab follows the mouse.
        runFrameAt(ctx, Vector2(floatTrack.center().x, floatTrack.center().y), true, declare);
        CHECK(fraction > 0.3f);
        CHECK(fraction < 0.7f);
        runFrameAt(ctx, Vector2(floatTrack.center().x, floatTrack.center().y), false, declare);

        // The integer slider rounds and clamps to its own range.
        runFrameAt(ctx, Vector2(intTrack.maxX() - 1.0f, intTrack.center().y), true, declare);
        CHECK(count == 16);
        runFrameAt(ctx, Vector2(intTrack.x + 1.0f, intTrack.center().y), true, declare);
        CHECK(count == 1);
        runFrameAt(ctx, Vector2(intTrack.x + 1.0f, intTrack.center().y), false, declare);
        CHECK(count == 1);
    }

    void testTextInput() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        char name[16] = {};
        std::size_t cursor = 0;
        bool focused = false;
        Rect fieldRect;

        auto declare = [&] {
            ReZeroGui::TextInputBuilder field = ui.inputText("Name");
            field.buffer(name).hint("type here");
            field.changed(); // emits, so the queries below read back this widget
            fieldRect = field.rect();
            focused = field.focused();
        };

        runFrame(ctx, declare);
        const Rect rect = fieldRect;
        CHECK(rect.width > 0.0f);
        CHECK(!focused);
        CHECK(name[0] == '\0');

        // Clicking focuses the field and drops the caret.
        runFrameAt(ctx, rect.center(), true, declare);
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(focused);

        // Typed characters arrive one at a time, like WM_CHAR does.
        ctx.io().addCharacter(U'H');
        runFrame(ctx, declare);
        ctx.io().addCharacter(U'i');
        runFrame(ctx, declare);
        CHECK(focused);
        CHECK(std::strcmp(name, "Hi") == 0);
        cursor = ctx.caretPosition();
        CHECK(cursor == 2);

        // The field keeps focus without the mouse, and keeps consuming keys.
        runFrame(ctx, declare);
        CHECK(focused);

        // Backspace deletes the character before the caret.
        runFrameWithKey(ctx, Input::KeyCode::Backspace, declare);
        CHECK(std::strcmp(name, "H") == 0);
        CHECK(ctx.caretPosition() == 1);

        // Left arrow then typing inserts in the middle.
        runFrameWithKey(ctx, Input::KeyCode::LeftArrow, declare);
        CHECK(ctx.caretPosition() == 0);
        ctx.io().addCharacter(U'X');
        runFrame(ctx, declare);
        CHECK(std::strcmp(name, "XH") == 0);

        // Home/End move the caret to the edges, Delete eats forward.
        runFrameWithKey(ctx, Input::KeyCode::End, declare);
        CHECK(ctx.caretPosition() == 2);
        runFrameWithKey(ctx, Input::KeyCode::Home, declare);
        CHECK(ctx.caretPosition() == 0);
        runFrameWithKey(ctx, Input::KeyCode::Delete, declare);
        CHECK(std::strcmp(name, "H") == 0);

        // Enter ends the edit.
        runFrameWithKey(ctx, Input::KeyCode::Enter, declare);
        CHECK(!focused);

        // A key that does nothing while unfocused must not edit.
        runFrameWithKey(ctx, Input::KeyCode::Backspace, declare);
        CHECK(std::strcmp(name, "H") == 0);

        // Multi byte input stays whole: deleting steps over the whole code point.
        char wide[16] = {};
        Rect wideRect;
        runFrame(ctx, [&] {
            ReZeroGui::TextInputBuilder field = ui.inputText("Wide");
            field.buffer(wide);
            wideRect = field.rect();
        });
        runFrameAt(ctx, wideRect.center(), true, [&] {
            ReZeroGui::TextInputBuilder field = ui.inputText("Wide");
            field.buffer(wide);
        });
        ctx.io().addCharacter(U'\u00E9'); // e-acute, two bytes
        runFrame(ctx, [&] {
            ReZeroGui::TextInputBuilder field = ui.inputText("Wide");
            field.buffer(wide);
        });
        CHECK(std::strlen(wide) == 2);
        runFrameWithKey(ctx, Input::KeyCode::Backspace, [&] {
            ReZeroGui::TextInputBuilder field = ui.inputText("Wide");
            field.buffer(wide);
        });
        CHECK(std::strlen(wide) == 0);
    }

    void testProgressBar() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        std::size_t vertices = 0;

        Rect progressRect;
        runFrame(ctx, [&] {
            ReZeroGui::ProgressBarBuilder bar = ui.progressBar("Loading");
            bar.fraction(0.25f).overlay("25%");
            progressRect = bar.rect();
        });
        const auto *foreground = ctx.drawItems().foreground;
        CHECK(foreground != nullptr);
        if (foreground != nullptr) {
            vertices = foreground->vertexCount();
        }
        CHECK(vertices > 0);
        const Rect rect = progressRect;
        CHECK(rect.width > 0.0f);
        CHECK(rect.height > 0.0f);

        // The fraction is clamped and the bar still draws at the extremes.
        const auto *afterFull = ctx.drawItems().foreground;
        runFrame(ctx, [&] { ui.progressBar().fraction(2.0f); });
        CHECK(afterFull != nullptr);
        if (afterFull != nullptr) {
            CHECK(afterFull->vertexCount() > 0);
        }

        runFrame(ctx, [&] {
            ReZeroGui::ProgressBarBuilder bar = ui.progressBar();
            bar.fraction(-1.0f);
            CHECK(bar.rect().width > 0.0f);
        });
    }

    /// A button can stretch over the whole available width with its label centred -
    /// the usual shape for a stacked OK / Cancel pair.
    void testButtonFillWidth() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        // Outside the window, so nothing is hovered and the label keeps its resting
        // colour.
        const Vector2 away(560.0f, 440.0f);
        const ReZeroGui::Pixel inkColor = ctx.currentStyle().color(ReZeroGui::ColorIndex::Text);

        Rect rect;
        float available = 0.0f;
        float inkMinimumX = 0.0f;
        float inkMaximumX = 0.0f;

        // Horizontal extent of the label's ink. Reading the draw list is how the test
        // checks where the text actually landed instead of trusting the layout maths.
        // The scan is limited to the button's row: the window chrome is drawn with the
        // same colour.
        auto measureInk = [&] {
            const ReZeroGui::Pixel32 wanted = inkColor.toPixel32();
            bool seen = false;
            inkMinimumX = 0.0f;
            inkMaximumX = 0.0f;
            for (const ReZeroGui::DrawVertex &vertex : ctx.drawList().currentVertices()) {
                if (vertex.pixel != wanted || vertex.position.y < rect.y || vertex.position.y > rect.maxY()) {
                    continue;
                }
                inkMinimumX = seen ? std::min(inkMinimumX, vertex.position.x) : vertex.position.x;
                inkMaximumX = seen ? std::max(inkMaximumX, vertex.position.x) : vertex.position.x;
                seen = true;
            }
            CHECK(seen);
        };

        auto declareStretched = [&] {
            available = ui.contentRegionWidth();
            ReZeroGui::ButtonBuilder button = ui.button("OK");
            button.fillWidth();
            rect = button.rect();
        };

        // The window chrome is emitted from the rectangle the window had when it was
        // opened, so the first frame still shows the default size. Let it settle before
        // measuring ink, otherwise the title bar lands in the button's row.
        runFrameAt(ctx, away, false, declareStretched);

        // Full width plus the default centring.
        runFrameAt(ctx, away, false, [&] {
            declareStretched();
            measureInk();
        });
        CHECK(available > 300.0f);
        CHECK(rect.width == available);
        CHECK((inkMinimumX + inkMaximumX) * 0.5f > rect.center().x - 4.0f);
        CHECK((inkMinimumX + inkMaximumX) * 0.5f < rect.center().x + 4.0f);

        // Left aligned inside the same stretched button.
        runFrameAt(ctx, away, false, [&] {
            ReZeroGui::ButtonBuilder button = ui.button("OK");
            button.fillWidth().align(Vector2(0.0f, 0.5f));
            rect = button.rect();
            measureInk();
        });
        // Left aligned still keeps the frame padding, so the label does not touch the
        // border.
        CHECK(inkMinimumX >= rect.x + ctx.currentStyle().framePadding.x - 0.5f);
        CHECK(inkMinimumX < rect.x + 20.0f);

        // An explicit width still wins, and a fixed height combines with the stretch.
        runFrameAt(ctx, away, false, [&] {
            ReZeroGui::ButtonBuilder button = ui.button("OK");
            button.fillWidth().width(100.0f);
            rect = button.rect();
        });
        CHECK(rect.width == 100.0f);

        runFrameAt(ctx, away, false, [&] {
            available = ui.contentRegionWidth();
            ReZeroGui::ButtonBuilder button = ui.button("OK");
            button.fillWidth().height(40.0f);
            rect = button.rect();
        });
        CHECK(rect.width == available);
        CHECK(rect.height == 40.0f);
    }
    /// Two windows must not share widget ids, otherwise the same label collides.
    void testWindowIdScoping() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        int callsA = 0;
        int callsB = 0;
        auto clickA = [&callsA] { callsA += 1; };
        auto clickB = [&callsB] { callsB += 1; };

        Id idA = ReZeroGui::InvalidId;
        Id idB = ReZeroGui::InvalidId;
        Rect rectA;
        Rect rectB;

        auto declare = [&] {
            {
                WindowScope a(ctx, (char *)"A");
                a.position(0.0f, 0.0f).size(200.0f, 120.0f);
                if (a.isVisible()) {
                    ReZeroGui::ButtonBuilder okA = ui.button("OK");
                    okA.onClick(clickA);
                    idA = okA.id();
                    rectA = okA.rect();
                }
            }
            {
                WindowScope b(ctx, (char *)"B");
                b.position(250.0f, 0.0f).size(200.0f, 120.0f);
                if (b.isVisible()) {
                    ReZeroGui::ButtonBuilder okB = ui.button("OK");
                    okB.onClick(clickB);
                    idB = okB.id();
                    rectB = okB.rect();
                }
            }
        };

        runFrame(ctx, declare);
        CHECK(idA != ReZeroGui::InvalidId);
        CHECK(idB != ReZeroGui::InvalidId);
        CHECK(idA != idB);
        CHECK(rectA.width > 0.0f);
        CHECK(rectB.width > 0.0f);
        CHECK(rectB.x >= rectA.maxX());

        // Clicking B must not reach A even though both are called "OK".
        runFrameAt(ctx, rectB.center(), true, declare);
        runFrameAt(ctx, rectB.center(), false, declare);
        CHECK(callsB == 1);
        CHECK(callsA == 0);
    }

    /// Widgets stack vertically, and the label of a framed widget takes its own
    /// line above the frame.
    void testLayoutFlow() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        Rect first;
        Rect second;
        Rect slider;
        Rect label;

        auto declare = [&] {
            ReZeroGui::ButtonBuilder one = ui.button("One");
            first = one.rect();
            ReZeroGui::ButtonBuilder two = ui.button("Two");
            second = two.rect();
            label = ui.text("Caption").rect();
            ReZeroGui::FloatSliderBuilder amount = ui.sliderFloat("Amount");
            amount.range(0.0f, 1.0f);
            slider = amount.rect();
        };

        runFrame(ctx, declare);

        CHECK(second.y >= first.maxY());
        CHECK(label.y >= second.maxY());
        CHECK(slider.y >= label.maxY());
        // Every item stays inside the window.
        CHECK(slider.maxX() <= 400.0f);

        // The slider track spans the content region rather than a single item.
        CHECK(slider.width > first.width);
    }

    /// Keyboard focus is not the mouse owner: clicking anywhere else must end the
    /// edit in a single click and leave every other widget interactive.
    void testTextFocusRelease() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        char name[16] = {};
        int clicks = 0;
        Rect fieldRect;
        Rect buttonRect;
        bool focused = false;

        auto declare = [&] {
            ReZeroGui::TextInputBuilder field = ui.inputText("Name");
            field.buffer(name);
            fieldRect = field.rect();
            focused = field.focused();

            ReZeroGui::ButtonBuilder button = ui.button("OK");
            button.onClick([&clicks] { clicks += 1; });
            buttonRect = button.rect();
        };

        runFrame(ctx, declare);
        CHECK(!focused);
        CHECK(fieldRect.width > 0.0f);
        CHECK(buttonRect.y >= fieldRect.maxY());

        // Clicking the field gives it the keyboard.
        runFrameAt(ctx, fieldRect.center(), true, declare);
        runFrameAt(ctx, fieldRect.center(), false, declare);
        CHECK(focused);
        CHECK(ctx.wantsKeyboardCapture());

        // One click on the button ends the edit and presses the button. While the
        // field owned the mouse nothing else could even be hovered, which is what
        // froze the whole UI.
        runFrameAt(ctx, buttonRect.center(), true, declare);
        CHECK(!focused);
        runFrameAt(ctx, buttonRect.center(), false, declare);
        CHECK(clicks == 1);
        CHECK(!ctx.wantsKeyboardCapture());

        // A click on empty space ends the edit too.
        runFrameAt(ctx, fieldRect.center(), true, declare);
        runFrameAt(ctx, fieldRect.center(), false, declare);
        CHECK(focused);
        const Vector2 empty(300.0f, 200.0f);
        runFrameAt(ctx, empty, true, declare);
        runFrameAt(ctx, empty, false, declare);
        CHECK(!focused);

        // A field that stops being declared loses the keyboard instead of silently
        // owning it again the next time it appears.
        runFrameAt(ctx, fieldRect.center(), true, declare);
        runFrameAt(ctx, fieldRect.center(), false, declare);
        CHECK(focused);
        runFrame(ctx, [&] {
            ReZeroGui::ButtonBuilder button = ui.button("OK");
            button.onClick([&clicks] { clicks += 1; });
        });
        runFrame(ctx, declare);
        CHECK(!focused);
    }

    /// A switch animates its knob through a context owned value, and that value
    /// follows the widget's lifetime: a switch that is not declared for a frame comes
    /// back settled instead of replaying the slide.
    void testSwitchToggle() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        bool flag = false;
        bool changed = false;
        float slide = 0.0f;
        Rect rect;

        auto declare = [&] {
            ReZeroGui::SwitchToggleBuilder toggle = ui.switchToggle("Wireframe");
            toggle.checked(&flag);
            changed = toggle.changed();
            slide = toggle.slide();
            rect = toggle.rect();
        };

        // A switch that appears for the first time is settled, not mid slide.
        runFrame(ctx, declare);
        CHECK(!flag);
        CHECK(slide == 0.0f);
        CHECK(rect.width > 0.0f);
        CHECK(rect.height > 0.0f);

        // Clicking flips the value and starts the knob moving. The click lands on
        // release, which is also the frame the slide starts from.
        runFrameAt(ctx, rect.center(), true, declare);
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(flag);
        CHECK(changed);
        CHECK(slide > 0.0f);
        CHECK(slide < 1.0f);
        const float firstStep = slide;

        runFrame(ctx, declare);
        CHECK(slide > firstStep);

        for (int frame = 0; frame < 120; frame += 1) {
            runFrame(ctx, declare);
        }
        CHECK(slide == 1.0f);

        // ... and back again.
        runFrameAt(ctx, rect.center(), true, declare);
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(!flag);
        CHECK(slide < 1.0f);
        for (int frame = 0; frame < 120; frame += 1) {
            runFrame(ctx, declare);
        }
        CHECK(slide == 0.0f);

        // Half way through a slide, one frame without the switch drops its animation
        // value: it is born settled instead of resuming from the middle.
        runFrameAt(ctx, rect.center(), true, declare);
        runFrameAt(ctx, rect.center(), false, declare);
        CHECK(slide < 1.0f);
        runFrame(ctx, [&] { ui.text("Nothing"); });
        runFrame(ctx, declare);
        CHECK(slide == 1.0f);

        // A disabled switch ignores clicks.
        auto declareDisabled = [&] {
            ReZeroGui::SwitchToggleBuilder toggle = ui.switchToggle("Wireframe");
            toggle.checked(&flag).disabled(true);
            changed = toggle.changed();
            slide = toggle.slide();
        };
        runFrameAt(ctx, rect.center(), true, declareDisabled);
        runFrameAt(ctx, rect.center(), false, declareDisabled);
        CHECK(flag);
        CHECK(!changed);
    }

    /// Horizontal extent of the geometry drawn in `color` during the current frame.
    /// Reading the draw list is how a test checks where a widget actually landed
    /// instead of trusting the rectangle it reports.
    void drawnExtent(Context &ctx, ReZeroGui::Pixel color, float &minimumX, float &maximumX) {
        const ReZeroGui::Pixel32 wanted = color.toPixel32();
        bool seen = false;
        minimumX = 0.0f;
        maximumX = 0.0f;
        for (const ReZeroGui::DrawVertex &vertex : ctx.drawList().currentVertices()) {
            if (vertex.pixel != wanted) {
                continue;
            }
            if (!seen) {
                minimumX = vertex.position.x;
                maximumX = vertex.position.x;
                seen = true;
                continue;
            }
            minimumX = std::min(minimumX, vertex.position.x);
            maximumX = std::max(maximumX, vertex.position.x);
        }
    }

    /// The switch lays its row out three ways: switch first (the default), a wider
    /// gap, and label first with the switch pushed to the right edge - capped by
    /// maxWidth - while the stretched row stays clickable.
    void testSwitchLayout() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        bool flag = false;
        Rect rect;
        float trackMinimumX = 0.0f;
        float trackMaximumX = 0.0f;

        // The mouse stays outside the window, so the track is drawn in its resting
        // color and nothing else in the frame uses that color.
        const Vector2 away(560.0f, 440.0f);
        const ReZeroGui::Pixel resting = ctx.currentStyle().color(ReZeroGui::ColorIndex::FrameBackground);

        auto declare = [&](float spacing, float maxWidth, bool labelFirst) {
            ReZeroGui::SwitchToggleBuilder toggle = ui.switchToggle("Wireframe");
            toggle.checked(&flag).spacing(spacing).maxWidth(maxWidth).labelFirst(labelFirst);
            rect = toggle.rect();
        };
        auto layout = [&](float spacing, float maxWidth, bool labelFirst) {
            runFrameAt(ctx, away, false, [&] { declare(spacing, maxWidth, labelFirst); });
        };

        // Default: the switch leads and the row is only as wide as its contents.
        layout(-1.0f, 0.0f, false);
        const Rect defaultRect = rect;
        drawnExtent(ctx, resting, trackMinimumX, trackMaximumX);
        CHECK(trackMinimumX == defaultRect.x);

        const float trackWidth = ctx.currentStyle().fontSize * 1.8f;
        const float labelWidth = ui.calcTextSize("Wireframe").x;
        const float defaultGap = ctx.currentStyle().itemInnerSpacing.x;
        CHECK(defaultRect.width > trackWidth + defaultGap + labelWidth - 0.5f);
        CHECK(defaultRect.width < trackWidth + defaultGap + labelWidth + 0.5f);

        // A wider gap widens the row by exactly that much.
        layout(40.0f, 0.0f, false);
        CHECK(rect.width > trackWidth + 40.0f + labelWidth - 0.5f);
        CHECK(rect.width < trackWidth + 40.0f + labelWidth + 0.5f);

        // Label first: the row stretches over the content region and the switch owns
        // its right edge, a whole label away from the text.
        layout(-1.0f, 0.0f, true);
        const Rect labelFirstRect = rect;
        drawnExtent(ctx, resting, trackMinimumX, trackMaximumX);
        CHECK(labelFirstRect.width > defaultRect.width);
        CHECK(trackMaximumX > labelFirstRect.maxX() - 0.5f);
        CHECK(trackMinimumX > labelFirstRect.x + labelWidth);

        // maxWidth caps the stretch and keeps the switch on the right edge.
        layout(-1.0f, 150.0f, true);
        const Rect cappedRect = rect;
        CHECK(cappedRect.width == 150.0f);
        drawnExtent(ctx, resting, trackMinimumX, trackMaximumX);
        CHECK(trackMaximumX > cappedRect.maxX() - 0.5f);
        CHECK(trackMinimumX > cappedRect.x + labelWidth);

        // The stretched row is clickable in the gap between label and switch, not
        // just on the switch itself.
        CHECK(!flag);
        auto clickGap = [&] { declare(-1.0f, 150.0f, true); };
        const Vector2 gap(cappedRect.x + cappedRect.width * 0.55f, cappedRect.center().y);
        CHECK(gap.x < trackMinimumX);
        runFrameAt(ctx, gap, true, clickGap);
        runFrameAt(ctx, gap, false, clickGap);
        CHECK(flag);
    }

    /// Separators: a plain rule spans the content region, and the labelled one keeps
    /// the text at the left edge with the rule starting clear of it.
    void testSeparators() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        Rect separatorRect;
        Rect headerRect;
        Rect below;
        float lineMinimumX = 0.0f;
        float lineMaximumX = 0.0f;

        // The mouse stays outside the window: nothing is hovered and the window
        // chrome does not use the separator colour, so the scan sees rules only.
        const Vector2 away(560.0f, 440.0f);
        const ReZeroGui::Pixel rule = ctx.currentStyle().color(ReZeroGui::ColorIndex::Separator);

        runFrameAt(ctx, away, false, [&] {
            ReZeroGui::SeparatorBuilder separator = ui.separator();
            separatorRect = separator.rect();
            below = ui.text("below").rect();
        });
        drawnExtent(ctx, rule, lineMinimumX, lineMaximumX);
        CHECK(separatorRect.height == 1.0f);
        CHECK(separatorRect.width > 300.0f);
        CHECK(lineMinimumX == separatorRect.x);
        CHECK(lineMaximumX == separatorRect.maxX());
        // It owns a row, so the next widget starts below it.
        CHECK(below.y >= separatorRect.maxY());

        runFrameAt(ctx, away, false, [&] {
            ReZeroGui::SeparatorTextBuilder header = ui.separatorText("Section");
            headerRect = header.rect();
            below = ui.text("below").rect();
        });
        const float labelWidth = ui.calcTextSize("Section").x;
        drawnExtent(ctx, rule, lineMinimumX, lineMaximumX);
        CHECK(headerRect.height > 1.0f);
        CHECK(headerRect.width > 300.0f);
        CHECK(below.y >= headerRect.maxY());

        // Both rules reach a row edge, and the label sits in the gap between them,
        // left of centre. The gap is measured from the drawn vertices rather than
        // trusted, so this cannot pass while a rule runs under the label.
        std::vector<float> ruleColumns;
        for (const ReZeroGui::DrawVertex &vertex : ctx.drawList().currentVertices()) {
            if (vertex.pixel == rule.toPixel32()) {
                ruleColumns.push_back(vertex.position.x);
            }
        }
        std::sort(ruleColumns.begin(), ruleColumns.end());
        std::vector<float> columnEdges;
        for (const float column : ruleColumns) {
            if (columnEdges.empty() || column - columnEdges.back() > 0.5f) {
                columnEdges.push_back(column);
            }
        }
        // Four distinct edges means exactly two rules, one on each side of the label.
        CHECK(columnEdges.size() == 4);
        float gapStart = headerRect.x;
        float gapEnd = headerRect.x;
        if (columnEdges.size() >= 4) {
            gapStart = columnEdges[1];
            gapEnd = columnEdges[2];
        }
        CHECK(lineMinimumX == headerRect.x);
        CHECK(lineMaximumX == headerRect.maxX());
        const float expectedGap = labelWidth + 2.0f * ctx.currentStyle().itemInnerSpacing.x;
        CHECK(gapEnd - gapStart > expectedGap - 0.5f);
        CHECK(gapEnd - gapStart < expectedGap + 0.5f);
        CHECK((gapStart + gapEnd) * 0.5f < headerRect.center().x);
        CHECK((gapStart + gapEnd) * 0.5f > headerRect.x + headerRect.width * 0.25f);

        // textPosition(0) pins the label to the left edge, which drops the leading
        // rule; textPosition(1) does the same on the other side.
        runFrameAt(ctx, away, false, [&] {
            ReZeroGui::SeparatorTextBuilder header = ui.separatorText("Section");
            header.textPosition(0.0f);
            headerRect = header.rect();
        });
        drawnExtent(ctx, rule, lineMinimumX, lineMaximumX);
        CHECK(lineMinimumX > headerRect.x + labelWidth);
        CHECK(lineMaximumX == headerRect.maxX());

        runFrameAt(ctx, away, false, [&] {
            ReZeroGui::SeparatorTextBuilder header = ui.separatorText("Section");
            header.textPosition(1.0f);
            headerRect = header.rect();
        });
        drawnExtent(ctx, rule, lineMinimumX, lineMaximumX);
        CHECK(lineMinimumX == headerRect.x);
        CHECK(lineMaximumX < headerRect.maxX() - labelWidth);
    }

    /// Slider options: a thin track that leaves the row height alone, a rectangle grab,
    /// and the value drawn on the label's line against its right edge.
    void testSliderOptions() {
        TestAllocator allocator;
        ReZeroGui::AllocatorView view(allocator);
        Context ctx(view);
        ctx.withDisplaySize(Vector2(640.0f, 480.0f));
        Ui ui(ctx);

        const Vector2 away(560.0f, 440.0f);
        const ReZeroGui::Style &style = ctx.currentStyle();
        const ReZeroGui::Pixel grabColor = style.color(ReZeroGui::ColorIndex::SliderGrab);
        const ReZeroGui::Pixel barColor = style.color(ReZeroGui::ColorIndex::ProgressBar);
        const ReZeroGui::Pixel inkColor = style.color(ReZeroGui::ColorIndex::Text);

        /// Limits of the geometry drawn in one colour inside a y band.
        struct Bounds {
            bool seen = false;
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
        };
        auto boundsOf = [&](ReZeroGui::Pixel color, float bandTop, float bandBottom) {
            Bounds bounds;
            const ReZeroGui::Pixel32 wanted = color.toPixel32();
            for (const ReZeroGui::DrawVertex &vertex : ctx.drawList().currentVertices()) {
                if (vertex.pixel != wanted || vertex.position.y < bandTop || vertex.position.y > bandBottom) {
                    continue;
                }
                if (!bounds.seen) {
                    bounds.minX = bounds.maxX = vertex.position.x;
                    bounds.minY = bounds.maxY = vertex.position.y;
                    bounds.seen = true;
                    continue;
                }
                bounds.minX = std::min(bounds.minX, vertex.position.x);
                bounds.maxX = std::max(bounds.maxX, vertex.position.x);
                bounds.minY = std::min(bounds.minY, vertex.position.y);
                bounds.maxY = std::max(bounds.maxY, vertex.position.y);
            }
            return bounds;
        };

        float value = 0.5f;
        Rect row;

        auto declare = [&](bool thin, float thickness, ReZeroGui::SliderGrabShape shape, bool valueOnLabel) {
            ReZeroGui::FloatSliderBuilder slider = ui.sliderFloat("Amount");
            slider.value(&value).range(0.0f, 1.0f).grabShape(shape).valueOnLabel(valueOnLabel);
            if (thin) {
                slider.thin(thickness);
            }
            row = slider.rect();
        };

        // The window chrome is emitted from the rectangle the window had when it was
        // opened, so let the first frame settle before scanning.
        runFrameAt(ctx, away, false, [&] { declare(false, 0.0f, ReZeroGui::SliderGrabShape::Circle, false); });
        const float defaultRowHeight = row.height;
        CHECK(defaultRowHeight > 15.0f);

        // Without thin() the bar fills the whole row.
        const Bounds defaultBar = boundsOf(barColor, row.y, row.maxY());
        CHECK(defaultBar.seen);
        CHECK(defaultBar.maxY - defaultBar.minY > 15.0f);

        // thin() slims the bar without touching the row height, so the value text still
        // has room and the drag target keeps its size.
        runFrameAt(ctx, away, false, [&] { declare(true, 4.0f, ReZeroGui::SliderGrabShape::Circle, false); });
        CHECK(row.height == defaultRowHeight);
        const Bounds thinBar = boundsOf(barColor, row.y, row.maxY());
        CHECK(thinBar.seen);
        CHECK(thinBar.maxY - thinBar.minY < 8.0f);
        CHECK(thinBar.maxY - thinBar.minY > 2.0f);

        // The grab is deliberately taller than the thin bar - that is the whole feel of
        // a slim slider.
        const Bounds thinGrab = boundsOf(grabColor, row.y - 4.0f, row.maxY() + 4.0f);
        CHECK(thinGrab.seen);
        CHECK(thinGrab.maxY - thinGrab.minY > 10.0f);
        CHECK(thinGrab.maxY - thinGrab.minY > thinBar.maxY - thinBar.minY + 4.0f);

        // The thickness is adjustable.
        runFrameAt(ctx, away, false, [&] { declare(true, 8.0f, ReZeroGui::SliderGrabShape::Circle, false); });
        const Bounds thickBar = boundsOf(barColor, row.y, row.maxY());
        CHECK(thickBar.seen);
        CHECK(thickBar.maxY - thickBar.minY > 6.0f);
        CHECK(thickBar.maxY - thickBar.minY < 12.0f);

        // A rectangle grab spans the bar's height, a round one does not.
        runFrameAt(ctx, away, false, [&] { declare(false, 0.0f, ReZeroGui::SliderGrabShape::Rectangle, false); });
        const Bounds rectGrab = boundsOf(grabColor, row.y, row.maxY());
        CHECK(rectGrab.seen);
        CHECK(rectGrab.maxY - rectGrab.minY > 15.0f);
        CHECK(rectGrab.maxX - rectGrab.minX > 10.0f);

        runFrameAt(ctx, away, false, [&] { declare(false, 0.0f, ReZeroGui::SliderGrabShape::Circle, false); });
        const Bounds roundGrab = boundsOf(grabColor, row.y, row.maxY());
        CHECK(roundGrab.seen);
        CHECK(roundGrab.maxY - roundGrab.minY < 15.0f);

        // By default the value sits inside the row, and the line above holds only the
        // label.
        const float labelWidth = ui.calcTextSize("Amount").x;
        const Bounds defaultRowInk = boundsOf(inkColor, row.y - 26.0f, row.y - 1.0f);
        const Bounds defaultTrackInk = boundsOf(inkColor, row.y, row.maxY());
        CHECK(defaultRowInk.seen);
        CHECK(defaultRowInk.maxX < row.x + labelWidth + 8.0f);
        CHECK(defaultTrackInk.seen);

        // valueOnLabel() moves the number to the right edge of that line.
        runFrameAt(ctx, away, false, [&] { declare(false, 0.0f, ReZeroGui::SliderGrabShape::Circle, true); });
        const Bounds valueRowInk = boundsOf(inkColor, row.y - 26.0f, row.y - 1.0f);
        const Bounds valueTrackInk = boundsOf(inkColor, row.y, row.maxY());
        CHECK(valueRowInk.seen);
        CHECK(valueRowInk.maxX > row.maxX() - 6.0f);
        CHECK(valueRowInk.minX < row.x + labelWidth + 8.0f);
        CHECK(!valueTrackInk.seen);
    }
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    testLabelParts();
    testText();
    testButton();
    testButtonFillWidth();
    testCheckbox();
    testRadioButtons();
    testSwitchToggle();
    testSwitchLayout();
    testSeparators();
    testSliders();
    testSliderOptions();
    testTextInput();
    testTextFocusRelease();
    testProgressBar();
    testWindowIdScoping();
    testLayoutFlow();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
