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
        // ����ӳ�䣬ֱ�ӹ���֡����
        EthernetHeader frame_head = {it->second.first, ethernet_address_, EthernetHeader::TYPE_IPv4};
        EthernetFrame frame = {frame_head, serialize(dgram)};
        transmit(frame);
    } else {
        // ���淢������)
        if (wait_arp_msgs.find(next_hop_ipv4Addr) == wait_arp_msgs.end()) {
            // ������ӳ�䣬����ARP����
            EthernetHeader frame_head = {ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP};
            // ����arp��Ϣ
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
    // ֻ������̫��Ŀ�ĵ��ǹ㲥��ַ��洢����̫����ַ��Ա����`_ethernet_address`�е���̫����ַ
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
            // ���ͷ���̫����ַ��ip��ַ
            uint32_t ip_addr = arp_msg.sender_ip_address;
            // ���⣬�����ARP�����������ǵ�IP��ַ���뷢���ʵ���ARP�ظ���
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

                // ����
                EthernetFrame sendframe = {frame_head, serialize(arp_msg_send)};
                transmit(sendframe);
            }
            // ����ӳ��
            arp_map_[ip_addr] = {arp_msg.sender_ethernet_address, time_};
            // ����
            for (auto it = cache_queue_.begin(); it != cache_queue_.end();) {
                Address address_cache = it->first;
                InternetDatagram dgram_cache = it->second;
                // ���ip�Ǹ��µ�ip, ����
                if (address_cache.ipv4_numeric() == ip_addr) {
                    send_datagram(dgram_cache, address_cache);
                    // ɾ��
                    cache_queue_.erase(it);
                } else {
                    it++;
                }
            }
            // ɾ���ѷ���δ��Ӧ�Ĳ���
            wait_arp_msgs.erase(ip_addr);
        }
        return;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    time_ += ms_since_last_tick;
    // ����ӳ���
    for (auto it = arp_map_.begin(); it != arp_map_.end();) {
        // ����30����ɾ��
        if (time_ - (it->second).second >= 30 * 1000) {
            arp_map_.erase(it++);
        } else {
            it++;
        }
    }
    // �����ѷ���, δ��Ӧ�Ĳ���
    for (auto it = wait_arp_msgs.begin(); it != wait_arp_msgs.end(); it++) {
        // ����5�����ط�
        if (time_ - it->second >= 5 * 1000) {
            // ����arp����, �㲥
            EthernetHeader frame_head = {ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP};
            // ����arp��Ϣ
            ARPMessage arp_msg;
            arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
            arp_msg.sender_ethernet_address = ethernet_address_;
            arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
            arp_msg.target_ethernet_address = {0};
            arp_msg.target_ip_address = it->first;
            EthernetFrame frame = {frame_head, serialize(arp_msg)};
            // ����
            transmit(frame);
            // ���·���ʱ��
            it->second = time_;
        }
    }
}
