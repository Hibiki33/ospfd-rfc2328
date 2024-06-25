#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <vector>

#include "utils.hpp"

namespace LSA {

/* LSA types. */
enum struct Type : uint8_t {
    ROUTER = 1,
    NETWORK,
    SUMMARY,
    ASBR_SUMMARY,
    AS_EXTERNAL
};

/* LSA header structure. */
struct Header {
    uint16_t age;
    uint8_t options;
    Type type;
    in_addr_t link_state_id;
    in_addr_t advertising_router;
    uint32_t sequence_number;
    uint16_t checksum;
    uint16_t length;

    /* Convert host to network byte order. */
    void host_to_network() noexcept {
        age = htons(age);
        link_state_id = htonl(link_state_id);
        advertising_router = htonl(advertising_router);
        sequence_number = htonl(sequence_number);
        checksum = htons(checksum);
        length = htons(length);
    }
    /* Convert network to host byte order. */
    void network_to_host() noexcept {
        age = ntohs(age);
        link_state_id = ntohl(link_state_id);
        advertising_router = ntohl(advertising_router);
        sequence_number = ntohl(sequence_number);
        checksum = ntohs(checksum);
        length = ntohs(length);
    }

    bool operator==(const Header& rhs) const {
        return type == rhs.type && link_state_id == rhs.link_state_id && advertising_router == rhs.advertising_router;
    }
} __attribute__((packed));

/* Router-LSA Link types. */
enum class LinkType : uint8_t {
    POINT2POINT = 1,
    TRANSIT,
    STUB,
    VIRTUAL
};

/* Base LSA structure. */
struct Base {
    Header header;
    virtual size_t size() const = 0;

    virtual void to_packet(char *packet) const {
        /* Copy the header. */
        memcpy(packet, &header, sizeof(Header));
        reinterpret_cast<Header *>(packet)->host_to_network();
    }

    virtual void make_checksum() = 0;

    bool operator<(const Base& rhs) const {
        assert(header.link_state_id == rhs.header.link_state_id);
        assert(header.advertising_router == rhs.header.advertising_router);
        if (header.sequence_number != rhs.header.sequence_number) {
            return header.sequence_number < rhs.header.sequence_number;
        }
        return header.checksum < rhs.header.checksum;
    }

    bool operator>(const Base& rhs) const {
        return rhs < *this;
    }
};

/* Router-LSA structure. */
struct Router : public Base {
    /* Router-LSA Link structure. */
    struct Link {
        in_addr_t link_id;
        in_addr_t link_data;
        LinkType type;
        uint8_t tos;
        uint16_t metric;

        Link() = default;
        Link(in_addr_t link_id, in_addr_t link_data, LinkType type, uint16_t metric)
            : link_id(link_id), link_data(link_data), type(type), tos(0), metric(metric) {
        }
        Link(char *net_ptr) {
            link_id = ntohl(*reinterpret_cast<in_addr_t *>(net_ptr));
            net_ptr += sizeof(link_id);
            link_data = ntohl(*reinterpret_cast<in_addr_t *>(net_ptr));
            net_ptr += sizeof(link_data);
            type = *reinterpret_cast<LinkType *>(net_ptr);
            net_ptr += sizeof(type);
            tos = 0;
            net_ptr += sizeof(tos);
            metric = ntohs(*reinterpret_cast<uint16_t *>(net_ptr));
        }
    } __attribute__((packed));

    uint16_t flags;
    uint16_t num_links;
    std::vector<Link> links;

    Router() = default;
    Router(char *net_ptr) {
        /* Parse the header. */
        header = *reinterpret_cast<Header *>(net_ptr);
        header.network_to_host();
        /* Parse the router-LSA Data. */
        net_ptr += sizeof(Header);
        flags = ntohs(*reinterpret_cast<uint16_t *>(net_ptr));
        net_ptr += sizeof(flags);
        num_links = ntohs(*reinterpret_cast<uint16_t *>(net_ptr));
        net_ptr += sizeof(num_links);
        for (auto i = 0; i < num_links; ++i) {
            // auto link = Link(net_ptr);
            links.emplace_back(net_ptr);
            net_ptr += sizeof(Link);
        }
    }

