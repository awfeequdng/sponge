#include "wrapping_integers.hh"
#include <iostream>
// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t seqno = (n + isn.raw_value())% (1ul << 32);
    return WrappingInt32{seqno};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    int32_t diff = n - isn;
    uint64_t step{};
    if (diff < 0) {
        step = diff + (1ul << 32);
    } else {
        step = diff;
    }
    uint64_t remain = checkpoint % (1ul << 32);
    uint64_t left_dis;
    uint64_t right_dis;
    if (remain >= step) {
        left_dis = (remain - step);
        right_dis = (1ul << 32) - left_dis;
    } else {
        right_dis = (step - remain);
        left_dis = (1ul << 32) - right_dis;
    }
    // 保证checkpoint必须大于left_dis
    if (checkpoint < left_dis) {
        checkpoint += right_dis;
    } else if (right_dis <= left_dis) {
        checkpoint += right_dis;
    } else {
        checkpoint -= left_dis;
    }
    return checkpoint;
}
