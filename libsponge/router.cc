#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
    _forward_table.push_back(std::make_tuple(route_prefix, prefix_length, next_hop, interface_num));
    // Your code here.
}

bool Router::can_match(uint32_t prefix, uint8_t prefix_length, uint32_t ip_addr) {
    uint32_t mask = UINT32_MAX << (32 - prefix_length);
    if ((ip_addr & mask) == prefix) return true;
    return false;
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    if ((dgram.header().ttl == 0) || (dgram.header().ttl == 1)) return ;
    uint32_t target_ip_addr = dgram.header().dst;
    uint8_t longest_prefix_length = 0;
    size_t _interface = 0;
    std::optional<Address> _next_hop{};
    //! Transfer IP Datagram to default router when it's in the forward table 
    //! and no route prefix can match the target ip address.
    for (auto &&[route_prefix, prefix_length, next_hop, interface_num] : _forward_table) {
        if (interface_num != 0) continue;
        _next_hop = next_hop;
        break;
    }
    for (auto &&[route_prefix, prefix_length, next_hop, interface_num] : _forward_table) {
        if (!can_match(route_prefix, prefix_length, target_ip_addr)) continue;
        if (prefix_length > longest_prefix_length) {
            longest_prefix_length = prefix_length;
            _interface = interface_num;
            _next_hop = next_hop;
        }
    }
    dgram.header().ttl--;
    interface(_interface).send_datagram(dgram, _next_hop.has_value() ? _next_hop.value() : Address::from_ipv4_numeric(dgram.header().dst));
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
