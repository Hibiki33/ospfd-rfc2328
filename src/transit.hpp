#pragma once

#include <atomic>
#include <cstdint>

#include "packet.hpp"

class Interface;

namespace OSPF {

void send_packet(const char *data, size_t len, OSPF::Type type, in_addr_t dst, Interface *intf);

void recv_loop();

void send_loop();

extern std::atomic<bool> running;

constexpr const char* OSPF_ALL_SPF_ROUTERS = "224.0.0.5";
constexpr const char* OSPF_ALL_DR_ROUTERS = "224.0.0.6";

} // namespace OSPF
