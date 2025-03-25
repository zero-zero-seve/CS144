#include "network_interface.hh"

#include <iostream>

#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface

bool NetworkInterface::equal(const EthernetAddress& d1, const EthernetAddress& d2) {
    for (int i = 0; i < 6; i++) {
        if (d1[i] != d2[i]) {
            return false;
        }
    }

    return true;
}

NetworkInterface::NetworkInterface(string_view name, shared_ptr<OutputPort> port,
                                   const EthernetAddress& ethernet_address, const Address& ip_address)
    : name_(name),
      port_(notnull("OutputPort", move(port))),
      ethernet_address_(ethernet_address),
      ip_address_(ip_address),
      arp_map_(),
      wait_arp_msgs(),
      cache_queue_(),
      time_(0) {}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway,
//! but may also be another host if directly connected to the same network as the destination) Note: the
//! Address type can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric()
//! method.
void NetworkInterface::send_datagram(const InternetDatagram& dgram, const Address& next_hop) {
    uint32_t next_hop_ipv4Addr = next_hop.ipv4_numeric();
    auto it = arp_map_.find(next_hop_ipv4Addr);
    if (it != arp_map_.end() && time_ - it->second.second < 30 * 1000) {
        // 存在映射，直接构造帧发送
        EthernetHeader frame_head = {it->second.first, ethernet_address_, EthernetHeader::TYPE_IPv4};
        EthernetFrame frame = {frame_head, serialize(dgram)};
        transmit(frame);
    } else {
        // 缓存发送数据)
        if (wait_arp_msgs.find(next_hop_ipv4Addr) == wait_arp_msgs.end()) {
            // 不存在映射，发送ARP请求
            EthernetHeader frame_head = {ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP};
            // 创建arp消息
            ARPMessage arp_msg;
            arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
            arp_msg.sender_ethernet_address = ethernet_address_;
            arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
            arp_msg.target_ethernet_address = {0};
            arp_msg.target_ip_address = next_hop_ipv4Addr;
            EthernetFrame frame = {frame_head, serialize(arp_msg)};
            transmit(frame);
            wait_arp_msgs[next_hop_ipv4Addr] = time_;
            cache_queue_.push_back(std::make_pair(next_hop, dgram));
        } else {
            for (auto it2 = cache_queue_.begin(); it2 != cache_queue_.end(); it2++) {
                if (it2->first.ipv4_numeric() == next_hop_ipv4Addr &&
                    it2->second.header.src == dgram.header.src &&
                    it2->second.header.dst == dgram.header.dst) {
                    return;
                }
            }
            cache_queue_.push_back(std::make_pair(next_hop, dgram));
        }
    }
}
//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame(EthernetFrame frame) {
    EthernetHeader header = frame.header;
    // 只接受以太网目的地是广播地址或存储在以太网地址成员变量`_ethernet_address`中的以太网地址
    if (!equal(header.dst, ETHERNET_BROADCAST) && !equal(header.dst, ethernet_address_)) {
        return;
    }
    if (frame.header.type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram ip_datagram;
        auto res = parse(ip_datagram, move(frame.payload));
        if (res == true) {
            datagrams_received_.push(ip_datagram);
        } else {
            return;
        }
    } else {
        ARPMessage arp_msg;
        auto res = parse(arp_msg, move(frame.payload));
        if (res == true) {
            // 发送方以太网地址和ip地址
            uint32_t ip_addr = arp_msg.sender_ip_address;
            // 此外，如果是ARP请求请求我们的IP地址，请发送适当的ARP回复。
            if ((arp_msg.opcode == ARPMessage::OPCODE_REQUEST) &&
                (arp_msg.target_ip_address == ip_address_.ipv4_numeric())) {
                EthernetHeader frame_head = {arp_msg.sender_ethernet_address, ethernet_address_,
                                             EthernetHeader::TYPE_ARP};
                ARPMessage arp_msg_send;
                arp_msg_send.opcode = arp_msg_send.OPCODE_REPLY;
                arp_msg_send.sender_ethernet_address = ethernet_address_;
                arp_msg_send.sender_ip_address = ip_address_.ipv4_numeric();
                arp_msg_send.target_ethernet_address = arp_msg.sender_ethernet_address;
                arp_msg_send.target_ip_address = arp_msg.sender_ip_address;

                // 发送
                EthernetFrame sendframe = {frame_head, serialize(arp_msg_send)};
                transmit(sendframe);
            }
            // 更新映射
            arp_map_[ip_addr] = {arp_msg.sender_ethernet_address, time_};
            // 发送
            for (auto it = cache_queue_.begin(); it != cache_queue_.end();) {
                Address address_cache = it->first;
                InternetDatagram dgram_cache = it->second;
                // 如果ip是更新的ip, 则发送
                if (address_cache.ipv4_numeric() == ip_addr) {
                    send_datagram(dgram_cache, address_cache);
                    // 删除
                    cache_queue_.erase(it);
                } else {
                    it++;
                }
            }
            // 删除已发送未回应的部分
            wait_arp_msgs.erase(ip_addr);
        }
        return;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    time_ += ms_since_last_tick;
    // 更新映射表
    for (auto it = arp_map_.begin(); it != arp_map_.end();) {
        // 超过30秒则删除
        if (time_ - (it->second).second >= 30 * 1000) {
            arp_map_.erase(it++);
        } else {
            it++;
        }
    }
    // 更新已发送, 未回应的部分
    for (auto it = wait_arp_msgs.begin(); it != wait_arp_msgs.end(); it++) {
        // 超过5秒则重发
        if (time_ - it->second >= 5 * 1000) {
            // 不在arp表中, 广播
            EthernetHeader frame_head = {ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP};
            // 创建arp消息
            ARPMessage arp_msg;
            arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
            arp_msg.sender_ethernet_address = ethernet_address_;
            arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
            arp_msg.target_ethernet_address = {0};
            arp_msg.target_ip_address = it->first;
            EthernetFrame frame = {frame_head, serialize(arp_msg)};
            // 发送
            transmit(frame);
            // 更新发送时间
            it->second = time_;
        }
    }
}
