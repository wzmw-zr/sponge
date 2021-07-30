#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! This is the Network Interface Under IP Protocol.
//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    if (ip_arp.count(next_hop_ip)) {
        auto &&[dst_eth_addr, t] = ip_arp[next_hop_ip];
        EthernetFrame frame;
        frame.payload() = dgram.serialize();
        frame.header().type = frame.header().TYPE_IPv4;
        frame.header().src = this->_ethernet_address;
        frame.header().dst = dst_eth_addr;
        _frames_out.push(frame);
        return ;
    }
    //! Broadcast arp requests.
    if (!arp_requests.count(next_hop_ip)) {
        arp_requests[next_hop_ip] = 0;
        unsend_datagram[next_hop_ip] = vector<InternetDatagram>{};
        ARPMessage msg;
        msg.opcode = msg.OPCODE_REQUEST;
        msg.sender_ip_address = this->_ip_address.ipv4_numeric();
        msg.sender_ethernet_address = this->_ethernet_address;
        msg.target_ip_address = next_hop_ip;
        EthernetFrame frame;
        frame.payload() = msg.serialize();
        frame.header().type = frame.header().TYPE_ARP;
        frame.header().src = this->_ethernet_address;
        frame.header().dst = ETHERNET_BROADCAST;
        _frames_out.push(frame);
    }
    unsend_datagram[next_hop_ip].push_back(dgram);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if ((frame.header().dst != ETHERNET_BROADCAST) && (frame.header().dst != this->_ethernet_address)) return std::nullopt;
    if (frame.header().type == frame.header().TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) == ParseResult::NoError) return dgram;
    } else {
        ARPMessage msg;
        if (msg.parse(frame.payload()) != ParseResult::NoError) return std::nullopt;
        ip_arp[msg.sender_ip_address] = std::make_tuple(msg.sender_ethernet_address, 0);
        if (arp_requests.count(msg.sender_ip_address)) {
            for (auto dgram : unsend_datagram[msg.sender_ip_address]) {
                EthernetFrame _frame;
                _frame.header().type = _frame.header().TYPE_IPv4;
                _frame.header().src = this->_ethernet_address;
                _frame.header().dst = msg.sender_ethernet_address;
                _frame.payload() = dgram.serialize();
                _frames_out.push(_frame);
            }
            arp_requests.erase(msg.sender_ip_address);
            unsend_datagram.erase(msg.sender_ip_address);
        }
        if (msg.opcode == msg.OPCODE_REPLY) return std::nullopt;
        if (msg.target_ip_address != this->_ip_address.ipv4_numeric()) return std::nullopt;
        ARPMessage reply;
        reply.sender_ip_address = this->_ip_address.ipv4_numeric();
        reply.sender_ethernet_address = this->_ethernet_address;
        reply.target_ip_address = msg.sender_ip_address;
        reply.target_ethernet_address = msg.sender_ethernet_address;
        reply.opcode = reply.OPCODE_REPLY;
        EthernetFrame _frame;
        _frame.payload() = reply.serialize();
        _frame.header().type = _frame.header().TYPE_ARP;
        _frame.header().src = this->_ethernet_address;
        _frame.header().dst = msg.sender_ethernet_address;
        _frames_out.push(_frame);
    }
    return std::nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { 
    for (auto iter = ip_arp.begin(); iter != ip_arp.end(); ) {
        std::get<1>(iter->second) += ms_since_last_tick;
        if (std::get<1>(iter->second) <= 30000) iter++;
        else iter = ip_arp.erase(iter);
    }
    for (auto iter = arp_requests.begin(); iter != arp_requests.end(); ) {
        iter->second += ms_since_last_tick;
        if (iter->second <= 5000) iter++;
        else {
            unsend_datagram.erase(iter->first);
            iter = arp_requests.erase(iter);
        }
    }
}
