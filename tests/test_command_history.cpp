#include "Editor/Commands/CommandHistory.h"
#include "doctest.h"
#include <memory>

using molga::CommandHistory;
using molga::ICommand;

namespace {
struct CounterCommand : ICommand {
    int* value;
    int delta;
    CounterCommand(int* v, int d) : value(v), delta(d) {}
    void Execute() override { *value += delta; }
    void Undo() override    { *value -= delta; }
    std::string Name() const override { return "Counter"; }
};
}

TEST_CASE("Execute applies the command and makes it undoable") {
    int v = 0;
    CommandHistory h;
    h.Execute(std::make_unique<CounterCommand>(&v, 5));
    CHECK(v == 5);
    CHECK(h.CanUndo());
    CHECK_FALSE(h.CanRedo());
    h.Undo();
    CHECK(v == 0);
    CHECK(h.CanRedo());
}

TEST_CASE("Redo re-applies an undone command") {
    int v = 0;
    CommandHistory h;
    h.Execute(std::make_unique<CounterCommand>(&v, 3));
    h.Undo();
    h.Redo();
    CHECK(v == 3);
    CHECK_FALSE(h.CanRedo());
}

TEST_CASE("a new command after undo clears the redo stack") {
    int v = 0;
    CommandHistory h;
    h.Execute(std::make_unique<CounterCommand>(&v, 1));
    h.Execute(std::make_unique<CounterCommand>(&v, 1));  // v = 2
    h.Undo();                                            // v = 1
    REQUIRE(h.CanRedo());
    h.Execute(std::make_unique<CounterCommand>(&v, 10)); // v = 11
    CHECK_FALSE(h.CanRedo());
    CHECK(v == 11);
}

TEST_CASE("Undo/Redo on empty stacks are safe no-ops") {
    CommandHistory h;
    h.Undo();
    h.Redo();
    CHECK_FALSE(h.CanUndo());
    CHECK_FALSE(h.CanRedo());
}
