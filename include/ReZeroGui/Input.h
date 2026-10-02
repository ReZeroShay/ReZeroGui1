#pragma once

#include <cstddef>
#include <ReZeroGui/ReZeroLib.h>
#include <array>
#include <vector>
#include <span>
namespace ReZeroGui {
    struct Input {
        enum class MouseButton : std::uint8_t {
            Left = 0,
            Right = 1,
            Middle = 2,
            X1 = 3,
            X2 = 4,
            Count = 5,
        };
        static constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

        enum class KeyCode : std::uint8_t {
            None = 0,
            Space,
            Apostrophe,
            Comma,
            Minus,
            Period,
            Slash,
            Digit0,
            Digit1,
            Digit2,
            Digit3,
            Digit4,
            Digit5,
            Digit6,
            Digit7,
            Digit8,
            Digit9,
            Semicolon,
            Equal,
            A,
            B,
            C,
            D,
            E,
            F,
            G,
            H,
            I,
            J,
            K,
            L,
            M,
            N,
            O,
            P,
            Q,
            R,
            S,
            T,
            U,
            V,
            W,
            X,
            Y,
            Z,
            LeftBracket,
            Backslash,
            RightBracket,
            GraveAccent,
            Escape,
            Enter,
            Tab,
            Backspace,
            Insert,
            Delete,
            RightArrow,
            LeftArrow,
            DownArrow,
            UpArrow,
            PageUp,
            PageDown,
            Home,
            End,
            CapsLock,
            ScrollLock,
            NumLock,
            PrintScreen,
            Pause,
            F1,
            F2,
            F3,
            F4,
            F5,
            F6,
            F7,
            F8,
            F9,
            F10,
            F11,
            F12,
            Keypad0,
            Keypad1,
            Keypad2,
            Keypad3,
            Keypad4,
            Keypad5,
            Keypad6,
            Keypad7,
            Keypad8,
            Keypad9,
            KeypadDecimal,
            KeypadDivide,
            KeypadMultiply,
            KeypadSubtract,
            KeypadAdd,
            KeypadEnter,
            LeftShift,
            LeftControl,
            LeftAlt,
            LeftSuper,
            RightShift,
            RightControl,
            RightAlt,
            RightSuper,
            Menu,
            Count,
        };
        static constexpr std::size_t KeyCodeCount = static_cast<std::size_t>(KeyCode::Count);

        void setMousePosition(Vector2 position) noexcept { mousePosition = position; }

        void setMouseButtonDown(MouseButton button, bool down) noexcept {
            const std::size_t index = static_cast<std::size_t>(button);
            if (down && !mouseDown[index]) {
                mouseClicked[index] = true;
            }
            mouseDown[index] = down;
        }

        void setMouseWheel(float delta) noexcept { mouseWheel += delta; }

        void setKeyState(KeyCode key, bool down) noexcept {
            if (key == KeyCode::None || key == KeyCode::Count) {
                return;
            }
            const std::size_t index = static_cast<std::size_t>(key);
            if (down && !keyDown[index]) {
                keyPressed[index] = true;
            }
            keyDown[index] = down;
        }

        void addCharacter(char32_t codepoint) noexcept { characters.push_back(codepoint); }

        // -- widget side queries ----------------------------------------------

        Vector2 currentMousePosition() const noexcept { return mousePosition; }
        Vector2 currentMouseDelta() const noexcept { return mouseDelta; }
        float currentMouseWheel() const noexcept { return mouseWheel; }

        bool isMouseDown(MouseButton button) const noexcept { return mouseDown[static_cast<std::size_t>(button)]; }

        bool isMouseClicked(MouseButton button) const noexcept {
            return mouseClicked[static_cast<std::size_t>(button)];
        }

        bool isKeyPressed(KeyCode key) const noexcept { return keyPressed[static_cast<std::size_t>(key)]; }

        std::span<const char32_t> currentCharacters() const noexcept { return characters; }

        // -- frame boundaries --------------------------------------------------

        auto onFrameBegin([[maybe_unused]] float deltaTime) noexcept {
            // Positions arrive between frames; the delta is what the mouse moved since
            // the previous frame was presented.
            mouseDelta = Vector2(mousePosition.x - previousMousePosition.x, mousePosition.y - previousMousePosition.y);
        }

        void onFrameEnd() noexcept {
            mouseClicked.fill(false);
            keyPressed.fill(false);
            characters.clear();
            mouseWheel = 0.0f;
            previousMousePosition = mousePosition;
        }

        /// Drops all state, e.g. when the host window loses focus.
        void reset() noexcept {
            mouseDown.fill(false);
            mouseClicked.fill(false);
            keyDown.fill(false);
            keyPressed.fill(false);
            characters.clear();
            mouseWheel = 0.0f;
            mouseDelta = Vector2{};
        }

      private:
        Vector2 mousePosition;
        Vector2 previousMousePosition;
        Vector2 mouseDelta;
        float mouseWheel = 0.0f;

        std::array<bool, MouseButtonCount> mouseDown{};
        std::array<bool, MouseButtonCount> mouseClicked{};

        std::array<bool, KeyCodeCount> keyDown{};
        std::array<bool, KeyCodeCount> keyPressed{};

        std::vector<char32_t> characters;
    };
} // namespace ReZeroGui