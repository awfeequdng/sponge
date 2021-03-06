#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPConnection::set_inactive() {
    _active = false;
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _sender.send_empty_segment();
}

void TCPConnection::collect_output() {
    while (not _sender.segments_out().empty()) {
        TCPSegment &seg = _sender.segments_out().front();
        if (_active == false) {
            // 当前处于无效状态，发送rst，然后直接返回
            seg.header().rst = true;
            _segments_out.emplace(seg);
            _sender.segments_out().pop();
            return;
        }
        if (_receiver.ackno().has_value()) {
            // 除了syn，其他所有数据包都要加上ack标志
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        _segments_out.emplace(seg);
        _sender.segments_out().pop();
    }
}

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { 
    return _ms_tick - _last_segment_received_tick;
}

void TCPConnection::segment_received(const TCPSegment &seg) { 
    // 如果当前还没有发送过syn，并且对端发送过来的不是syn，则直接返回，不做任何处理
    if (_sender.next_seqno_absolute() == 0 && not seg.header().syn) {
        return;
    }
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        return;
    }

    _last_segment_received_tick = _ms_tick;
    // 更新接收窗口
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
    _receiver.segment_received(seg);
    

    // 被动关闭时，将_linger_after_streams_finish设置为false，因为被动关闭没有TIME_WAIT这个状态
    // 即CLOSE_WAIT和LAST_ACK状态时将_linger_after_streams_finish设置为false
    // CLOSE_WAIT
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
        _linger_after_streams_finish = false;
    // LAST_ACK : 这个状态不用再设置了，刚刚CLOSE_WAIT状态已经设置为false了
    // if (receiver_state == TCPReceiverStateSummary::FIN_RECV &&
    //     sender_state == TCPSenderStateSummary::FIN_SENT)
    //     _linger_after_streams_finish = false;

    // 接收到syn请求时有两种处理方式：
    // 1、如果还没有发送过syn请求，然后连接的状态会被转换为SYN_RECV，这种情况回复syn+ack;
    // 2、如果刚刚发送了syn请求，还没有接收到ack，然后状态会被转换为SYN_RECV；或者发送了syn，并且接收的segment为syn+ack，然后状态就会转换为ESTABLISHED;此时都回复ack
    if (seg.header().syn) {
        if (TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_SENT ||
            seg.header().ack) {
            // 刚刚发送了syn请求，还没有收到ack；或者发送了syn，并且收到了ack响应
            // 此时回复ack
            _sender.send_empty_segment();
        } else {
            // 还没发送过syn请求：
            // 回复syn/ack 报文
            // 最后面会统一调用_sender.fill_window()发送
            // _sender.fill_window();
        }
    } else if (seg.length_in_sequence_space() > 0) {
        // 如果是数据包，回复ack;
        // 如果是个fin，回复ack
        _sender.send_empty_segment();
    } else {
        // 如果ack不携带任何数据， 则不需要回复ack
    }

    // 如果sender还有数据发送，则继续发送
    _sender.fill_window();
    // 该函数会将所有的segment添加上ackno
    collect_output();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t size = _sender.stream_in().write(data);
    _sender.fill_window();
    collect_output();
    return size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    _ms_tick += ms_since_last_tick;

    
    _sender.tick(ms_since_last_tick);
    // 如果连续重传次数超过_cfg.MAX_RETX_ATTEMPTS，则直接断开链接
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        set_inactive();
    }
    // 超时重传后需要将segment_out中的segment发送出来
    collect_output();
    

    std::string sender_state = TCPState::state_summary(_sender);
    std::string receiver_state = TCPState::state_summary(_receiver);
    // 被动关闭时，将_linger_after_streams_finish设置为false，因为被动关闭没有TIME_WAIT这个状态
    // 即CLOSE_WAIT和LAST_ACK状态时将_linger_after_streams_finish设置为false
    // CLOSE_WAIT
    // if (receiver_state == TCPReceiverStateSummary::FIN_RECV &&
    //     sender_state == TCPSenderStateSummary::SYN_ACKED)
    //     _linger_after_streams_finish = false;
    // LAST_ACK
    // if (receiver_state == TCPReceiverStateSummary::FIN_RECV &&
    //     sender_state == TCPSenderStateSummary::FIN_SENT)
    //     _linger_after_streams_finish = false;


    // 当前处于TIME_WAIT状态
    if (sender_state == TCPSenderStateSummary::FIN_ACKED &&
        receiver_state == TCPReceiverStateSummary::FIN_RECV) {
        if (_linger_after_streams_finish) {
            // 在time_wait等待 10 * _cfg.rt_timeout，然后关闭连接
            if ((_ms_tick - _last_segment_received_tick) >= 10 * _cfg.rt_timeout) {
                _active = false;
            }
        } else {
            // 不需要等待10 * _cfg.rt_timeout,只要接收到_sender处于FIN_ACKED, _receiver处于FIN_RECV立马结束连接
            _active = false;
        }        
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // end_input后，connection还是active的，接收侧还没有关闭
    // _active = false;
    // 在结束输入后，我们将重新填充以下窗口，触发发送机制
    _sender.fill_window();
    collect_output();
}

void TCPConnection::connect() {
    _sender.fill_window();
    collect_output();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            // 此处如何发送rst？？ tcp connection都要结束了？
            // 应该是封装rst报文直接发送，而不是放入发送缓冲区，此处存在bug. todo:
            set_inactive();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
