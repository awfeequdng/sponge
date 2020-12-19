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
    // fin已经被发送，或者已经接收到了fin的ack，不再发送任何segment
    if (_fin_in_flight_or_acked)
        return;

    bool syn_fin_flag{false};
    TCPHeader header{};
    if (next_seqno_absolute() == 0) {
        // SYN segment
        header.syn = true;
        syn_fin_flag = true;
    }
    // 结束输出，将fin标志位置1
    if (_stream.eof()) {
        header.fin = true;
        syn_fin_flag = true;
    }
    header.seqno = next_seqno();

    // 如果是receiver win size满了，不可发送任何数据包，包括发送fin（syn发送时默认window size为1，所以syn发送都能成功）
    uint64_t win_size_remain = _receiver_window_size - bytes_in_flight();
    if (win_size_remain == 0) {
        return;
    }

    uint64_t buf_size = _stream.buffer_size();
    // 如果bytestream没有数据发送，并且当前不是syn或fin包，则不发送任何segment
    if (buf_size == 0 && !syn_fin_flag) {
        return;
    }
    // 发送数据包的大小需要考虑三方面：1、发送缓冲区中数多少；2、TCP max_payload_size；3、receiver window size - bytes_in_flight()
    uint64_t fill_size = min(buf_size, TCPConfig::MAX_PAYLOAD_SIZE);
    fill_size = min(fill_size, win_size_remain);

    Buffer buf(_stream.read(fill_size));
    // bytestream读完数据后，发现存在eof，并且发送窗口有空间发送这个fin（占用一个字节序列），则将header.fin置位
    if (_stream.eof()) {
        if ((fill_size + 1 <= TCPConfig::MAX_PAYLOAD_SIZE) &&
            (fill_size + 1 <= win_size_remain)) {
            header.fin = true;
        }
    }
    // fin将被发送，设置标志
    if (header.fin) {
        _fin_in_flight_or_acked = true;
    }
    TCPSegment seg{};
    seg.header() = header;
    seg.payload() = buf;
    _segments_out.push(seg);
    // track the outstanding TCPSegment
    _segments_track.push_back(std::make_pair(std::make_pair(_ms_tick_cnt, 0), seg));
    _next_seqno += seg.length_in_sequence_space();
    _bytes_in_fight += seg.length_in_sequence_space();
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    while(not _segments_track.empty()) {
        std::pair<std::pair<uint64_t, uint64_t>, TCPSegment> &tick_retx_seg = _segments_track.front();
        TCPSegment &seg = tick_retx_seg.second;
        WrappingInt32 expected_ackno = seg.header().seqno + seg.length_in_sequence_space();
        // 如果expected_ackno 或者 ackno 发生回绕该如何处理？todo:此处存在bug
        if (expected_ackno - ackno <= 0) {
            _bytes_in_fight -= seg.length_in_sequence_space();
            if (seg.header().fin) {
                // 接收到fin的响应
                _fin_in_flight_or_acked = true;
            }
            _segments_track.pop_front();
            // 接收到数据包的ack了，将consecutive_retransmissions置0
            _consecutive_retransmissions = 0;
        } else {
            break;
        }
    }
    _receiver_window_size = window_size;
}

// todo: 在将segment从队列中取出来后，再放进去，最后导致segment的的payload内容改变了？？？
// whiy

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    _ms_tick_cnt += ms_since_last_tick;
    std::deque<std::pair<std::pair<uint64_t, uint64_t>, TCPSegment>>::iterator it_track= _segments_track.begin();
    bool retx_flag{false}; //是否发生重传
    while (it_track != _segments_track.end()) {
        std::pair<uint64_t, uint64_t> &tick_retx = it_track->first;
        uint64_t tick = tick_retx.first;
        uint64_t retx = tick_retx.second;
        // 超时时间是按指数方式增长的，每超时一次，超时时间就翻一倍
        if (tick + (_initial_retransmission_timeout<<retx) <= _ms_tick_cnt) {
            // 超时重传, 更新重传时间,更新重传次数
            tick_retx.first = _ms_tick_cnt;
            tick_retx.second = retx + 1;
            // 将该超时的segment放在队列后面
            _segments_out.push(it_track->second);
            // 连续重传的数据包数目
            retx_flag = true;
        }
        it_track++;
    }
    if (retx_flag)
        _consecutive_retransmissions++; // 发生重传

}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

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