    size_t size() const override {
        return sizeof(Header) + sizeof(flags) + sizeof(num_links) + links.size() * sizeof(Link);
    }

    void to_packet(char *packet) const override {
        /* Initialize the packet. */
        // auto packet = new char[size()];

        Base::to_packet(packet);

        /* Copy the router-LSA Data. */
        auto net_ptr = packet + sizeof(Header);
        *reinterpret_cast<uint16_t *>(net_ptr) = htons(flags);
        net_ptr += sizeof(flags);
        *reinterpret_cast<uint16_t *>(net_ptr) = htons(num_links);
        net_ptr += sizeof(num_links);
        for (const auto& link : links) {
            *reinterpret_cast<in_addr_t *>(net_ptr) = htonl(link.link_id);
            net_ptr += sizeof(in_addr_t);
            *reinterpret_cast<in_addr_t *>(net_ptr) = htonl(link.link_data);
            net_ptr += sizeof(in_addr_t);
            *reinterpret_cast<LinkType *>(net_ptr) = link.type;
            net_ptr += sizeof(LinkType);
            *reinterpret_cast<uint8_t *>(net_ptr) = link.tos;
            net_ptr += sizeof(uint8_t);
            *reinterpret_cast<uint16_t *>(net_ptr) = htons(link.metric);
            net_ptr += sizeof(uint16_t);
        }
        // reinterpret_cast<Header *>(packet)->checksum =
        //     htons(fletcher16(packet + 2, header.length - 2, offsetof(Header, checksum)));
        // return packet;
    }

    void make_checksum() override {
        char *packet = new char[size()]; // alloc to avoid vla
        to_packet(packet);
        header.checksum = fletcher16(packet + 2, header.length - 2, 14);
        delete[] packet;
    }

    bool operator==(const Router& rhs) const {
        return header.link_state_id == rhs.header.link_state_id &&
               header.advertising_router == rhs.header.advertising_router &&
               header.sequence_number == rhs.header.sequence_number;
    }
};

/* Network-LSA structure. */
struct Network : public Base {
    in_addr_t network_mask;
    std::vector<in_addr_t> attached_routers;

    Network() = default;
    Network(char *net_ptr) {
        /* Parse the header. */
        header = *reinterpret_cast<Header *>(net_ptr);
        header.network_to_host();
        auto attr_end = net_ptr + header.length;
        /* Parse the network-LSA Data. */
        net_ptr += sizeof(Header);
        network_mask = ntohl(*reinterpret_cast<in_addr_t *>(net_ptr));
        net_ptr += sizeof(network_mask);
        while (net_ptr < attr_end) {
            attached_routers.emplace_back(ntohl(*reinterpret_cast<in_addr_t *>(net_ptr)));
            net_ptr += sizeof(in_addr_t);
        }
    }

    size_t size() const override {
        return sizeof(Header) + sizeof(network_mask) + attached_routers.size() * sizeof(in_addr_t);
    }

    void to_packet(char *packet) const override {
        /* Initialize the packet. */
        // auto packet = new char[size()];

        Base::to_packet(packet);

        /* Copy the network-LSA Data. */
        auto net_ptr = packet + sizeof(Header);
        *reinterpret_cast<in_addr_t *>(net_ptr) = htonl(network_mask);
        net_ptr += sizeof(in_addr_t);
        for (const auto& router : attached_routers) {
            *reinterpret_cast<in_addr_t *>(net_ptr) = htonl(router);
            net_ptr += sizeof(in_addr_t);
        }
        // reinterpret_cast<Header *>(packet)->checksum =
        //     fletcher16(packet + 2, header.length - 2, offsetof(Header, checksum));
        // return packet;
    }

