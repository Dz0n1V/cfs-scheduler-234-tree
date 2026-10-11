#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "process.hpp"

namespace cfs {

enum class TreeFormat { TwoThreeFour, RedBlack };

// A 2-3-4 tree of processes ordered by (waiting_time, id).
//
// The tree is stored through the well-known isomorphism between 2-3-4 trees and
// red-black trees:
//
//   2-node  [ b ]        <->  black node with two black children
//   3-node  [ a b ]      <->  black node with ONE red child (left or right)
//   4-node  [ a b c ]    <->  black node with TWO red children
//
// Insertion is the classic top-down 2-3-4 algorithm (split every 4-node met on the
// way down), which in red-black terms is a colour flip plus, when needed, rotations.
// Deletion is the standard red-black deletion, which corresponds to borrowing from /
// merging with siblings in the 2-3-4 view. All operations run in O(log n).
class Tree234 {
public:
    Tree234() {
        nil_ = new Node(Process{}, nullptr, Color::Black);
        nil_->parent = nil_->left = nil_->right = nil_;
        root_ = nil_;
    }

    ~Tree234() {
        destroy(root_);
        delete nil_;
    }

    Tree234(const Tree234&) = delete;
    Tree234& operator=(const Tree234&) = delete;

    bool empty() const { return root_ == nil_; }
    std::size_t size() const { return size_; }

    // Destroys every node; the (empty) tree stays usable.
    void clear() {
        destroy(root_);
        root_ = nil_;
        size_ = 0;
    }

    // ---------------------------------------------------------------- insertion
    void insert(const Process& p) {
        Node* z = new Node(p, nil_, Color::Red);
        Node* parent = nil_;
        Node* cur = root_;
        while (cur != nil_) {
            if (is_four_node(cur)) split_four_node(cur);  // top-down 4-node split
            parent = cur;
            cur = (z->process < cur->process) ? cur->left : cur->right;
        }
        z->parent = parent;
        if (parent == nil_)
            root_ = z;
        else if (z->process < parent->process)
            parent->left = z;
        else
            parent->right = z;
        fix_insert(z);
        ++size_;
    }

    // ----------------------------------------------------------------- deletion
    // Removes the process with the same (waiting_time, id) key. Returns false if absent.
    bool erase(const Process& key) {
        Node* n = find_node(key);
        if (n == nil_) return false;
        erase_node(n);
        return true;
    }

    // Removes the process with the given PID (linear lookup). Returns false if absent.
    bool erase_by_id(int id) {
        Node* n = find_node_by_id(root_, id);
        if (n == nil_) return false;
        erase_node(n);
        return true;
    }

    // Removes and returns the leftmost process (smallest waiting time).
    Process pop_min() {
        if (root_ == nil_) throw std::out_of_range("Tree234::pop_min on an empty tree");
        Node* n = root_;
        while (n->left != nil_) n = n->left;
        Process p = n->process;
        erase_node(n);
        return p;
    }

    // ------------------------------------------------------------------- lookup
    std::optional<Process> find_by_id(int id) const {
        Node* n = find_node_by_id(root_, id);
        if (n == nil_) return std::nullopt;
        return n->process;
    }

    // All processes with exactly this waiting time AND execution time, ordered by PID.
    std::vector<Process> search(int waiting_time, int execution_time) const {
        std::vector<Process> out;
        collect(root_, waiting_time, execution_time, out);
        return out;
    }

    // All processes in key order (in-order traversal).
    std::vector<Process> to_vector() const {
        std::vector<Process> out;
        out.reserve(size_);
        in_order(root_, out);
        return out;
    }

    // ------------------------------------------------------------- bulk update
    // Adds `increment` to the waiting time of every process in the tree. Because every
    // key moves by the same amount, the ordering (and thus the tree shape) is unchanged.
    // Returns copies (with the already-updated waiting time) of the processes whose
    // waiting time has reached or passed their maximum waiting time.
    std::vector<Process> add_waiting_time(int increment) {
        std::vector<Process> exceeded;
        add_waiting_time(root_, increment, exceeded);
        return exceeded;
    }

    // ------------------------------------------------------------------ output
    void print(std::ostream& os, TreeFormat format = TreeFormat::TwoThreeFour) const {
        if (root_ == nil_) {
            os << "(empty)\n";
            return;
        }
        if (format == TreeFormat::TwoThreeFour)
            print_234(os, root_, 0);
        else
            print_rb(os, root_, 0, "");
    }

    // Lightweight wrapper so that `os << tree.view(TreeFormat::RedBlack)` works.
    struct View {
        const Tree234& tree;
        TreeFormat format;
    };
    View view(TreeFormat format) const { return View{*this, format}; }

