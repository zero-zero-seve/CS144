#include "tcp_sender.hh"

#include <algorithm>
#include <optional>

#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const { return num_in_flight_; }

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const { return consecutive_retransmissions_; }

void TCPSender::push(const TransmitFunction& transmit) {
    if (send_FIN) {  // 多余的事情交给重传机制 tick函数
        return;
    }
    // 得到可以使用的空间大小
    uint16_t window_size = (recvwindow_size_ > 0 ? recvwindow_size_ : 1);
    TCPSenderMessage message;
    if (!send_SYN) {
        message.SYN = true;
        message.seqno = isn_;
        message.RST = input_.has_error();
        next_seqno_++;
        if (input_.reader().bytes_buffered() && recv_seqno_ + window_size > next_seqno_) {
            size_t send_size = std::min(TCPConfig::MAX_PAYLOAD_SIZE,
                                        static_cast<size_t>(window_size - (next_seqno_ - recv_seqno_)));
            read(input_.reader(), std::min(send_size, input_.reader().bytes_buffered()), message.payload);
            next_seqno_ += message.payload.length();
        }
        if (input_.reader().is_finished() && recv_seqno_ + window_size > next_seqno_) {
            message.FIN = true;
            send_FIN = true;
            next_seqno_++;
        }
        transmit(message);
        // 更新相关状态
        send_SYN = true;
        num_in_flight_ += message.sequence_length();
        wait_ack_sendmsgs.push(message);
        if (!is_alarm_running) {
            is_alarm_running = true;
            sum_of_time = 0;
        }
        return;
    }
    // 发送结束，单独返回一个FIN包
    if (input_.reader().is_finished() && recv_seqno_ + window_size > next_seqno_) {
        message.FIN = true;
        message.RST = input_.has_error();
        message.seqno = Wrap32::wrap(next_seqno_, isn_);
        transmit(message);
        // 更新相关状态
        num_in_flight_ += message.sequence_length();
        wait_ack_sendmsgs.push(message);
        send_FIN = true;
        next_seqno_++;
        if (!is_alarm_running) {
            is_alarm_running = true;
            sum_of_time = 0;
        }
    }
    // 循环发送，直到无新的字节需要读取或者无可用空间
    while (input_.reader().bytes_buffered() && recv_seqno_ + window_size > next_seqno_) {
        // 根据大小来读取数据
        size_t send_size = std::min(TCPConfig::MAX_PAYLOAD_SIZE,
                                    static_cast<size_t>(window_size - (next_seqno_ - recv_seqno_)));
        read(input_.reader(), std::min(send_size, input_.reader().bytes_buffered()), message.payload);
        message.seqno = Wrap32::wrap(next_seqno_, isn_);
        message.RST = input_.has_error();
        next_seqno_ += message.sequence_length();
        // 如果发送完了，则添加FIN标志
        if (input_.reader().is_finished() && recv_seqno_ + window_size > next_seqno_) {
            message.FIN = true;
            send_FIN = true;
            next_seqno_++;
        }
        // 发送报文
        transmit(message);
        // 更新相关状态
        num_in_flight_ += message.sequence_length();
        wait_ack_sendmsgs.push(message);
        if (!is_alarm_running) {
            is_alarm_running = true;
            sum_of_time = 0;
        }
    }
}

TCPSenderMessage TCPSender::make_empty_message() const {
    TCPSenderMessage msg;
    msg.seqno = Wrap32::wrap(next_seqno_, isn_);
    msg.RST = input_.has_error();
    return msg;
}

void TCPSender::receive(const TCPReceiverMessage& msg) {
    if (msg.RST) {
        input_.set_error();
        return;
    }
    if (msg.ackno == std::nullopt) {
        recvwindow_size_ = msg.window_size;
        return;
    }
    uint64_t abs_ackno = msg.ackno.value().unwrap(isn_, next_seqno_);
    // 超出了窗口范围
    if (abs_ackno > next_seqno_) {
        return;
    }
    // 设置新的窗口大小
    if (abs_ackno >= recv_seqno_) {
        recv_seqno_ = abs_ackno;
        recvwindow_size_ = msg.window_size;
    }
    // 删除已经确认发送成功
    while (!wait_ack_sendmsgs.empty()) {
        TCPSenderMessage send_msg = wait_ack_sendmsgs.front();
        // 当前队列头的段还未发送成功
        if (abs_ackno < send_msg.seqno.unwrap(isn_, next_seqno_) + send_msg.sequence_length()) return;

        // 发送成功，要出队列，修改发送的数据大小
        wait_ack_sendmsgs.pop();
        num_in_flight_ -= send_msg.sequence_length();
        // 重置重传超时时间、重传次数与重传计时器
        RTO_ms_ = initial_RTO_ms_;
        consecutive_retransmissions_ = 0;
        sum_of_time = 0;
    }
    // 重传计时器是否启动取决于发送方是否有未完成的数据
    is_alarm_running = !wait_ack_sendmsgs.empty();
}

void TCPSender::tick(uint64_t ms_since_last_tick, const TransmitFunction& transmit) {
    if (!is_alarm_running) {
        return;
    }
    sum_of_time += ms_since_last_tick;
    if (sum_of_time >= RTO_ms_ && !wait_ack_sendmsgs.empty()) {
        // 重传
        transmit(wait_ack_sendmsgs.front());
        sum_of_time = 0;
        if (recvwindow_size_ > 0) {
            consecutive_retransmissions_++;
            RTO_ms_ *= 2;
        }
    }
}