    void make_checksum() override {
        char *packet = new char[size()]; // alloc to avoid vla
        to_packet(packet);
        header.checksum = fletcher16(packet + 2, header.length - 2, 14);
        delete[] packet;
    }

    bool operator==(const Network& rhs) const {
        return header.link_state_id == rhs.header.link_state_id &&
               header.advertising_router == rhs.header.advertising_router && header.length == rhs.header.length &&
               network_mask == rhs.network_mask && attached_routers == rhs.attached_routers;
    }
};

/* Summary-LSA structure. */
struct Summary : public Base {
    in_addr_t network_mask;
    // union {
    //     uint8_t tos;
    //     uint32_t metric;
    // };
    uint8_t tos;
    uint32_t metric; // 实际上是24位的一个字段，需要特殊处理

    Summary(char *net_ptr) {
        /* Parse the header. */
        header = *reinterpret_cast<Header *>(net_ptr);
        header.network_to_host();
        /* Parse the summary-LSA Data. */
        net_ptr += sizeof(Header);
        network_mask = ntohl(*reinterpret_cast<in_addr_t *>(net_ptr));
        net_ptr += sizeof(network_mask);
        tos = *reinterpret_cast<uint8_t *>(net_ptr);
        net_ptr += sizeof(tos);
        uint8_t metric_bytes[3];
        memcpy(metric_bytes, net_ptr, sizeof(metric_bytes));
        metric = metric_bytes[0] << 16 | metric_bytes[1] << 8 | metric_bytes[2]; // ATTN: check
    }

    size_t size() const override {
        return sizeof(Header) + sizeof(network_mask) + sizeof(metric);
    }

    void to_packet(char *packet) const override {
        /* Initialize the packet. */
        // auto packet = new char[size()];

        Base::to_packet(packet);

        /* Copy the summary-LSA Data. */
        auto net_ptr = packet + sizeof(Header);
        *reinterpret_cast<in_addr_t *>(net_ptr) = htonl(network_mask);
        net_ptr += sizeof(in_addr_t);
        *reinterpret_cast<uint8_t *>(net_ptr) = tos;
        net_ptr += sizeof(uint8_t);
        uint8_t metric_bytes[3] = {(uint8_t)(metric >> 16), (uint8_t)(metric >> 8), (uint8_t)(metric)};
        memcpy(net_ptr, metric_bytes, sizeof(metric_bytes)); // ATTN: check
        // reinterpret_cast<Header *>(packet)->checksum =
        //     fletcher16(packet + 2, header.length - 2, offsetof(Header, checksum));
        // return packet;
    }

    void make_checksum() override {
        char *packet = new char[size()]; // alloc to avoid vla
        to_packet(packet);
        header.checksum = fletcher16(packet + 2, header.length - 2, 14);
        delete[] packet;
    }
};

/* ASBR-summary-LSA structure. */
// ASBR-summary-LSA = Summary-LSA except for ls type

/* AS-external-LSA structure. */
struct ASExternal : public Base {
    in_addr_t network_mask;
    struct ExternRoute {
        uint8_t tos;
#define AS_EXTERNAL_FLAG 0x01
        uint32_t metric; // 同样是24位的一个字段
        in_addr_t forwarding_address;
        uint32_t external_router_tag;
    };

    std::vector<ExternRoute> e;

    ASExternal(char *net_ptr) {
        /* Parse the header. */
        header = *reinterpret_cast<Header *>(net_ptr);
        header.network_to_host();
        /* Parse the AS-external-LSA Data. */
        net_ptr += sizeof(Header);
        network_mask = ntohl(*reinterpret_cast<in_addr_t *>(net_ptr));
        net_ptr += sizeof(network_mask);
        while (net_ptr < net_ptr + header.length) {
            ExternRoute er;
            er.tos = *reinterpret_cast<uint8_t *>(net_ptr);
            net_ptr += sizeof(er.tos);
            uint8_t metric_bytes[3];
            memcpy(metric_bytes, net_ptr, sizeof(metric_bytes));
            er.metric = metric_bytes[0] << 16 | metric_bytes[1] << 8 | metric_bytes[2]; // ATTN: check
            net_ptr += sizeof(metric_bytes);
            er.forwarding_address = ntohl(*reinterpret_cast<in_addr_t *>(net_ptr));
            net_ptr += sizeof(er.forwarding_address);
            er.external_router_tag = ntohl(*reinterpret_cast<uint32_t *>(net_ptr));
            net_ptr += sizeof(er.external_router_tag);
            e.push_back(er);
        }
    }

