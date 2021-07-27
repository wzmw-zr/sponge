#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    ByteStream &output = stream_out();
    if (_state == s_listen) {
        if (!seg.header().syn) return ;
        _isn = seg.header().seqno;
        _reassembler.push_substring(seg.payload().copy(), 0, seg.header().fin);
        if (output.input_ended()) _state = s_fin;
        else _state = s_syn_recv;
    } else if (_state == s_syn_recv) {
        if (seg.header().syn) return ;
        uint64_t checkpoint = output.bytes_written();
        uint64_t index = unwrap(seg.header().seqno, _isn, checkpoint) - 1;
        _reassembler.push_substring(seg.payload().copy(), index, seg.header().fin);
        if (output.input_ended()) _state = s_fin;
    } else if (_state == s_fin) {}
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (_state == s_listen) return optional<WrappingInt32>{nullopt};
    return optional<WrappingInt32>{wrap(stream_out().bytes_written() + (_state == s_syn_recv ? 1 : 2), _isn)};
}

size_t TCPReceiver::window_size() const { 
    size_t unread = stream_out().bytes_read();
    size_t unassembled = stream_out().bytes_written();
    size_t unacceptable = unread + _capacity;
    return unacceptable - unassembled;
}
