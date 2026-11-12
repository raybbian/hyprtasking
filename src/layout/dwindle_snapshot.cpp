#include "dwindle_snapshot.hpp"

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/algorithm/tiled/dwindle/DwindleAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>

using namespace Layout::Tiled;

namespace {
CDwindleAlgorithm* dwindle(const PHLWORKSPACE& workspace) {
    return dynamic_cast<CDwindleAlgorithm*>(workspace->space()->algorithm()->tiledAlgo().get());
}

void capture_node(const SP<SDwindleNodeData>& node, HTDwindle::Tree& tree) {
    if (!node->isNode) {
        tree.push_back({node->pTarget->window()});
        return;
    }

    capture_node(node->children[0].lock(), tree);
    const size_t first = tree.size() - 1;
    capture_node(node->children[1].lock(), tree);
    tree.push_back({nullptr, first, tree.size() - 1, node->splitRatio, node->splitTop});
}
}

std::optional<HTDwindle::Tree> HTDwindle::capture(const PHLWORKSPACE& workspace) {
    const auto algorithm = dwindle(workspace);
    if (!algorithm)
        return workspace->space()->algorithm()->tiledTargets() == 0 ? std::optional{Tree{}} : std::nullopt;

    for (const auto& ref : workspace->space()->targets()) {
        const auto target = ref.lock();
        if (!target || target->floating())
            continue;
        auto root = algorithm->getNodeFromWindow(target->window());
        if (!root)
            return std::nullopt;
        while (root->pParent)
            root = root->pParent.lock();
        Tree tree;
        capture_node(root, tree);
        return tree;
    }
    return Tree{};
}

bool HTDwindle::restore(const Tree& tree, const PHLWORKSPACE& workspace) {
    if (tree.empty())
        return true;
    const auto algorithm = dwindle(workspace);
    if (!algorithm || workspace->space()->algorithm()->tiledTargets() != (tree.size() + 1) / 2)
        return false;

    // Reuse the leaves and internal nodes allocated by Hyprland's native
    // insertion path. Validate the entire replacement before rewiring links.
    std::vector<SP<SDwindleNodeData>> nodes(tree.size());
    std::vector<SP<SDwindleNodeData>> branches;
    for (size_t i = 0; i < tree.size(); ++i) {
        if (!tree[i].window)
            continue;
        nodes[i] = algorithm->getNodeFromWindow(tree[i].window);
        if (!nodes[i])
            return false;
        for (auto parent = nodes[i]->pParent.lock(); parent; parent = parent->pParent.lock()) {
            if (std::ranges::find(branches, parent) != branches.end())
                break;
            branches.push_back(parent);
        }
    }
    if (branches.size() != tree.size() / 2)
        return false;

    for (size_t i = 0; i < tree.size(); ++i) {
        const auto& split = tree[i];
        if (split.window)
            continue;
        auto node = branches.back();
        branches.pop_back();
        nodes[i] = node;
        node->splitRatio = split.ratio;
        node->splitTop = split.vertical;
        node->children = {nodes[split.first], nodes[split.second]};
        nodes[split.first]->pParent = node;
        nodes[split.second]->pParent = node;
    }
    nodes.back()->pParent.reset();
    workspace->space()->recalculate();
    return true;
}
