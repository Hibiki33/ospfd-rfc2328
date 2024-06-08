#pragma once

#include <list>
#include <vector>

#include <net/if.h>
#include <netinet/in.h>

class Neighbor;

/*
 * OSPF接口用以连接路由器和网络：
 * - 假设每个OSPF接口接入各自的网络/子网；
 * - 可以认为接口属于包含其接入网络的区域；
 * - 由路由器发出的LSA反映其状态和相关联的邻接。
 */
class Interface {
public:
    /* 接口的类型 */
    enum class Type {
        P2P = 1,
        BROADCAST,
        NBMA,
        P2MP,
        VIRTUAL
    } type = Type::BROADCAST;

    /* 接口的功能层次 */
    enum class State {
        DOWN,
        LOOPBACK,
        WAITING,
        POINT2POINT,
        DROTHER,
        BACKUP,
        DR
    } state = State::DOWN;

    /* 接口ip地址 */
    in_addr_t ip_addr;
    /* 接口子网掩码 */
    in_addr_t mask;
    /* 区域标识 */
    uint32_t area_id;

    /* 从该接口发送Hello报文的时间间隔 */
    uint32_t hello_interval = 10;
    /* 当不再收到路由器的Hello包后，宣告邻居断开的时间间隔 */
    uint32_t router_dead_interval = 40;
    /* 向该接口的邻接重传LSA的时间间隔 */
    uint32_t rxmt_interval = 5;
    /* 接口上发送一个LSU包所需要的大致时间 */
    uint32_t intf_trans_delay = 1;

    /* 路由器优先级 */
    uint8_t router_priority = 1;

    /* Hello计时器 */
    uint32_t hello_timer;
    /* Wait计时器 */
    uint32_t wait_timer;

    /* 该接口的邻接路由器 */
    std::list<Neighbor *> neighbors;

    /* 选举出的DR */
    in_addr_t designated_router;
    /* 选举出的BDR */
    in_addr_t backup_designated_router;

    /* 接口输出值，即在Router-LSA中宣告的连接状态距离值 */
    uint32_t cost = 1;

    /* 验证类型 */
    uint16_t auth_type;
    /* 验证密码 */
    uint64_t auth_key;

    /* 接口名称 */
    char name[IFNAMSIZ];

public:
    /* 改变接口状态的事件 */
    void event_interface_up();
    void event_wait_timer();
    void event_backup_seen();
    void event_neighbor_change();
    void event_loop_ind();
    void event_unloop_ind();
    void event_interface_down();

    /* 管理邻居 */
    // Neighbor *add_neighbor(in_addr_t ip_addr);
    // void remove_neighbor(in_addr_t ip_addr);
    // void clear_neighbors();
    // Neighbor *get_neighbor(in_addr_t ip_addr);

    Interface() = default;
    Interface(in_addr_t ip_addr, in_addr_t mask, uint32_t area_id)
        : ip_addr(ip_addr), mask(mask), area_id(area_id) {
    }
    ~Interface();

private:
    /* 选举DR和BDR */
    void elect_designated_router();
};

extern std::vector<Interface *> this_interfaces;
constexpr const int MAX_INTERFACE_NUM = 16;

void init_interfaces();