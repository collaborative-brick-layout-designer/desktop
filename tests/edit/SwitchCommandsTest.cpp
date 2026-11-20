// Setting switches is one undo step (in Run mode too: Aaron, 2026-11-19); a
// switch put back to its default loses its key; keys of parts no longer on
// the map are dropped as it writes; copies of parts keep the settings.

#include "edit/SwitchCommands.h"
#include "core/LayerBrick.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QUndoStack>

using namespace bld;

TEST(SwitchCommands, SetAndPutBackAsUndoSteps) {
    core::Map map;
    auto layer = std::make_unique<core::LayerBrick>();
    core::Brick p;
    p.guid = QStringLiteral("P");
    layer->bricks.push_back(p);
    map.layers().push_back(std::move(layer));
    map.sidecar.switchStates = { { QStringLiteral("Gone/0"), 1 } };

    QUndoStack stack;
    stack.push(new edit::SetSwitchStatesCommand(map, { { QStringLiteral("P/0"), 2 } }, QStringLiteral("Set a switch")));
    EXPECT_EQ(map.sidecar.switchStates, (QMap<QString, int>{ { QStringLiteral("P/0"), 2 } }));
    stack.push(new edit::SetSwitchStatesCommand(map, { { QStringLiteral("P/0"), std::nullopt } }, QStringLiteral("Set a switch")));
    EXPECT_TRUE(map.sidecar.switchStates.isEmpty());
    stack.undo();
    EXPECT_EQ(map.sidecar.switchStates, (QMap<QString, int>{ { QStringLiteral("P/0"), 2 } }));
    stack.undo();
    EXPECT_EQ(map.sidecar.switchStates, (QMap<QString, int>{ { QStringLiteral("Gone/0"), 1 } }));
    stack.redo();
    EXPECT_EQ(map.sidecar.switchStates, (QMap<QString, int>{ { QStringLiteral("P/0"), 2 } }));
    stack.redo();
    EXPECT_TRUE(map.sidecar.switchStates.isEmpty());
    // Nothing to change: no step.
    const int steps = stack.count();
    stack.push(new edit::SetSwitchStatesCommand(map, { { QStringLiteral("P/0"), std::nullopt } }, QStringLiteral("Set a switch")));
    EXPECT_EQ(stack.count(), steps);
}

TEST(SwitchCommands, CopiesKeepTheirOriginalsSettings) {
    const QMap<QString, int> states{ { QStringLiteral("P/0"), 2 }, { QStringLiteral("Q/1"), 0 } };
    EXPECT_EQ(edit::withCopiedSwitchStates(states, { QStringLiteral("P"), QStringLiteral("X") }, { QStringLiteral("P2"), QStringLiteral("X2") }),
              (QMap<QString, int>{ { QStringLiteral("P/0"), 2 }, { QStringLiteral("Q/1"), 0 }, { QStringLiteral("P2/0"), 2 } }));
}
