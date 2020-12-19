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
    , _receiver_window_size(1) {}

uint64_t TCPSender::bytes_in_flight() const {
    return _bytes_in_fight;
 }

// fill_window会在回复ackno后被上层调用，
// 因此在bytestream中没有数据时，并且不是syn和fin请求，不发送任何数据包
void TCPSender::fill_window() {
    bool send_flag = false;

    TCPHeader header{};
    if (next_seqno_absolute() == 0) {
        // SYN segment
        header.syn = true;
        send_flag = true;
    }
    // 结束输出，将fin标志位置1
    if (_stream.eof()) {
        header.fin = true;
        send_flag = true;
    }
    header.seqno = next_seqno();

    uint64_t buf_size = _stream.buffer_size();
    if (buf_size) {
        send_flag = true;
    }

    if (!send_flag) {
        // 没有数据发送，或者没有syn或fin发送，则不发送任何数据包
        return;
    }
    uint64_t fill_size = min(buf_size, TCPConfig::MAX_PAYLOAD_SIZE);
    // 发送数据包大小还需要考虑receiver的window_size
    // 也就是发送数据包的大小需要考虑三方面：1、发送缓冲区中数多少；2、TCP max_payload_size；3、receiver的window size
    // fill_size = min(fill_size, _receiver_window_size);
    // 以上代码存在bug，是由于我们此时可能有bytes_in_flight()不为0，也就是receiver 可以再接收的数据大小为window_size - bytes_in_flight()
    fill_size = min(fill_size, _receiver_window_size - bytes_in_flight());
    Buffer buf(_stream.read(fill_size));

    TCPSegment seg{};
    seg.header() = header;
    seg.payload() = buf;
    _segments_out.emplace(seg);
    // track the outstanding TCPSegment
    _segments_track.emplace(seg);
    _next_seqno += seg.length_in_sequence_space();
    _bytes_in_fight += seg.length_in_sequence_space();
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    while(not _segments_track.empty()) {
        TCPSegment &seg = _segments_track.front();
        WrappingInt32 expected_ackno = seg.header().seqno + seg.length_in_sequence_space();
        // 如果expected_ackno 或者 ackno 发生回绕该如何处理？todo:此处存在bug
        if (expected_ackno - ackno <= 0) {
            _bytes_in_fight -= seg.length_in_sequence_space();
            _segments_track.pop();
        } else {
            break;
        }
    }
    _receiver_window_size = window_size;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { DUMMY_CODE(ms_since_last_tick); }

unsigned int TCPSender::consecutive_retransmissions() const { return {}; }

void TCPSender::send_empty_segment() {
    TCPHeader header{};
    header.seqno = next_seqno();
    // empty segment用于ack
    header.ack = true;
    TCPSegment seg{};
    seg.header() = header;
    _segments_out.emplace(seg);
    // empty segment不需要跟踪
}