    // ------------------------------------------------------------ verification
    // Checks every structural invariant (BST order, root black, no red-red, equal black
    // height on all paths, parent links, node count). Intended for tests and debugging.
    bool validate(std::string* why = nullptr) const {
        std::string err;
        if (nil_->color != Color::Black) err = "sentinel is not black";
        else if (root_->color != Color::Black) err = "root is not black";
        else if (root_ != nil_ && root_->parent != nil_) err = "root has a parent";
        std::size_t count = 0;
        const Process* prev = nullptr;
        if (err.empty() && check(root_, prev, count, err) < 0 && err.empty()) err = "invariant violated";
        if (err.empty() && count != size_) err = "node count does not match size()";
        if (why) *why = err;
        return err.empty();
    }

private:
    enum class Color { Red, Black };

    struct Node {
        Process process;
        Node* parent;
        Node* left;
        Node* right;
        Color color;
        Node(const Process& p, Node* sentinel, Color c)
            : process(p), parent(sentinel), left(sentinel), right(sentinel), color(c) {}
    };

    Node* nil_;
    Node* root_;
    std::size_t size_ = 0;

    bool is_red(const Node* n) const { return n->color == Color::Red; }

    void destroy(Node* n) {
        if (n == nil_) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

    // ---------------------------------------------------------------- rotations
    void rotate_left(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nil_) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == nil_)
            root_ = y;
        else if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rotate_right(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right != nil_) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == nil_)
            root_ = y;
        else if (x == x->parent->right)
            x->parent->right = y;
        else
            x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    // --------------------------------------------------------------- insertion
    // A black node with two red children is a 4-node.
    bool is_four_node(const Node* n) const {
        return n->color == Color::Black && is_red(n->left) && is_red(n->right);
    }

    // Splits a 4-node: its middle key moves up into the parent (the node turns red and
    // joins the parent), the two outer keys become separate 2-nodes (their nodes turn
    // black). If the parent already holds a red key, rotations restore the invariants.
    void split_four_node(Node* n) {
        n->color = Color::Red;
        n->left->color = Color::Black;
        n->right->color = Color::Black;
        fix_insert(n);
    }

    // Restores the red-black invariants after `z` (red) may have a red parent.
    void fix_insert(Node* z) {
        while (is_red(z->parent)) {
            Node* p = z->parent;
            Node* g = p->parent;
            if (p == g->left) {
                Node* uncle = g->right;
                if (is_red(uncle)) {
                    p->color = Color::Black;
                    uncle->color = Color::Black;
                    g->color = Color::Red;
                    z = g;
                } else {
                    if (z == p->right) {
                        z = p;
                        rotate_left(z);
                    }
                    z->parent->color = Color::Black;
                    z->parent->parent->color = Color::Red;
                    rotate_right(z->parent->parent);
                }
            } else {
                Node* uncle = g->left;
                if (is_red(uncle)) {
                    p->color = Color::Black;
                    uncle->color = Color::Black;
                    g->color = Color::Red;
                    z = g;
                } else {
                    if (z == p->left) {
                        z = p;
                        rotate_right(z);
                    }
                    z->parent->color = Color::Black;
                    z->parent->parent->color = Color::Red;
                    rotate_left(z->parent->parent);
                }
            }
        }
        root_->color = Color::Black;
    }

    // ---------------------------------------------------------------- deletion
    void transplant(Node* u, Node* v) {
        if (u->parent == nil_)
            root_ = v;
        else if (u == u->parent->left)
            u->parent->left = v;
        else
            u->parent->right = v;
        v->parent = u->parent;
    }

    Node* minimum(Node* n) const {
        while (n->left != nil_) n = n->left;
        return n;
    }

