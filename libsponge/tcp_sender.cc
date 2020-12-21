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
    , _receiver_ackno(_isn)
    , _receiver_window_size(1) {}

uint64_t TCPSender::bytes_in_flight() const {
    return _bytes_in_fight;
 }

// fill_window会在接收ack响应后被上层调用，或者主动发送数据时被上层调用；
// fill_window功能：
// 1、当接收窗口为0时，将这个窗口值当做1，如果发送缓存区有数据，发送1字节的数据；
// 2、当接收窗口为0时，将这个窗口值当做1，如果缓冲区没有数据，并且fin标志设置了，发送一个fin segment；
// 3、当接收窗口为0时，将这个窗口值当做1，如果缓冲区没有数据，且没有syn和fin标志，则直接返回。
void TCPSender::fill_window() {
    // fin已经被发送，或者已经接收到了fin的ack，不再发送任何segment
    if (_fin_in_flight_or_acked)
        return;

    do {
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

        // 下一个seqno的结束位置，不包含结束值
        // When filling window, treat a '0' window size as equal to '1' but don't back off RTO
        // 在调用fill_window填充窗口时，如果接收窗口的值为0，我们将这个接收窗口的值设置为1，但是不回退RTO的值
        WrappingInt32 next_seqno_end = _receiver_ackno + (_receiver_window_size == 0 ? 1 : _receiver_window_size);

        // 如果是receiver win size满了，不可发送任何数据包，
        // 即next_seqno必须在[_receiver_ackno, _receiver_ackno + _receiver_window_size)之间，
        // 否则不发送任何数据包，包括不发送fin（syn发送时默认window size为1，所以syn发送都能成功）
        // 无符号数相减后转换成有符号数，可以解决回绕问题，这个原理和linux内核中的time_before函数类似
        int32_t remain = next_seqno_end - next_seqno();
        if (remain <= 0) {
            // next_seqno() 大于seqno_end说明当前发送窗口大小为0
            return;
        }

        uint64_t win_size_remain = remain;

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
        // 如果发送数据包大小为TCPConfig::MAX_PAYLOAD_SIZE，并且发送窗口足够大，此时限制发送数据包的只有TCPConfig::MAX_PAYLOAD_SIZE时，
        // 如果此时设置了eof，则将fin也随同数据包一起发送
        if (_stream.eof()) {
            if ((fill_size <= TCPConfig::MAX_PAYLOAD_SIZE) &&
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
    } while(_stream.buffer_size() && _receiver_window_size); // 如果bytestream中还有数据，并且接收窗口大于0，则继续发送
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    // ackno的值不能超过next_seqno()，否则认为时无效的ackno
    if (ackno - next_seqno() > 0) {
        return;
    }

    // 在等待队列中的数据接收到了ack响应
    bool ack_outstanding_flag{false};
    while(not _segments_track.empty()) {
        std::pair<std::pair<uint64_t, uint64_t>, TCPSegment> &tick_retx_seg = _segments_track.front();
        TCPSegment &seg = tick_retx_seg.second;
        WrappingInt32 expected_ackno = seg.header().seqno + seg.length_in_sequence_space();
        // 如果expected_ackno 或者 ackno 发生回绕该如何处理？
        // 此时发生了回绕也没有关系，因为WrappingInt32的减法返回的是int32_t类型，如果发生了回绕，减法右边的数本应更大，但是现在变得更小，
        // 减法后无符号数的值转换成有符号后(32位的最高为为1)，那么相减的结果就是负数，不影响比较结果。
        if (expected_ackno - ackno <= 0) {
            _bytes_in_fight -= seg.length_in_sequence_space();
            if (seg.header().fin) {
                // 接收到fin的响应
                _fin_in_flight_or_acked = true;
            }
            _segments_track.pop_front();
            // 接收到数据包的ack了，将consecutive_retransmissions置0
            _consecutive_retransmissions = 0;
            ack_outstanding_flag = true;
        } else {
            break;
        }
    }
    // 如果收到ack后，并且是等待队列segment的ack，则设置余下等待队列第一个segment的超时时间。
    // 如果接收到的ack不是等待队列中segment的ack，那么即使余下的等待队列中有segment也不重置超时时间戳。
    // 接收到ack后，如果重传队列还有其他segment，则将第一个segment的超时时间戳设置为当前值，
    // 第二个第三个segment不用管，在没有接收到第一个segment的ack是不会重传第二个甚至第三个segment的
    if (ack_outstanding_flag && not _segments_track.empty()) {
        std::pair<std::pair<uint64_t, uint64_t>, TCPSegment> &tick_retx_seg = _segments_track.front();
        tick_retx_seg.first.first = _ms_tick_cnt; // 设置当前的时间戳
        tick_retx_seg.first.second = 0; // 重传次数为0
    }
    // 接收窗口为[ackno, ackno + window_size)，当发送的数据包seqno不在这个范围内，则不能发送。
    _receiver_ackno = ackno;
    _receiver_window_size = window_size;
}

// todo: 在将segment从队列中取出来后，再放进去，最后导致segment的的payload内容改变了？？？
// whiy

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    _ms_tick_cnt += ms_since_last_tick;

    std::deque<std::pair<std::pair<uint64_t, uint64_t>, TCPSegment>>::iterator it_track= _segments_track.begin();
    bool retx_flag{false}; //是否发生重传
    // 当重传队列中有多个segment时，第一个segment没有接收到ack时，
    // 即使第二个segment的超时时间到了也不能重传，必须等到第一个segment
    // 接收到ack才能开启第二个segment的超时定时器。
    if (it_track != _segments_track.end()) {
        std::pair<uint64_t, uint64_t> &tick_retx = it_track->first;
        uint64_t tick = tick_retx.first;
        uint64_t retx = tick_retx.second;
        // 超时时间是按指数方式增长的，每超时一次，超时时间就翻一倍
        if (tick + (_initial_retransmission_timeout << retx) <= _ms_tick_cnt) {
            // 超时重传, 更新重传时间,更新重传次数
            tick_retx.first = _ms_tick_cnt;
            // When filling window, treat a '0' window size as equal to '1' but don't back off RTO
            // 当接收窗口大小为0时，超时后不改变RTO的值，此时就是不改变超时计数
            if (_receiver_window_size)
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
    // // 如果bytestream存在错误，则发送rst segment
    // if (stream_in().error()) {
    //     header.rst = true;
    // }
    header.seqno = next_seqno();
    // empty segment用于ack, ack标志以及ack number在其他地方设置
    TCPSegment seg{};
    seg.header() = header;
    _segments_out.emplace(std::move(seg));
    // empty segment不需要跟踪
}
