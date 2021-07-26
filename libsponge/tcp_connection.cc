#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::unclean_shutdown(bool send_rst) {
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    TCPSegment seg = get_segment();
    if (send_rst) seg.header().rst = true;
    _segments_out.push(seg);
}

TCPSegment TCPConnection::get_segment() {
    if (_sender.segments_out().empty()) _sender.send_empty_segment();
    TCPSegment seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    if (_receiver.ackno().has_value()) {
        seg.header().ack = true;
        seg.header().ackno = _receiver.ackno().value();
    }
    seg.header().win = static_cast<uint16_t>(min(static_cast<size_t>(numeric_limits<uint16_t>().max()), _receiver.window_size()));
    return seg;
}

//! prerequisites of clean shutdown 
bool TCPConnection::connection_done() const {
    if (_receiver.stream_out().eof() 
        &&  _sender.stream_in().eof()
        &&  (_sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2)
        &&  (_sender.bytes_in_flight() == 0)) {
        if (!_linger_after_streams_finish) return true;
        if (time_since_last_segment_received() >= (static_cast<size_t>(_cfg.rt_timeout) * static_cast<size_t>(10))) return true;
    }
    return false;
}

void TCPConnection::segment_received(const TCPSegment &seg) { 
    if (connection_done()) return ;
    _time_since_last_segment_received = 0;
    if (seg.header().rst) {
        //! unclean shutdown [receive]
        //! in LISTEN, RST should be ignored.
        if (state() == TCPState::State::LISTEN) return ;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        //! if ackno is correct, don't need to reply.
        if (seg.header().ackno == _sender.next_seqno()) return ;
        TCPSegment reply = get_segment();
        reply.header().rst = true;
        _segments_out.push(reply);
        return ;
    }
    if (seg.header().ack) _sender.ack_received(seg.header().ackno, seg.header().win);
    else {
        if (seg.header().syn && (state() == TCPState::State::LISTEN)) _sender.fill_window();
    }
    _receiver.segment_received(seg);
    //! if receiver reached EOF while sender not, clean shutdown passively.
    if (_receiver.stream_out().eof() && !_sender.stream_in().eof()) _linger_after_streams_finish = false;
    //! reply when incoming segment occupies any sequence numbers.
    if (seg.length_in_sequence_space() && _sender.segments_out().empty()) _sender.send_empty_segment();
    while (!_sender.segments_out().empty()) _segments_out.push(get_segment());
}

bool TCPConnection::active() const { 
    if (_sender.stream_in().error() || _receiver.stream_out().error()) return false;
    if (connection_done()) return false;
    return true;
}

size_t TCPConnection::write(const string &data) {
    if (connection_done()) return 0;
    size_t len = _sender.stream_in().write(data);
    //! send the written data to TCP as possible
    _sender.fill_window();
    while (!_sender.segments_out().empty()) _segments_out.push(get_segment());
    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    if (state() == TCPState::State::LISTEN) return ;
    if (connection_done()) return ;
    auto pre = _sender.consecutive_retransmissions();
    _sender.tick(ms_since_last_tick);
    if (_receiver.ackno().has_value()) _time_since_last_segment_received += ms_since_last_tick;
    if (_sender.consecutive_retransmissions() <= TCPConfig::MAX_RETX_ATTEMPTS) {
        auto after = _sender.consecutive_retransmissions();
        //! when no retransmission, don't send segment.
        if (after != pre) 
            while (!_sender.segments_out().empty()) _segments_out.push(get_segment());
    } else {
        unclean_shutdown(true);
    }
}

//! TODO:
void TCPConnection::end_input_stream() {
    //! when end input stream of sender, and if reach EOF, send FIN message (in `fill_window()` method).
    _sender.stream_in().end_input();
    _sender.fill_window();
    while (!_sender.segments_out().empty()) _segments_out.push(get_segment());
}

//! TODO:
void TCPConnection::connect() {
    //! If sender is not in `closed` state, return. 
    if (_sender.next_seqno_absolute() != 0) return ;
    _sender.fill_window();
    while (!_sender.segments_out().empty()) _segments_out.push(get_segment());
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            unclean_shutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
