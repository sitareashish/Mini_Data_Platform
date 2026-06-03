#pragma once
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <functional>

namespace MiniDB {

// B-Tree order (max children per node)
constexpr int BTREE_ORDER = 4;

struct BTreeKey {
    std::string value;
    int row_id;

    bool operator<(const BTreeKey& other) const {
        return value < other.value;
    }
    bool operator==(const BTreeKey& other) const {
        return value == other.value;
    }
};

struct BTreeNode {
    std::vector<BTreeKey> keys;
    std::vector<std::shared_ptr<BTreeNode>> children;
    bool is_leaf;

    BTreeNode(bool leaf = true) : is_leaf(leaf) {}

    bool is_full() const {
        return (int)keys.size() == 2 * BTREE_ORDER - 1;
    }
};

class BTree {
public:
    BTree();

    void insert(const std::string& key, int row_id);
    std::vector<int> search(const std::string& key) const;
    std::vector<int> range_search(const std::string& low, const std::string& high) const;
    void remove(const std::string& key, int row_id);
    void print_tree() const;
    int size() const { return num_keys; }

private:
    std::shared_ptr<BTreeNode> root;
    int num_keys;

    void split_child(std::shared_ptr<BTreeNode> parent, int idx, std::shared_ptr<BTreeNode> child);
    void insert_non_full(std::shared_ptr<BTreeNode> node, const BTreeKey& key);
    void search_node(std::shared_ptr<BTreeNode> node, const std::string& key, std::vector<int>& results) const;
    void range_search_node(std::shared_ptr<BTreeNode> node, const std::string& low,
                           const std::string& high, std::vector<int>& results) const;
    void print_node(std::shared_ptr<BTreeNode> node, int depth) const;
};

} // namespace MiniDB
