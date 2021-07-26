#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStreamBuffer::ByteStreamBuffer(const size_t size) : capacity(size), written_bytes(0), read_bytes(0), buff(deque<char>()) {}

size_t ByteStreamBuffer::push(char c) {
    if (!capacity) return 0;
    buff.push_back(c);
    capacity--;
    written_bytes++;
    return 1;
}

void ByteStreamBuffer::pop() {
    if (buff.empty()) return ;
    buff.pop_front();
    capacity++;
    read_bytes++;
}

bool ByteStreamBuffer::empty() const {
    return buff.empty();
}

size_t ByteStreamBuffer::size() const {
    return buff.size();
}

size_t ByteStreamBuffer::total_written() const {
    return written_bytes;
}

size_t ByteStreamBuffer::total_read() const {
    return read_bytes;
}

size_t ByteStreamBuffer::remain_capacity() const {
    return capacity;
}

char ByteStreamBuffer::operator[](int ind) const {
    return buff[ind];
}

ByteStream::ByteStream(const size_t capacity) : buff(capacity), end_of_sending(false) {}

size_t ByteStream::write(const string &data) {
    size_t len = 0;
    for (char c : data) {
        if (buff.push(c) == 0) break;
        len++;
    }
    return len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string s = "";
    size_t t_len = min(len, buff.size());
    for (size_t i = 0; i < t_len; i++) s += buff[i];
    return s;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    for (size_t i = 0; i < len && !buff.empty(); i++) buff.pop();
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string s = peek_output(len);
    pop_output(len);
    return s;
}

void ByteStream::end_input() {
    end_of_sending = true;
}

bool ByteStream::input_ended() const { return end_of_sending; }

size_t ByteStream::buffer_size() const { return buff.size(); }

bool ByteStream::buffer_empty() const { return buff.empty(); }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return buff.total_written(); }

size_t ByteStream::bytes_read() const { return buff.total_read(); }

size_t ByteStream::remaining_capacity() const { return buff.remain_capacity(); }
