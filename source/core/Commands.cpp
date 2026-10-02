#include "Commands.h"

namespace trs
{

// The one place that changes a score's tree.
struct ScoreAccess
{
    static Node* find (Score& s, const std::string& id)  { return s.findMutable (id); }

    static Node* findParent (Score& s, const std::string& id, size_t* index)
    {
        return s.findParentMutable (id, index);
    }

    static void touch (Score& s) { ++s.revisionCounter; }
};

namespace
{
    bool isScalar (const Json& v)
    {
        return ! v.isArray() && ! v.isObject();
    }

    bool containsId (const Node& n, const std::string& id)
    {
        if (n.id == id)
            return true;

        for (const auto& c : n.children)
            if (containsId (c, id))
                return true;

        return false;
    }

    // Every id in the subtree must be new to the score, and unique inside the subtree.
    bool idsAreFree (const Score& score, const Node& n, std::vector<std::string>& seen)
    {
        if (n.id.empty() || score.find (n.id) != nullptr)
            return false;

        for (const auto& s : seen)
            if (s == n.id)
                return false;

        seen.push_back (n.id);

        for (const auto& c : n.children)
            if (! idsAreFree (score, c, seen))
                return false;

        return true;
    }

    bool nestingIsValid (const Node& n)
    {
        for (const auto& c : n.children)
            if (! isValidChild (n.type, c.type) || ! nestingIsValid (c))
                return false;

        return true;
    }
}

//==============================================================================
SetPropertyCommand::SetPropertyCommand (std::string nodeId, std::string k, std::optional<Json> v, std::string commandName)
    : id (std::move (nodeId)), key (std::move (k)), label (std::move (commandName)), value (std::move (v))
{
}

bool SetPropertyCommand::apply (Score& score)
{
    auto* node = ScoreAccess::find (score, id);

    if (node == nullptr || key.empty() || (value.has_value() && ! isScalar (*value)))
        return false;

    const auto it = node->props.find (key);
    previous = it != node->props.end() ? std::optional<Json> (it->second) : std::nullopt;

    if (value.has_value())
        node->props[key] = *value;
    else
        node->props.erase (key);

    ScoreAccess::touch (score);
    return true;
}

void SetPropertyCommand::revert (Score& score)
{
    if (auto* node = ScoreAccess::find (score, id))
    {
        if (previous.has_value())
            node->props[key] = *previous;
        else
            node->props.erase (key);

        ScoreAccess::touch (score);
    }
}

//==============================================================================
InsertNodeCommand::InsertNodeCommand (std::string parentId, size_t index, Node subtree, std::string commandName)
    : parent (std::move (parentId)), label (std::move (commandName)), position (index), node (std::move (subtree))
{
}

bool InsertNodeCommand::apply (Score& score)
{
    auto* p = ScoreAccess::find (score, parent);
    std::vector<std::string> seen;

    if (p == nullptr || ! isValidChild (p->type, node.type) || ! nestingIsValid (node) || ! idsAreFree (score, node, seen))
        return false;

    placedAt = position < p->children.size() ? position : p->children.size();
    p->children.insert (p->children.begin() + (std::ptrdiff_t) placedAt, node);
    ScoreAccess::touch (score);
    return true;
}

void InsertNodeCommand::revert (Score& score)
{
    if (auto* p = ScoreAccess::find (score, parent))
    {
        if (placedAt < p->children.size() && p->children[placedAt].id == node.id)
        {
            p->children.erase (p->children.begin() + (std::ptrdiff_t) placedAt);
            ScoreAccess::touch (score);
        }
    }
}

//==============================================================================
RemoveNodeCommand::RemoveNodeCommand (std::string nodeId, std::string commandName)
    : id (std::move (nodeId)), label (std::move (commandName))
{
}

bool RemoveNodeCommand::apply (Score& score)
{
    size_t at = 0;
    auto* p = ScoreAccess::findParent (score, id, &at);

    if (p == nullptr)   // missing, or the root, which cannot be removed
        return false;

    removed = std::move (p->children[at]);
    p->children.erase (p->children.begin() + (std::ptrdiff_t) at);
    parent = p->id;
    index = at;
    ScoreAccess::touch (score);
    return true;
}

void RemoveNodeCommand::revert (Score& score)
{
    if (auto* p = ScoreAccess::find (score, parent))
    {
        const auto at = index < p->children.size() ? index : p->children.size();
        p->children.insert (p->children.begin() + (std::ptrdiff_t) at, std::move (removed));
        ScoreAccess::touch (score);
    }
}

//==============================================================================
MoveNodeCommand::MoveNodeCommand (std::string nodeId, std::string newParentId, size_t index, std::string commandName)
    : id (std::move (nodeId)), newParent (std::move (newParentId)), label (std::move (commandName)), newIndex (index)
{
}

bool MoveNodeCommand::apply (Score& score)
{
    size_t from = 0;
    auto* oldP = ScoreAccess::findParent (score, id, &from);
    auto* newP = ScoreAccess::find (score, newParent);
    auto* node = ScoreAccess::find (score, id);

    if (oldP == nullptr || newP == nullptr || node == nullptr
        || ! isValidChild (newP->type, node->type)
        || containsId (*node, newParent))   // cannot move a node into itself
        return false;

    oldParent = oldP->id;
    oldIndex = from;

    Node moving = std::move (*node);
    oldP->children.erase (oldP->children.begin() + (std::ptrdiff_t) from);

    // The destination may have moved if it comes after the node in the same parent; look it up again.
    newP = ScoreAccess::find (score, newParent);
    const auto at = newIndex < newP->children.size() ? newIndex : newP->children.size();
    newP->children.insert (newP->children.begin() + (std::ptrdiff_t) at, std::move (moving));
    ScoreAccess::touch (score);
    return true;
}

void MoveNodeCommand::revert (Score& score)
{
    size_t at = 0;
    auto* current = ScoreAccess::findParent (score, id, &at);

    if (current == nullptr)
        return;

    Node moving = std::move (current->children[at]);
    current->children.erase (current->children.begin() + (std::ptrdiff_t) at);

    if (auto* back = ScoreAccess::find (score, oldParent))
    {
        const auto to = oldIndex < back->children.size() ? oldIndex : back->children.size();
        back->children.insert (back->children.begin() + (std::ptrdiff_t) to, std::move (moving));
    }

    ScoreAccess::touch (score);
}

//==============================================================================
bool CompositeCommand::apply (Score& score)
{
    for (size_t i = 0; i < steps.size(); ++i)
    {
        if (! steps[i]->apply (score))
        {
            for (auto j = i; j > 0; --j)
                steps[j - 1]->revert (score);

            return false;
        }
    }

    return true;
}

void CompositeCommand::revert (Score& score)
{
    for (auto i = steps.size(); i > 0; --i)
        steps[i - 1]->revert (score);
}

//==============================================================================
bool UndoManager::perform (std::unique_ptr<Command> command)
{
    if (command == nullptr || ! command->apply (score))
        return false;

    redoStack.clear();

    if (group != nullptr)
    {
        group->add (std::move (command));
        return true;
    }

    undoStack.push_back (std::move (command));

    if (undoStack.size() > limit)
        undoStack.erase (undoStack.begin());

    return true;
}

void UndoManager::beginGroup (const std::string& name)
{
    endGroup();
    group = std::make_unique<CompositeCommand> (name);
}

void UndoManager::endGroup()
{
    if (group == nullptr)
        return;

    if (! group->empty())
    {
        undoStack.push_back (std::move (group));

        if (undoStack.size() > limit)
            undoStack.erase (undoStack.begin());
    }

    group.reset();
}

std::string UndoManager::undoName() const
{
    return undoStack.empty() ? std::string() : undoStack.back()->name();
}

std::string UndoManager::redoName() const
{
    return redoStack.empty() ? std::string() : redoStack.back()->name();
}

bool UndoManager::undo()
{
    endGroup();

    if (undoStack.empty())
        return false;

    auto command = std::move (undoStack.back());
    undoStack.pop_back();
    command->revert (score);
    redoStack.push_back (std::move (command));
    return true;
}

bool UndoManager::redo()
{
    endGroup();

    if (redoStack.empty())
        return false;

    auto command = std::move (redoStack.back());
    redoStack.pop_back();

    if (! command->apply (score))
        return false;   // cannot happen when the history is intact; the step is dropped

    undoStack.push_back (std::move (command));
    return true;
}

void UndoManager::clear()
{
    group.reset();
    undoStack.clear();
    redoStack.clear();
}

}  // namespace trs
