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
    if (send_FIN) {  // ��������齻���ش����� tick����
        return;
    }
    // �õ�����ʹ�õĿռ��С
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
        // �������״̬
        send_SYN = true;
        num_in_flight_ += message.sequence_length();
        wait_ack_sendmsgs.push(message);
        if (!is_alarm_running) {
            is_alarm_running = true;
            sum_of_time = 0;
        }
        return;
    }
    // ���ͽ�������������һ��FIN��
    if (input_.reader().is_finished() && recv_seqno_ + window_size > next_seqno_) {
        message.FIN = true;
        message.RST = input_.has_error();
        message.seqno = Wrap32::wrap(next_seqno_, isn_);
        transmit(message);
        // �������״̬
        num_in_flight_ += message.sequence_length();
        wait_ack_sendmsgs.push(message);
        send_FIN = true;
        next_seqno_++;
        if (!is_alarm_running) {
            is_alarm_running = true;
            sum_of_time = 0;
        }
    }
    // ѭ�����ͣ�ֱ�����µ��ֽ���Ҫ��ȡ�����޿��ÿռ�
    while (input_.reader().bytes_buffered() && recv_seqno_ + window_size > next_seqno_) {
        // ���ݴ�С����ȡ����
        size_t send_size = std::min(TCPConfig::MAX_PAYLOAD_SIZE,
                                    static_cast<size_t>(window_size - (next_seqno_ - recv_seqno_)));
        read(input_.reader(), std::min(send_size, input_.reader().bytes_buffered()), message.payload);
        message.seqno = Wrap32::wrap(next_seqno_, isn_);
        message.RST = input_.has_error();
        next_seqno_ += message.sequence_length();
        // ����������ˣ������FIN��־
        if (input_.reader().is_finished() && recv_seqno_ + window_size > next_seqno_) {
            message.FIN = true;
            send_FIN = true;
            next_seqno_++;
        }
        // ���ͱ���
        transmit(message);
        // �������״̬
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
    // �����˴��ڷ�Χ
    if (abs_ackno > next_seqno_) {
        return;
    }
    // �����µĴ��ڴ�С
    if (abs_ackno >= recv_seqno_) {
        recv_seqno_ = abs_ackno;
        recvwindow_size_ = msg.window_size;
    }
    // ɾ���Ѿ�ȷ�Ϸ��ͳɹ�
    while (!wait_ack_sendmsgs.empty()) {
        TCPSenderMessage send_msg = wait_ack_sendmsgs.front();
        // ��ǰ����ͷ�Ķλ�δ���ͳɹ�
        if (abs_ackno < send_msg.seqno.unwrap(isn_, next_seqno_) + send_msg.sequence_length()) return;

        // ���ͳɹ���Ҫ�����У��޸ķ��͵����ݴ�С
        wait_ack_sendmsgs.pop();
        num_in_flight_ -= send_msg.sequence_length();
        // �����ش���ʱʱ�䡢�ش��������ش���ʱ��
        RTO_ms_ = initial_RTO_ms_;
        consecutive_retransmissions_ = 0;
        sum_of_time = 0;
    }
    // �ش���ʱ���Ƿ�����ȡ���ڷ��ͷ��Ƿ���δ��ɵ�����
    is_alarm_running = !wait_ack_sendmsgs.empty();
}

void TCPSender::tick(uint64_t ms_since_last_tick, const TransmitFunction& transmit) {
    if (!is_alarm_running) {
        return;
    }
    sum_of_time += ms_since_last_tick;
    if (sum_of_time >= RTO_ms_ && !wait_ack_sendmsgs.empty()) {
        // �ش�
        transmit(wait_ack_sendmsgs.front());
        sum_of_time = 0;
        if (recvwindow_size_ > 0) {
            consecutive_retransmissions_++;
            RTO_ms_ *= 2;
        }
    }
}