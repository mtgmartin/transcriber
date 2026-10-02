#pragma once

#include "Score.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trs
{

// The only way the score changes. A command is applied once and can then be reverted and applied
// again, always leaving the score exactly as it was.
class Command
{
public:
    virtual ~Command() = default;

    // What the Undo menu says, e.g. "Change pitch".
    virtual std::string name() const = 0;

    // false (and no change) if the command does not fit the score, e.g. its target is gone.
    virtual bool apply (Score&) = 0;
    virtual void revert (Score&) = 0;
};

// Sets one property of a node, or removes it when no value is given.
class SetPropertyCommand final : public Command
{
public:
    SetPropertyCommand (std::string nodeId, std::string key, std::optional<Json> newValue, std::string commandName = "Change property");

    std::string name() const override { return label; }
    bool apply (Score&) override;
    void revert (Score&) override;

private:
    std::string id, key, label;
    std::optional<Json> value, previous;
};

// Inserts a subtree under a parent. index >= the number of children appends.
class InsertNodeCommand final : public Command
{
public:
    InsertNodeCommand (std::string parentId, size_t index, Node subtree, std::string commandName = "Insert");

    std::string name() const override { return label; }
    bool apply (Score&) override;
    void revert (Score&) override;

private:
    std::string parent, label;
    size_t position;
    Node node;
    size_t placedAt = 0;
};

// Removes a node and everything below it.
class RemoveNodeCommand final : public Command
{
public:
    explicit RemoveNodeCommand (std::string nodeId, std::string commandName = "Delete");

    std::string name() const override { return label; }
    bool apply (Score&) override;
    void revert (Score&) override;

private:
    std::string id, label, parent;
    size_t index = 0;
    Node removed;
};

// Moves a node (with its subtree) under another parent. The id does not change.
class MoveNodeCommand final : public Command
{
public:
    MoveNodeCommand (std::string nodeId, std::string newParentId, size_t newIndex, std::string commandName = "Move");

    std::string name() const override { return label; }
    bool apply (Score&) override;
    void revert (Score&) override;

private:
    std::string id, newParent, label, oldParent;
    size_t newIndex, oldIndex = 0;
};

// Several commands that undo and redo as one step.
class CompositeCommand final : public Command
{
public:
    explicit CompositeCommand (std::string commandName) : label (std::move (commandName)) {}

    // Adds a command that has not been applied yet.
    void add (std::unique_ptr<Command> c) { steps.push_back (std::move (c)); }

    std::string name() const override { return label; }
    bool apply (Score&) override;
    void revert (Score&) override;

    bool empty() const noexcept { return steps.empty(); }

private:
    friend class UndoManager;

    std::string label;
    std::vector<std::unique_ptr<Command>> steps;
};

// The undo history of one score.
class UndoManager
{
public:
    explicit UndoManager (Score& scoreToEdit, size_t maxSteps = 500) : score (scoreToEdit), limit (maxSteps) {}

    // Applies the command and remembers it. false if it did not apply (nothing is remembered).
    bool perform (std::unique_ptr<Command>);

    // Everything performed between begin and end undoes as a single step named `name`.
    void beginGroup (const std::string& name);
    void endGroup();

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    std::string undoName() const;
    std::string redoName() const;

    bool undo();
    bool redo();

    void clear();

private:
    Score& score;
    size_t limit;
    std::vector<std::unique_ptr<Command>> undoStack, redoStack;
    std::unique_ptr<CompositeCommand> group;   // already applied
};

}  // namespace trs
