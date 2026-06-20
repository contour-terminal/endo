// SPDX-License-Identifier: Apache-2.0
#include <tui/Canvas.hpp>
#include <tui/InputEvent.hpp>
#include <tui/runtime/Modal.hpp>
#include <tui/runtime/TuiRuntime.hpp>
#include <tui/runtime/testing/MockEventSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <variant>

using tui::Canvas;
using tui::InputEvent;
using tui::KeyEvent;
using tui::runtime::ModalComponent;
using tui::runtime::runModal;
using tui::runtime::TuiRuntime;
using tui::runtime::testing::MockEventSource;

namespace
{

/// A trivial modal that completes with the codepoint of the first key it sees.
class KeyCaptureModal: public ModalComponent<char32_t>
{
  public:
    void render(Canvas& /*canvas*/) override {}

    [[nodiscard]] std::optional<char32_t> step(InputEvent const& event) override
    {
        if (auto const* key = std::get_if<KeyEvent>(&event))
            return key->codepoint;
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("runModal resolves with the modal's result", "[Modal]")
{
    auto source = MockEventSource {};
    source.pushEvents({ InputEvent { KeyEvent { .codepoint = U'q' } } });
    auto runtime = TuiRuntime { source };
    auto modal = KeyCaptureModal {};

    auto const result = runtime.blockOn(runModal(&runtime, &modal));

    REQUIRE(result.has_value());
    REQUIRE(*result == U'q');
}

TEST_CASE("runModal returns nullopt when cancelled", "[Modal]")
{
    auto source = MockEventSource {};
    source.pushInterrupt();
    auto runtime = TuiRuntime { source };
    auto modal = KeyCaptureModal {};

    auto const result = runtime.blockOn(runModal(&runtime, &modal));

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("runModal keeps stepping until the modal completes", "[Modal]")
{
    auto source = MockEventSource {};
    // First a mouse move (ignored by the modal), then the key that resolves it.
    source.pushEvents({ InputEvent { tui::MouseEvent { .type = tui::MouseEvent::Type::Move } } });
    source.pushEvents({ InputEvent { KeyEvent { .codepoint = U'k' } } });
    auto runtime = TuiRuntime { source };
    auto modal = KeyCaptureModal {};

    auto const result = runtime.blockOn(runModal(&runtime, &modal));

    REQUIRE(result.has_value());
    REQUIRE(*result == U'k');
}
