#pragma once

#include <atomic>
#include <cstdint>

#include "packet.hpp"

class Interface;

namespace OSPF {

extern int recv_fd;

#ifndef IPPROTO_OSPF
#define IPPROTO_OSPF 89
#endif

void recv_loop();
void send_loop();

extern std::atomic<bool> running;

constexpr const char *ALL_SPF_ROUTERS = "224.0.0.5";
constexpr const char *ALL_DR_ROUTERS = "224.0.0.6";

} // namespace OSPF
