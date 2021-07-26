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
    _retransmission_timer._working = true;
    _retransmission_timer._que.push(seg);
    _retransmission_timer._bytes_in_flight += seg.length_in_sequence_space();
    _receiver_remain_size -= seg.length_in_sequence_space();
}

void TCPSender::fill_window() {
    //! TODO: Take states into account
    if (_state == closed) {
        TCPSegment seg;
        seg.header().seqno = wrap(_next_seqno, _isn);
        seg.header().syn = true;
        _state = syn_sent;
        send_segment(seg);
        return ;
    } else if (_state == syn_sent) {
        return ;
    } else if (_state == syn_acked) {
        if (_receiver_window_size == 0) {
            if (!_can_send_next) return ;
            TCPSegment seg;
            seg.header().seqno = wrap(_next_seqno, _isn);
            // window size euqals 0 and reach EOF 
            // to avoid over-transmit
            if (stream_in().eof()) {
                seg.header().fin = true;
                _state = fin_sent;
            } else {
                // avoid buffer being empty (I don't know whether this will happpen).
                // when buffer is empty, don't send any segment.
                if (stream_in().buffer_empty()) return ;
                seg.payload() = stream_in().read(1);
            }
            send_segment(seg);
            _can_send_next = false;
        } else if (stream_in().eof() && _receiver_remain_size){
            //! in second-place to avoid over-transmit
            TCPSegment seg;
            seg.header().seqno = wrap(_next_seqno, _isn);
            seg.header().fin = true;
            send_segment(seg);
            _state = fin_sent;
        } else {
            while (!stream_in().buffer_empty() && _receiver_remain_size) {
                TCPSegment seg;
                size_t payload_size = min({
                    stream_in().buffer_size(),
                    static_cast<size_t>(_receiver_remain_size),
                    static_cast<size_t>(TCPConfig::MAX_PAYLOAD_SIZE)
                });
                seg.payload() = stream_in().read(payload_size);
                seg.header().seqno = wrap(_next_seqno, _isn);
                if (stream_in().eof() && (_receiver_remain_size > payload_size)) {
                    seg.header().fin = true;
                    _state = fin_sent;
                }
                send_segment(seg);
            }
        }
    } else if (_state == fin_sent) {
        return ;
    } else if (_state == fin_acked) {
        return ;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    if (_state == closed) return ;
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    // If ackno is larger than the valid seqno (means this should be impossible to happen)
    if (abs_ackno > _next_seqno) return ;
    //! pop the full acked segments.
    while (!_retransmission_timer._que.empty()) {
        uint64_t right = unwrap(_retransmission_timer._que.front().header().seqno, _isn, _next_seqno) + 
            _retransmission_timer._que.front().length_in_sequence_space() - static_cast<uint64_t>(1);
        if (right < abs_ackno) {
            _receiver_remain_size += _retransmission_timer._que.front().length_in_sequence_space();
            _retransmission_timer._bytes_in_flight -= _retransmission_timer._que.front().length_in_sequence_space();
            _retransmission_timer._que.pop();
        }
        else break;
    }
    //! reset retransmission timer when receive new ack.
    if (abs_ackno > _last_recv_ackno) {
        _retransmission_timer._consecutive_retransmission = 0;
        _retransmission_timer._wait_time = 0;
        _retransmission_timer._rto = _initial_retransmission_timeout;
        if ((_state == syn_sent) && (abs_ackno == _next_seqno)) _state = syn_acked;
        if ((_state == fin_sent) && (abs_ackno == _next_seqno)) _state = fin_acked;
        _last_recv_ackno = abs_ackno;
        _can_send_next = true;
    }
    if (abs_ackno >= _last_recv_ackno) {
        //! NOTE: handle same akno with different window size, and NOTE: Interger Type.
        uint64_t left = max(_last_recv_ackno + static_cast<uint64_t>(_receiver_window_size - _receiver_remain_size), abs_ackno);
        uint64_t right = abs_ackno + static_cast<uint64_t>(window_size - 1);
        _receiver_remain_size = right >= left ? static_cast<uint16_t>(right - left + 1) : 0;
        _receiver_window_size = window_size;
        if (window_size) _can_send_next = true;
    }
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    if ((_state == closed) || (_state == fin_acked) || (!_retransmission_timer._working) || (_retransmission_timer._que.empty())) return ;
    _retransmission_timer._wait_time += ms_since_last_tick;
    if (_retransmission_timer._wait_time < _retransmission_timer._rto) return ;
    _segments_out.push(_retransmission_timer._que.front());
    if (_receiver_window_size) {
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
