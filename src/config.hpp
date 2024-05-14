#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <netinet/in.h>

inline const char *this_name = "ens33";

#ifndef OSPF_VERSION
#define OSPF_VERSION 2
#endif

inline const in_addr_t this_router_id = ntohl(inet_addr("0.0.0.0"));