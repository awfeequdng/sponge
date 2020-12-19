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

    // 如果不是syn数据包，或者在这之前没有接收到过syn数据包，当前的这个数据包就是无效的，直接丢弃
    if (!_isn_flag)
        return;

    if (seg.header().fin) {
        eof = true;
        _fin_flag = true;
    }

    // 计算出absolute seqno
    uint64_t abs_seqno = unwrap(seg.header().seqno, WrappingInt32(_isn), _reassembler.first_unassembled_index());
    // 从absolute seqno中减去syn占据的一个字节(这个字节被syn消耗，但是没有写入到bytestream的缓存中)，就是bytestream中index的值;
    // 如果当前这个数据包中也包含syn，则当前index不能减1，因为这个就是第一个数据包，abs_seqno就是从0开始偏移
    // 当前的syn只会影响下一个非syn数据包的index，不会影响当前数据包的index。
    // 如果当前是syn包，在计算index不需要减1，否则非syn数据包需要减去1。
    // fin数据包后面不需要接收其他的数据包，所以不会有bytestream的index需要偏移2，即(syn + fin)所占据字节。只需考虑syn多出的一字节偏移
    uint64_t index = abs_seqno - (seg.header().syn ? 0 : _isn_flag);
    std::string push_string(std::string(seg.payload().str()));
    _reassembler.push_substring(push_string, index, eof);
    
    // 根据first_unassembled反推处_next_ackno
    // first_unassembled_index就是下一个希望接收的index，根据_syn_fin_cnt转换成next_ackno
    // segment的syn设置了的数据包一定被放入bytestream，因此first_unassembled_index返回的一定是非syn数据包
    index = _reassembler.first_unassembled_index();
    // 1、非syn数据包的abs_seqno比bytestream的index索引要多1(多了一个syn占用的字节)
    // 2、如果已经接收到了空fin数据包,并且这个fin数据包被放入了bytestream，那么下一个next_ackno就是这个fin数据包序列的下一个序列值（index加上syn占据的一个字节以及fin占据的一个字节）
    // 3、如果已经接收到了非空fin数据包(fin包中不仅包含fin标志，还带有数据)，并且这个fin数据包被放入bytestream，那么next_ackno就是这个数据包序列加上数据包的长度,再加上fin占用的一个字节（index加上syn占据的一个字节，fin占据的一个字节，以及fin数据包的长度）
    // 4、如果最后的fin数据包被接收，那么first_unassembled_index就已经计算了fin数据包的长度了
    bool flag = _fin_flag && _reassembler.unassembled_bytes() == 0;
    abs_seqno = index + _isn_flag + (flag ? 1 : 0);

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