    size_t size() const override {
        return sizeof(Header) + sizeof(network_mask) +
               e.size() * (sizeof(uint32_t) + sizeof(in_addr_t) + sizeof(uint32_t));
    }

    void to_packet(char *packet) const override {
        /* Initialize the packet. */
        // auto packet = new char[size()];

        Base::to_packet(packet);

        /* Copy the AS-external-LSA Data. */
        auto net_ptr = packet + sizeof(Header);
        *reinterpret_cast<in_addr_t *>(net_ptr) = htonl(network_mask);
        net_ptr += sizeof(in_addr_t);
        for (const auto& er : e) {
            *reinterpret_cast<uint8_t *>(net_ptr) = er.tos;
            net_ptr += sizeof(er.tos);
            uint8_t metric_bytes[3] = {(uint8_t)(er.metric >> 16), (uint8_t)(er.metric >> 8), (uint8_t)(er.metric)};
            memcpy(net_ptr, metric_bytes, sizeof(metric_bytes)); // ATTN: check
            net_ptr += sizeof(metric_bytes);
            *reinterpret_cast<in_addr_t *>(net_ptr) = htonl(er.forwarding_address);
            net_ptr += sizeof(er.forwarding_address);
            *reinterpret_cast<uint32_t *>(net_ptr) = htonl(er.external_router_tag);
            net_ptr += sizeof(er.external_router_tag);
        }
        // reinterpret_cast<Header *>(packet)->checksum =
        //     fletcher16(packet + 2, header.length - 2, offsetof(Header, checksum));
        // return packet;
    }
};

} // namespace LSA

using RouterLSA = LSA::Router;
using NetworkLSA = LSA::Network;
using SummaryLSA = LSA::Summary;
using ASBRSummaryLSA = LSA::Summary;
using ASExternalLSA = LSA::ASExternal;

namespace OSPF {

/* OSPF packet types. */
enum struct Type : uint8_t {
    HELLO = 1,
    DD,
    LSR,
    LSU,
    LSACK
};

/* OSPF packet header structure. */
struct Header {
    uint8_t version;
    Type type;
    uint16_t length;
    in_addr_t router_id;
    in_addr_t area_id;
    uint16_t checksum;
    uint16_t auth_type;
    uint64_t auth;

    /* Convert host to network byte order. */
    void host_to_network() noexcept {
        length = htons(length);
        router_id = htonl(router_id);
        area_id = htonl(area_id);
        checksum = htons(checksum);
        auth_type = htons(auth_type);
        auth = htonll(auth);
    }
    /* Convert network to host byte order. */
    void network_to_host() noexcept {
        length = ntohs(length);
        router_id = ntohl(router_id);
        area_id = ntohl(area_id);
        checksum = ntohs(checksum);
        auth_type = ntohs(auth_type);
        auth = ntohll(auth);
    }
} __attribute__((packed));

/* OSPF hello packet structure. */
struct Hello {
    in_addr_t network_mask;
    uint16_t hello_interval;
    uint8_t options;
    uint8_t router_priority;
    uint32_t router_dead_interval;
    in_addr_t designated_router;
    in_addr_t backup_designated_router;
    in_addr_t neighbors[0];

    /* Convert host to network byte order. */
    void host_to_network(size_t nbr_num) noexcept {
        network_mask = htonl(network_mask);
        hello_interval = htons(hello_interval);
        router_dead_interval = htonl(router_dead_interval);
        designated_router = htonl(designated_router);
        backup_designated_router = htonl(backup_designated_router);
        for (auto i = 0; i < nbr_num; ++i) {
            neighbors[i] = htonl(neighbors[i]);
        }
    }