    void erase_node(Node* z) {
        Node* y = z;
        Color y_original = y->color;
        Node* x;
        if (z->left == nil_) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nil_) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);  // in-order successor
            y_original = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;
            } else {
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            y->color = z->color;
        }
        if (y_original == Color::Black) fix_erase(x);
        delete z;
        --size_;
        nil_->parent = nil_;
    }

    // `x` carries an extra "black"; in the 2-3-4 view the node it belongs to is
    // under-full and must borrow a key from a sibling or merge with it.
    void fix_erase(Node* x) {
        while (x != root_ && x->color == Color::Black) {
            if (x == x->parent->left) {
                Node* w = x->parent->right;
                if (is_red(w)) {
                    w->color = Color::Black;
                    x->parent->color = Color::Red;
                    rotate_left(x->parent);
                    w = x->parent->right;
                }
                if (!is_red(w->left) && !is_red(w->right)) {
                    w->color = Color::Red;  // merge with sibling
                    x = x->parent;
                } else {
                    if (!is_red(w->right)) {
                        w->left->color = Color::Black;
                        w->color = Color::Red;
                        rotate_right(w);
                        w = x->parent->right;
                    }
                    w->color = x->parent->color;  // borrow from sibling
                    x->parent->color = Color::Black;
                    w->right->color = Color::Black;
                    rotate_left(x->parent);
                    x = root_;
                }
            } else {
                Node* w = x->parent->left;
                if (is_red(w)) {
                    w->color = Color::Black;
                    x->parent->color = Color::Red;
                    rotate_right(x->parent);
                    w = x->parent->left;
                }
                if (!is_red(w->right) && !is_red(w->left)) {
                    w->color = Color::Red;
                    x = x->parent;
                } else {
                    if (!is_red(w->left)) {
                        w->right->color = Color::Black;
                        w->color = Color::Red;
                        rotate_left(w);
                        w = x->parent->left;
                    }
                    w->color = x->parent->color;
                    x->parent->color = Color::Black;
                    w->left->color = Color::Black;
                    rotate_right(x->parent);
                    x = root_;
                }
            }
        }
        x->color = Color::Black;
    }

    // ------------------------------------------------------------------ lookup
    Node* find_node(const Process& key) const {
        Node* x = root_;
        while (x != nil_) {
            if (key.id == x->process.id && key.waiting_time == x->process.waiting_time) return x;
            x = (key < x->process) ? x->left : x->right;
        }
        return nil_;
    }

    Node* find_node_by_id(Node* n, int id) const {
        if (n == nil_) return nil_;
        if (n->process.id == id) return n;
        Node* found = find_node_by_id(n->left, id);
        return found != nil_ ? found : find_node_by_id(n->right, id);
    }

    // Keys with equal waiting time are contiguous in order, so both subtrees are
    // explored only when the waiting time matches.
    void collect(const Node* n, int waiting, int execution, std::vector<Process>& out) const {
        if (n == nil_) return;
        if (waiting < n->process.waiting_time) {
            collect(n->left, waiting, execution, out);
        } else if (waiting > n->process.waiting_time) {
            collect(n->right, waiting, execution, out);
        } else {
            collect(n->left, waiting, execution, out);
            if (n->process.execution_time == execution) out.push_back(n->process);
            collect(n->right, waiting, execution, out);
        }
    }

    void in_order(const Node* n, std::vector<Process>& out) const {
        if (n == nil_) return;
        in_order(n->left, out);
        out.push_back(n->process);
        in_order(n->right, out);
    }

    void add_waiting_time(Node* n, int increment, std::vector<Process>& exceeded) {
        if (n == nil_) return;
        n->process.waiting_time += increment;
        if (n->process.waiting_time >= n->process.max_waiting_time) exceeded.push_back(n->process);
        add_waiting_time(n->left, increment, exceeded);
        add_waiting_time(n->right, increment, exceeded);
    }

    // ------------------------------------------------------------------ output
    // `n` is the black node that anchors one 2-3-4 node; its red children (if any)
    // belong to the same 2-3-4 node.
    void print_234(std::ostream& os, const Node* n, int depth) const {
        std::vector<const Node*> keys;
        std::vector<const Node*> children;
        if (is_red(n->left)) {
            keys.push_back(n->left);
            children.push_back(n->left->left);
            children.push_back(n->left->right);
        } else {
            children.push_back(n->left);
        }
        keys.push_back(n);
        if (is_red(n->right)) {
            keys.push_back(n->right);
            children.push_back(n->right->left);
            children.push_back(n->right->right);
        } else {
            children.push_back(n->right);
        }

        os << std::string(static_cast<std::size_t>(depth) * 4, ' ') << "[ ";
        for (const Node* k : keys) os << k->process << ' ';
        os << "]\n";
        for (const Node* c : children)
            if (c != nil_) print_234(os, c, depth + 1);
    }

    // Pre-order, with the colour of each node and which side of its parent it hangs on.
    void print_rb(std::ostream& os, const Node* n, int depth, const char* side) const {
        if (n == nil_) return;
        os << std::string(static_cast<std::size_t>(depth) * 4, ' ') << side
           << (is_red(n) ? "[R] " : "[B] ") << n->process << '\n';
        print_rb(os, n->left, depth + 1, "L: ");
        print_rb(os, n->right, depth + 1, "R: ");
    }

    // ------------------------------------------------------------ verification
    // Returns the black height of the subtree, or -1 on violation (err is set).
    int check(const Node* n, const Process*& prev, std::size_t& count, std::string& err) const {
        if (n == nil_) return 1;
        if (n->left != nil_ && n->left->parent != n) { err = "bad parent link"; return -1; }
        if (n->right != nil_ && n->right->parent != n) { err = "bad parent link"; return -1; }
        if (is_red(n) && (is_red(n->left) || is_red(n->right))) { err = "red node has a red child"; return -1; }
        const int lh = check(n->left, prev, count, err);
        if (lh < 0) return -1;
        if (prev && !(*prev < n->process)) { err = "keys are not strictly increasing"; return -1; }
        prev = &n->process;
        ++count;
        const int rh = check(n->right, prev, count, err);
        if (rh < 0) return -1;
        if (lh != rh) { err = "unequal black height"; return -1; }
        return lh + (n->color == Color::Black ? 1 : 0);
    }
};

inline std::ostream& operator<<(std::ostream& os, const Tree234::View& v) {
    v.tree.print(os, v.format);
    return os;
}

// Default stream output is the 2-3-4 view.
inline std::ostream& operator<<(std::ostream& os, const Tree234& tree) {
    tree.print(os, TreeFormat::TwoThreeFour);
    return os;
}

}  // namespace cfs
