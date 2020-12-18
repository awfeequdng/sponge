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
    // 如果不是syn数据包，或者在这之前没有接收到过syn数据包，当前的这个数据包就是无效的，直接丢弃
    if (!_isn_flag)
        return;

    // 计算出absolute seqno
    uint64_t abs_seqno = unwrap(seg.header().seqno, WrappingInt32(_isn), _reassembler.first_unassembled_index());
    // 从absolute seqno中减去syn和fin的个数，就是bytestream中index的值;
    // 如果当前这个数据包中也包含syn和fin，则当前index不能减去这个数据包对syn和fin的计数，
    // 当前的syn和fin只会影响下一个数据包的index，不会影响当前数据包的index。
    uint64_t index = abs_seqno - _syn_fin_cnt;
    std::string push_string(std::string(seg.payload().str()));
    _reassembler.push_substring(push_string, index, eof);
    
    // 从absolute seqno中减去syn和fin的个数，就是bytestream中index的值;
    // 如果当前这个数据包中也包含syn和fin，则当前index不能减去这个数据包对syn和fin的计数，
    // 当前的syn和fin只会影响下一个数据包的index，不会影响当前数据包的index。
    if (seg.header().syn)
        _syn_fin_cnt++;
    if (seg.header().fin)
        _syn_fin_cnt++;

    // 根据first_unassembled反推处_next_ackno
    // first_unassembled_index就是下一个希望接收的index，根据_syn_fin_cnt转换成next_ackno
    // 这样反推可能存在一个问题：
    // 当发送数据包的顺序如下：syn -> data1 -> data2 -> fin，当这个data1和data2数据包都丢失后，
    // 发送数据包的顺序可能就是: syn -> data1 -> data2 -> fin -> data1 -> data2,
    // 接收端看到的数据包可能就是: syn -> fin -> data1 -> data2，
    // 那么此时从接收端看来，data1回应ack时_syn_fin_cnt等于2了(本应该等于1，因为在正常的有序的顺序来看data1前面只有syn数据包)，
    // 但此时由于数据包乱序后，data1比fin晚到接收段，导致data1接收到时，syn 和fin都接收到了,从而_syn_fin_cnt等于2。
    // 然后通过first_unassembled_index以及_syn_fin_cnt反推出_next_ackno时，_next_ackno就比实际的值要大1。
    // 因此这段代码不能使用(虽然能通过recv_window的测试用例，但对乱序的考虑不足，还存在bug)
    index = _reassembler.first_unassembled_index();
    abs_seqno = index + _syn_fin_cnt;
    _next_ackno = wrap(abs_seqno, WrappingInt32(_isn)).raw_value();
    // 这段代码默认认为数据包是顺序到达的，这有一定问题
    // _next_ackno = seg.header().seqno.raw_value() + seg.length_in_sequence_space();
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
