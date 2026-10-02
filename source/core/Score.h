#pragma once

#include "Json.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace trs
{

// The editable score: a tree of nodes with stable ids (the ids become the MEI xml:ids, so a
// click on the engraved page finds the node). Each node has a type, scalar properties and
// children. The shape follows the plan: score -> part -> staff -> measure -> layer -> events,
// with spanners and layout beside the parts.
//
// The tree is only changed through Command objects (see Commands.h), so every edit can be undone.
namespace nodeType
{
    inline const char* const score   = "score";
    inline const char* const part    = "part";
    inline const char* const staff   = "staff";
    inline const char* const measure = "measure";
    inline const char* const layer   = "layer";     // a voice in a measure
    inline const char* const note    = "note";
    inline const char* const rest    = "rest";
    inline const char* const chord   = "chord";     // holds notes that share one stem
    inline const char* const spanner = "spanner";   // tie, slur, hairpin, ...: refers to events by id
    inline const char* const layout  = "layout";    // page and system settings
}

struct Node
{
    std::string id;
    std::string type;
    std::map<std::string, Json> props;   // numbers, strings and booleans only
    std::vector<Node> children;

    const Json& prop (const std::string& key) const noexcept;
    bool has (const std::string& key) const { return props.count (key) != 0; }
};

// Which node types may sit directly under which.
bool isValidChild (const std::string& parentType, const std::string& childType);

class Score
{
public:
    // An empty score: just the root node.
    Score();

    const Node& root() const noexcept { return rootNode; }

    // nullptr if there is no node with this id. Linear in the size of the score.
    const Node* find (const std::string& id) const;
    const Node* findParent (const std::string& id, size_t* indexInParent = nullptr) const;

    // A new node of this type with a fresh id. It is not part of the score until a Command inserts it.
    Node makeNode (const std::string& type);

    // Gives a copy of the subtree fresh ids (for pasting or duplicating).
    Node cloneWithNewIds (const Node& source);

    // Increases with every change; lets a view know when to redraw.
    uint64_t revision() const noexcept { return revisionCounter; }

    size_t countNodes() const;

    // An empty string if the score is well formed: unique non-empty ids, legal nesting, scalar properties.
    std::string validate() const;

    Json toJson() const;
    static bool fromJson (const Json&, Score& result, std::string* error = nullptr);

    bool operator== (const Score& other) const;

private:
    friend struct ScoreAccess;   // the Commands change the tree through this

    Node* findMutable (const std::string& id);
    Node* findParentMutable (const std::string& id, size_t* indexInParent = nullptr);

    Node rootNode;
    uint64_t nextId = 1;
    uint64_t revisionCounter = 0;
};

}  // namespace trs
