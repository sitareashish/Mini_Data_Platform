#include "btree.h"
#include <iostream>
#include <stdexcept>

namespace MiniDB {

BTree::BTree() : num_keys(0) {
    root = std::make_shared<BTreeNode>(true);
}

void BTree::insert(const std::string& key, int row_id) {
    BTreeKey bkey{key, row_id};

    if (root->is_full()) {
        auto new_root = std::make_shared<BTreeNode>(false);
        new_root->children.push_back(root);
        split_child(new_root, 0, root);
        root = new_root;
    }
    insert_non_full(root, bkey);
    num_keys++;
}

void BTree::split_child(std::shared_ptr<BTreeNode> parent, int idx,
                         std::shared_ptr<BTreeNode> child) {
    auto new_node = std::make_shared<BTreeNode>(child->is_leaf);
    int t = BTREE_ORDER;

    // Copy second half of child's keys to new_node
    for (int j = 0; j < t - 1; j++)
        new_node->keys.push_back(child->keys[t + j]);

    // Copy children if not leaf
    if (!child->is_leaf) {
        for (int j = 0; j < t; j++)
            new_node->children.push_back(child->children[t + j]);
    }

    // Middle key goes up to parent
    BTreeKey median = child->keys[t - 1];

    // Shrink child
    child->keys.resize(t - 1);
    if (!child->is_leaf)
        child->children.resize(t);

    // Insert new_node into parent's children
    parent->children.insert(parent->children.begin() + idx + 1, new_node);
    parent->keys.insert(parent->keys.begin() + idx, median);
}

void BTree::insert_non_full(std::shared_ptr<BTreeNode> node, const BTreeKey& key) {
    int i = (int)node->keys.size() - 1;

    if (node->is_leaf) {
        // Insert key in sorted position
        node->keys.push_back(key); // placeholder
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            i--;
        }
        node->keys[i + 1] = key;
    } else {
        // Find correct child
        while (i >= 0 && key < node->keys[i])
            i--;
        i++;

        if (node->children[i]->is_full()) {
            split_child(node, i, node->children[i]);
            if (key < node->keys[i])
                ; // stay at i
            else
                i++;
        }
        insert_non_full(node->children[i], key);
    }
}

std::vector<int> BTree::search(const std::string& key) const {
    std::vector<int> results;
    search_node(root, key, results);
    return results;
}

void BTree::search_node(std::shared_ptr<BTreeNode> node, const std::string& key,
                         std::vector<int>& results) const {
    int i = 0;
    while (i < (int)node->keys.size() && key > node->keys[i].value)
        i++;

    // Collect all matching keys at this position
    while (i < (int)node->keys.size() && node->keys[i].value == key) {
        results.push_back(node->keys[i].row_id);
        i++;
    }

    if (!node->is_leaf) {
        // Check children that may contain matching keys
        int start = std::max(0, i - (int)results.size());
        for (int c = start; c <= i && c < (int)node->children.size(); c++)
            search_node(node->children[c], key, results);
    }
}

std::vector<int> BTree::range_search(const std::string& low, const std::string& high) const {
    std::vector<int> results;
    range_search_node(root, low, high, results);
    return results;
}

void BTree::range_search_node(std::shared_ptr<BTreeNode> node, const std::string& low,
                                const std::string& high, std::vector<int>& results) const {
    int i = 0;
    // Skip keys below range
    while (i < (int)node->keys.size() && node->keys[i].value < low) {
        if (!node->is_leaf)
            range_search_node(node->children[i], low, high, results);
        i++;
    }

    // Collect keys in range
    while (i < (int)node->keys.size() && node->keys[i].value <= high) {
        if (!node->is_leaf)
            range_search_node(node->children[i], low, high, results);
        results.push_back(node->keys[i].row_id);
        i++;
    }

    // Rightmost child
    if (!node->is_leaf && i < (int)node->children.size())
        range_search_node(node->children[i], low, high, results);
}

void BTree::remove(const std::string& key, int row_id) {
    // Simple lazy delete: mark matching entries as invalid by reinserting without that row_id
    // For a full implementation, we'd do proper B-Tree deletion
    // This approach rebuilds the relevant leaf entries
    if (!root) return;
    // Find and remove from leaf node
    std::function<bool(std::shared_ptr<BTreeNode>&)> remove_from = [&](std::shared_ptr<BTreeNode>& node) -> bool {
        for (int i = 0; i < (int)node->keys.size(); i++) {
            if (node->keys[i].value == key && node->keys[i].row_id == row_id) {
                node->keys.erase(node->keys.begin() + i);
                num_keys--;
                return true;
            }
        }
        if (!node->is_leaf) {
            for (auto& child : node->children)
                if (remove_from(child)) return true;
        }
        return false;
    };
    remove_from(root);
}

void BTree::print_tree() const {
    print_node(root, 0);
}

void BTree::print_node(std::shared_ptr<BTreeNode> node, int depth) const {
    std::string indent(depth * 2, ' ');
    std::cout << indent << "[";
    for (size_t i = 0; i < node->keys.size(); i++) {
        if (i) std::cout << ", ";
        std::cout << node->keys[i].value << ":" << node->keys[i].row_id;
    }
    std::cout << "]" << (node->is_leaf ? " (leaf)" : "") << "\n";
    for (auto& child : node->children)
        print_node(child, depth + 1);
}

} // namespace MiniDB
