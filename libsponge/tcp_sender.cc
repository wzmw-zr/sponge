#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>
#include <iostream>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _retransmission_timer(RetransmissionTimer{retx_timeout})
    , _state(closed) {}

uint64_t TCPSender::bytes_in_flight() const { return _retransmission_timer._bytes_in_flight; }

void TCPSender::send_segment(TCPSegment &seg) {
    _segments_out.push(seg);
    _next_seqno += seg.length_in_sequence_space();
    if (!_retransmission_timer._working) {
        _retransmission_timer._working = true;
        _retransmission_timer._wait_time = 0;
    }
    _retransmission_timer._que.push(seg);
    _retransmission_timer._bytes_in_flight += seg.length_in_sequence_space();
    _receiver_remain_size -= seg.length_in_sequence_space();
}

void TCPSender::fill_window() {
    if (_state == closed) {
        TCPSegment seg;
        seg.header().seqno = wrap(_next_seqno, _isn);
        seg.header().syn = true;
        _state = syn_sent;
        send_segment(seg);
        return ;
    }
    if (_state == syn_sent) return ;
    if (_state == syn_acked) {
        if (!_stream.buffer_size() && !_stream.eof()) return ;
        if (_receiver_window_size) {
            while (_receiver_remain_size) {
                TCPSegment seg;
                size_t payload_size = min({
                    _stream.buffer_size(),
                    static_cast<size_t>(_receiver_remain_size),
                    static_cast<size_t>(TCPConfig::MAX_PAYLOAD_SIZE)
                });
                seg.payload() = _stream.read(payload_size);
                seg.header().seqno = wrap(_next_seqno, _isn);
                if (_stream.eof() && (static_cast<size_t>(_receiver_remain_size) > payload_size)) {
                    seg.header().fin = true;
                    _state = fin_sent;
                }
                send_segment(seg);
                if (_stream.buffer_empty()) break;
            }
        } else if (_receiver_remain_size == 0) {
            TCPSegment seg;
            if (_stream.eof()) {
                seg.header().fin = true;
                _state = fin_sent;
                seg.header().seqno = wrap(_next_seqno, _isn);
                send_segment(seg);
            } else if (!_stream.buffer_empty()) {
                seg.header().seqno = wrap(_next_seqno, _isn);
                seg.payload() = _stream.read(1);
                send_segment(seg);
            }
        }
        return ;
    } 
    if (_state == fin_sent) return ;
    if (_state == fin_acked) return ;
}

bool TCPSender::valid_ackno(uint64_t abs_ackno) {
    return (abs_ackno <= _next_seqno) && (abs_ackno >= _last_ackno);
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    if (!valid_ackno(abs_ackno)) return ;
    _receiver_window_size = window_size;
    _receiver_remain_size = window_size;
    //! pop the full acked segments.
    while (!_retransmission_timer._que.empty()) {
        uint64_t right = unwrap(_retransmission_timer._que.front().header().seqno, _isn, _next_seqno) + 
            _retransmission_timer._que.front().length_in_sequence_space();
        if (right <= abs_ackno) {
            _retransmission_timer._bytes_in_flight -= _retransmission_timer._que.front().length_in_sequence_space();
            _retransmission_timer._que.pop();
            _retransmission_timer._wait_time = 0;
            _retransmission_timer._rto = _initial_retransmission_timeout;
            _retransmission_timer._consecutive_retransmission = 0;
            _last_ackno = right;
        }
        else break;
    }
    if (!_retransmission_timer._que.empty()) {
        _receiver_remain_size = static_cast<uint16_t>(
            abs_ackno + static_cast<uint64_t>(window_size) - 
            unwrap(_retransmission_timer._que.front().header().seqno, _isn, _next_seqno) - _retransmission_timer._bytes_in_flight
            ); 
    }
    if (!_retransmission_timer._bytes_in_flight) {
        _retransmission_timer._working = false;
    }
    if ((_state == syn_sent) && (abs_ackno == _next_seqno)) _state = syn_acked;
    if ((_state == fin_sent) && (abs_ackno == _next_seqno)) _state = fin_acked;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    if (!_retransmission_timer._working) return ;
    _retransmission_timer._wait_time += ms_since_last_tick;
    if (_retransmission_timer._wait_time < _retransmission_timer._rto) return ;
    _segments_out.push(_retransmission_timer._que.front());
    //! Increase RTO if window size not 0 or this is SYN segment.
    if (_receiver_window_size || _retransmission_timer._que.front().header().syn) {
        //! when receiver window size equals 0, don't change rto.
        _retransmission_timer._consecutive_retransmission++;
        _retransmission_timer._rto <<= 1;
    }
    _retransmission_timer._wait_time = 0;
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retransmission_timer._consecutive_retransmission; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}
