#include "Score.h"

#include <algorithm>
#include <set>

namespace trs
{

namespace
{
    const Json nullJson;

    const Node* findIn (const Node& node, const std::string& id)
    {
        if (node.id == id)
            return &node;

        for (const auto& c : node.children)
            if (const auto* found = findIn (c, id))
                return found;

        return nullptr;
    }

    const Node* findParentIn (const Node& node, const std::string& id, size_t* index)
    {
        for (size_t i = 0; i < node.children.size(); ++i)
        {
            if (node.children[i].id == id)
            {
                if (index != nullptr)
                    *index = i;

                return &node;
            }

            if (const auto* found = findParentIn (node.children[i], id, index))
                return found;
        }

        return nullptr;
    }

    size_t countIn (const Node& node)
    {
        size_t n = 1;

        for (const auto& c : node.children)
            n += countIn (c);

        return n;
    }

    // The number after the last '-' in an id such as "note-17", or 0.
    uint64_t numberOf (const std::string& id)
    {
        const auto dash = id.rfind ('-');

        if (dash == std::string::npos || dash + 1 >= id.size())
            return 0;

        uint64_t n = 0;

        for (auto i = dash + 1; i < id.size(); ++i)
        {
            if (id[i] < '0' || id[i] > '9' || n > 1000000000000ull)
                return 0;

            n = n * 10 + (uint64_t) (id[i] - '0');
        }

        return n;
    }

    bool validateNode (const Node& node, const Node* parent, std::set<std::string>& seen, std::string& problem)
    {
        if (node.id.empty())
        {
            problem = "a " + node.type + " has no id";
            return false;
        }

        if (! seen.insert (node.id).second)
        {
            problem = "the id " + node.id + " is used twice";
            return false;
        }

        if (parent != nullptr && ! isValidChild (parent->type, node.type))
        {
            problem = "a " + node.type + " (" + node.id + ") cannot be inside a " + parent->type;
            return false;
        }

        for (const auto& [key, value] : node.props)
        {
            if (value.isArray() || value.isObject())
            {
                problem = "the property " + key + " of " + node.id + " is not a single value";
                return false;
            }
        }

        for (const auto& c : node.children)
            if (! validateNode (c, &node, seen, problem))
                return false;

        return true;
    }

    Json nodeToJson (const Node& n)
    {
        auto j = Json::object();
        j.set ("id", n.id);
        j.set ("type", n.type);

        if (! n.props.empty())
        {
            auto props = Json::object();

            for (const auto& [key, value] : n.props)
                props.set (key, value);

            j.set ("props", std::move (props));
        }

        if (! n.children.empty())
        {
            auto children = Json::array();

            for (const auto& c : n.children)
                children.push (nodeToJson (c));

            j.set ("children", std::move (children));
        }

        return j;
    }

    bool nodeFromJson (const Json& j, Node& n, int depth, std::string* error)
    {
        auto fail = [&] (const char* why)
        {
            if (error != nullptr)
                *error = why;

            return false;
        };

        if (depth > 64)
            return fail ("the score is nested too deeply");

        if (! j.isObject() || ! j.get ("id").isString() || ! j.get ("type").isString())
            return fail ("a score node needs an id and a type");

        n.id = j.get ("id").asString();
        n.type = j.get ("type").asString();

        const auto& props = j.get ("props");

        for (const auto& key : props.keys())
            n.props[key] = props.get (key);

        const auto& children = j.get ("children");

        for (const auto& c : children.items())
        {
            Node child;

            if (! nodeFromJson (c, child, depth + 1, error))
                return false;

            n.children.push_back (std::move (child));
        }

        return true;
    }
}

const Json& Node::prop (const std::string& key) const noexcept
{
    const auto it = props.find (key);
    return it != props.end() ? it->second : nullJson;
}

bool isValidChild (const std::string& parent, const std::string& child)
{
    if (parent == nodeType::score)
        return child == nodeType::part || child == nodeType::spanner || child == nodeType::layout;

    if (parent == nodeType::part)    return child == nodeType::staff;
    if (parent == nodeType::staff)   return child == nodeType::measure;
    if (parent == nodeType::measure) return child == nodeType::layer;

    if (parent == nodeType::layer)
        return child == nodeType::note || child == nodeType::rest || child == nodeType::chord;

    if (parent == nodeType::chord)   return child == nodeType::note;

    return false;
}

Score::Score()
{
    rootNode.type = nodeType::score;
    rootNode.id = "score-" + std::to_string (nextId++);
}

const Node* Score::find (const std::string& id) const
{
    return findIn (rootNode, id);
}

const Node* Score::findParent (const std::string& id, size_t* indexInParent) const
{
    return findParentIn (rootNode, id, indexInParent);
}

Node* Score::findMutable (const std::string& id)
{
    return const_cast<Node*> (findIn (rootNode, id));
}

Node* Score::findParentMutable (const std::string& id, size_t* indexInParent)
{
    return const_cast<Node*> (findParentIn (rootNode, id, indexInParent));
}

Node Score::makeNode (const std::string& type)
{
    Node n;
    n.type = type;
    n.id = type + "-" + std::to_string (nextId++);
    return n;
}

Node Score::cloneWithNewIds (const Node& source)
{
    Node copy;
    copy.type = source.type;
    copy.props = source.props;
    copy.id = source.type + "-" + std::to_string (nextId++);

    for (const auto& c : source.children)
        copy.children.push_back (cloneWithNewIds (c));

    return copy;
}

size_t Score::countNodes() const
{
    return countIn (rootNode);
}

std::string Score::validate() const
{
    std::set<std::string> seen;
    std::string problem;

    if (rootNode.type != nodeType::score)
        return "the root is not a score";

    return validateNode (rootNode, nullptr, seen, problem) ? std::string() : problem;
}

Json Score::toJson() const
{
    auto j = Json::object();
    j.set ("nextId", (int64_t) nextId);
    j.set ("root", nodeToJson (rootNode));
    return j;
}

bool Score::fromJson (const Json& j, Score& result, std::string* error)
{
    Score loaded;
    Node root;

    if (! nodeFromJson (j.get ("root"), root, 0, error))
        return false;

    loaded.rootNode = std::move (root);

    const auto problem = loaded.validate();

    if (! problem.empty())
    {
        if (error != nullptr)
            *error = problem;

        return false;
    }

    // Never hand out an id that is already in the score.
    uint64_t highest = 0;
    std::vector<const Node*> stack { &loaded.rootNode };

    while (! stack.empty())
    {
        const auto* n = stack.back();
        stack.pop_back();
        highest = std::max (highest, numberOf (n->id));

        for (const auto& c : n->children)
            stack.push_back (&c);
    }

    loaded.nextId = std::max<uint64_t> ((uint64_t) j.get ("nextId").asInt (1), highest + 1);
    result = std::move (loaded);
    return true;
}

namespace
{
    bool sameNode (const Node& a, const Node& b)
    {
        if (a.id != b.id || a.type != b.type || a.props != b.props || a.children.size() != b.children.size())
            return false;

        for (size_t i = 0; i < a.children.size(); ++i)
            if (! sameNode (a.children[i], b.children[i]))
                return false;

        return true;
    }
}

bool Score::operator== (const Score& other) const
{
    return sameNode (rootNode, other.rootNode);
}

}  // namespace trs
