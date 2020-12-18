#include "tcp_receiver.hh"
#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    bool eof{};
    
    // 第一个数据包可以同时设置SYN和FIN，因此这段代码需要注释掉
    // if (seg.header().syn) {
    //     _isn_flag = true;
    //     _isn = seg.header().seqno.raw_value();
    // } else if (seg.header().fin) {
    //     eof = true;
    // }
    // 第一个数据包可以同时设置SYN和FIN
    if (seg.header().syn) {
        _isn_flag = true;
        _isn = seg.header().seqno.raw_value();
    }
    if (seg.header().fin) {
        eof = true;
    }

    // 计算出absolute seqno
    uint64_t abs_seqno = unwrap(seg.header().seqno, WrappingInt32(_isn), _reassembler.first_unassembled_index());
    // 从absolute seqno中减去syn和fin的个数，就是bytestream中index的值;
    // 如果当前这个数据包中也包含syn和fin，则当前index不能减去这个数据包对syn和fin的计数，
    // 当前的syn和fin只会影响下一个数据包的index，不会影响当前数据包的index。
    uint64_t index = abs_seqno - _syn_fin_cnt;
    std::string push_string(std::string(seg.payload().str()));
    _reassembler.push_substring(push_string, index, eof);
    
    _next_ackno = seg.header().seqno.raw_value() + seg.length_in_sequence_space();

    // 从absolute seqno中减去syn和fin的个数，就是bytestream中index的值;
    // 如果当前这个数据包中也包含syn和fin，则当前index不能减去这个数据包对syn和fin的计数，
    // 当前的syn和fin只会影响下一个数据包的index，不会影响当前数据包的index。
    if (seg.header().syn)
        _syn_fin_cnt++;
    if (seg.header().fin)
        _syn_fin_cnt++;
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (_isn_flag) {
        return WrappingInt32(_next_ackno);
    } else {
        return {};
    }
 }

size_t TCPReceiver::window_size() const { 
    return _reassembler.stream_out().remaining_capacity();
 }
