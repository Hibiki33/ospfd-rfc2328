#include <iostream>

#include "netinet/ip.h"
#include "packet.hpp"

int main(int argc, char **argv) {
    std::cout << sizeof(iphdr) << std::endl;
    return 0;
}
