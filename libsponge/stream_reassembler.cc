#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : 
    _output(capacity), _capacity(capacity), _st(set<Node>()) {}

StreamReassembler::Node::Node(string _data, size_t _left, size_t _right, bool _eof) : 
    data(_data), left(_left), right(_right), eof(_eof) {}

bool StreamReassembler::Node::operator<(const struct Node &that) const {
    return this->left == that.left ? this->right > that.right : this->left < that.right;
}

StreamReassembler::Node StreamReassembler::merge(StreamReassembler::Node &a, StreamReassembler::Node &b) {
    if ((a.left >= b.left) && (a.right <= b.right)) {
        b.eof |= a.eof;
        return b;
    }
    if ((b.left >= a.left) && (b.right <= a.right)) {
        a.eof |= b.eof;
        return a;
    }
    int left = min(a.left, b.left);
    int right = max(a.right, b.right);
    string data(right - left + 1, ' ');
    for (size_t i = a.left - left, j = 0; j < (a.right - a.left + 1); j++) data[i + j] = a.data[j];
    for (size_t i = b.left - left, j = 0; j < (b.right - b.left + 1); j++) data[i + j] = b.data[j];
    return Node(data, left, right, a.eof || b.eof);
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    int unread = _output.bytes_read();
    int unassemble = _output.bytes_written();
    int unacceptable = unread + _capacity;
    int left = index, right = index + static_cast<int>(data.size()) - 1;
    if (((left >= unacceptable) || (right < unassemble)) && 
        !((left == unassemble) && (right == (unassemble - 1)))) return ;
    int l = max(left, unassemble);
    int r = min(right, unacceptable - 1);
    string s = data.substr(l - index, r - l + 1);
    bool is_eof = r == right ? eof : false;
    Node temp(s, l, r, is_eof);
    // Merge overlap substring
    set<Node> rest;
    for (auto node : _st) {
        if ((node.left > temp.right) || (node.right < temp.left)) rest.insert(node);
        else temp = merge(temp, node);
    }
    _st = rest;
    _st.insert(temp);
    // Assmemble substring.
    while (!_st.empty() && _st.begin()->left == _output.bytes_written()) {
        _output.write(_st.begin()->data);
        if (_st.begin()->eof) _output.end_input();
        _st.erase(_st.begin());
    }
}

size_t StreamReassembler::unassembled_bytes() const { 
    size_t cnt = 0;
    for (auto node : _st) cnt += node.right - node.left + 1;
    return cnt;
}

bool StreamReassembler::empty() const { return _st.empty(); }
