#include "tcp_receiver.hh"

#include "debug.hh"

using namespace std;

void TCPReceiver::receive(TCPSenderMessage message) {
    if (message.RST) {
        reassembler_.reader().set_error();
    }
    if (message.SYN) {
        zero_point.emplace(message.seqno);
        reassembler_.insert(0, message.payload, message.FIN);
    } else if (zero_point != std::nullopt) {
        uint64_t first_index =
            message.seqno.unwrap(zero_point.value(), reassembler_.writer().bytes_pushed()) - 1;
        reassembler_.insert(first_index, message.payload, message.FIN);
    }
}

TCPReceiverMessage TCPReceiver::send() const {
    // ACK号以及滑动窗口大小
    uint16_t window_size = reassembler_.writer().available_capacity() > (uint64_t)65535
                               ? 65535
                               : reassembler_.writer().available_capacity();
    if (zero_point == std::nullopt) {
        return {std::nullopt, window_size, reassembler_.reader().has_error()};
    }
    return {std::optional<Wrap32>(Wrap32::wrap(reassembler_.writer().bytes_pushed() + 1, zero_point.value()) +
                                  reassembler_.writer().is_closed()),
            window_size, reassembler_.reader().has_error()};
}