    /* Convert network to host byte order. */
    void network_to_host() noexcept {
        network_mask = ntohl(network_mask);
        hello_interval = ntohs(hello_interval);
        router_dead_interval = ntohl(router_dead_interval);
        designated_router = ntohl(designated_router);
        backup_designated_router = ntohl(backup_designated_router);
        // 不转换neighbors
    }
} __attribute__((packed));

/* OSPF database description packet structure. */
struct DD {
    uint16_t interface_mtu;
    uint8_t options;
    uint8_t flags;
#define DD_FLAG_MS 0x01
#define DD_FLAG_M 0x02
#define DD_FLAG_I 0x04
#define DD_FLAG_ALL DD_FLAG_MS | DD_FLAG_M | DD_FLAG_I
    uint32_t sequence_number;
    LSA::Header lsahdrs[0];

    void host_to_network(size_t lsahdrs_num) noexcept {
        interface_mtu = htons(interface_mtu);
        sequence_number = htonl(sequence_number);
        for (auto i = 0; i < lsahdrs_num; ++i) {
            lsahdrs[i].host_to_network();
        }
    }

    void network_to_host() noexcept {
        interface_mtu = ntohs(interface_mtu);
        sequence_number = ntohl(sequence_number);
        // 不转换lsahdrs
    }
} __attribute__((packed));

/* OSPF link state request packet structure. */
struct LSR {
    struct Request {
        uint32_t ls_type;
        uint32_t link_state_id;
        uint32_t advertising_router;

        void host_to_network() noexcept {
            ls_type = htonl(ls_type);
            link_state_id = htonl(link_state_id);
            advertising_router = htonl(advertising_router);
        }

        void network_to_host() noexcept {
            ls_type = ntohl(ls_type);
            link_state_id = ntohl(link_state_id);
            advertising_router = ntohl(advertising_router);
        }

        bool operator==(const Request& rhs) const {
            return ls_type == rhs.ls_type && link_state_id == rhs.link_state_id &&
                   advertising_router == rhs.advertising_router;
        }
    } reqs[0];

    void host_to_network(size_t reqs_num) noexcept {
        for (auto i = 0; i < reqs_num; ++i) {
            reqs[i].host_to_network();
        }
    }

    void network_to_host() noexcept {
        // 不转换reqs
    }
} __attribute__((packed));

/* OSPF link state update packet structure. */
struct LSU {
    uint32_t num_lsas;
    // LSA是不定长的，很难在这里定义多个LSA
    // 将多个LSA的管理交给调用者

    void host_to_network() noexcept {
        num_lsas = htonl(num_lsas);
    }

    void network_to_host() noexcept {
        num_lsas = ntohl(num_lsas);
    }
} __attribute__((packed));

/* OSPF link state acknowledgment packet structure. */
struct LSAck {
    LSA::Header lsahdrs[0];
} __attribute__((packed));

void send_packet(Interface *intf, char *packet, size_t len, OSPF::Type type, in_addr_t dst);

size_t produce_hello(Interface *intf, char *body);
void process_hello(Interface *intf, char *ospf_packet, in_addr_t src_ip);

size_t produce_dd(char *body, Neighbor *nbr);
void process_dd(Interface *intf, char *ospf_packet, in_addr_t src_ip);

size_t produce_lsr(char *body, Neighbor *nbr);
void process_lsr(Interface *intf, char *ospf_packet, in_addr_t src_ip);

size_t produce_lsu(char *body, const std::list<LSA::Base *>& lsa_update_list);
void process_lsu(Interface *intf, char *ospf_packet, in_addr_t src_ip);

size_t produce_lsack(char *body, const std::list<LSA::Header *>& ls_summary_list);
void process_lsack(Interface *intf, char *ospf_packet, in_addr_t src_ip);

void flood_lsa(LSA::Base *lsa);

} // namespace OSPF
