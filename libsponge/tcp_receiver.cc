#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader header = seg.header();
    Buffer payload = seg.payload();
    string data = payload.copy();
    WrappingInt32 seqno = header.seqno;
    bool syn = header.syn;
    bool fin = header.fin;
    ByteStream &output = stream_out();
    if (_state == s_listen) {
        if (!syn) return ;
        _isn = seqno;
        _offset = static_cast<uint64_t>(1);
        seqno = seqno + static_cast<uint32_t>(1);
        uint64_t checkpoint = static_cast<uint64_t>(output.bytes_written());
        uint64_t index = unwrap(seqno, _isn, checkpoint) - _offset;
        _reassembler.push_substring(data, index, fin);
        if (fin) {
            _state = s_fin;
            _offset = static_cast<uint64_t>(2);
        } else {
            _state = s_syn_recv;
        }
    } else if (_state == s_syn_recv) {
        if (syn) return ;
        uint64_t checkpoint = static_cast<uint64_t>(output.bytes_written());
        uint64_t index = unwrap(seqno, _isn, checkpoint) - _offset;
        // uint64_t index = unwrap(seqno, _isn, abs_seqno);
        _reassembler.push_substring(data, index, fin);
        if (output.input_ended()) {
            _state = s_fin;
            _offset = static_cast<uint64_t>(2);
        }
    } else if (_state == s_fin) {}
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (_state == s_listen) return optional<WrappingInt32>{nullopt};
    const ByteStream &output = stream_out();
    uint64_t abs_sqeno = static_cast<uint64_t>(output.bytes_written()) + _offset;
    return optional<WrappingInt32>{wrap(abs_sqeno, _isn)};
}

size_t TCPReceiver::window_size() const { 
    const ByteStream &output = stream_out();
    size_t unread = output.bytes_read();
    size_t unassembled = output.bytes_written();
    size_t unacceptable = unread + _capacity;
    return unacceptable - unassembled;
}
