#pragma once

#include <functional>
#include <queue>

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

class TCPSender {
   public:
    /**
     * Construct TCP sender with given default Retransmission Timeout and possible ISN
     *
     * @param input ByteStream of outbound data
     * @param isn Initial Sequence Number (ISN) for this connection
     * @param initial_RTO_ms Initial value for the Retransmission Timeout (RTO) in milliseconds
     */
    TCPSender(ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms)
        : input_(std::move(input)),
          isn_(isn),
          send_SYN(false),
          send_FIN(false),
          wait_ack_sendmsgs(),
          initial_RTO_ms_(initial_RTO_ms),
          RTO_ms_(initial_RTO_ms),
          is_alarm_running(false),
          consecutive_retransmissions_(0),
          sum_of_time(0),
          next_seqno_(0),
          recv_seqno_(0),
          recvwindow_size_(1),
          num_in_flight_(0) {}

    /* Generate an empty TCPSenderMessage */
    TCPSenderMessage make_empty_message() const;

    /* Receive and process a TCPReceiverMessage from the peer's receiver */
    void receive(const TCPReceiverMessage& msg);

    /* Type of the `transmit` function that the push and tick methods can use to send messages */
    using TransmitFunction = std::function<void(const TCPSenderMessage&)>;

    /* Push bytes from the outbound stream */
    void push(const TransmitFunction& transmit);

    /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
    void tick(uint64_t ms_since_last_tick, const TransmitFunction& transmit);

    // Accessors
    uint64_t sequence_numbers_in_flight() const;  // For testing: how many sequence numbers are outstanding?
    uint64_t consecutive_retransmissions()
        const;  // For testing: how many consecutive retransmissions have happened?
    const Writer& writer() const { return input_.writer(); }
    const Reader& reader() const { return input_.reader(); }
    Writer& writer() { return input_.writer(); }

   private:
    Reader& reader() { return input_.reader(); }

    ByteStream input_;
    Wrap32 isn_;

    // �Ƿ���SYN�Լ�FIN
    bool send_SYN;
    bool send_FIN;

    // �ݴ淢����ȥ��byte segments
    std::queue<TCPSenderMessage> wait_ack_sendmsgs;

    // ��ʱ(�ش�)��ʱ�����
    uint64_t initial_RTO_ms_;
    uint64_t RTO_ms_;
    bool is_alarm_running;
    uint64_t consecutive_retransmissions_;
    uint64_t sum_of_time;

    // ��һ��δʹ�õ����к��Լ����յ������кţ������
    uint64_t next_seqno_;
    uint64_t recv_seqno_;
    // ���մ��ڵĴ�С
    uint64_t recvwindow_size_;
    // �ڿ������кŵ���Ŀ
    uint64_t num_in_flight_;
};
