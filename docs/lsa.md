# 链路状态报告

## LSA Header

所有LSA都有一个相同的Header结构，用于描述LSA的基本信息。

在不同的LSA中，`link_state_id`的含义不同。在Router LSA中，`link_state_id`为生成该LSA对应的**路由器ID**；在Network LSA中，`link_state_id`为该网段**DR的接口IP地址**。

```cpp
struct Header {
    uint16_t age;
    uint8_t options;
    Type type;
    in_addr_t link_state_id;
    in_addr_t advertising_router;
    uint32_t sequence_number;
    uint16_t checksum;
    uint16_t length;

    void host_to_network() noexcept;
    void network_to_host() noexcept;
} __attribute__((packed));
```

## Router LSA

由每个配置了ospf的路由器生成，描述该路由器接入该区域的接口，称为`Link`。

本实验中，所有网络类型均为`Transit`，因此`link_id`为DR的接口IP地址，`link_data`为关联（对端）**接口的IP地址**。在用最短路径计算路由时，需要**通过`link_id`找到对应的Network LSA，再通过`link_data`找到对应的路由器ID**。`metric`为该链路的代价，即路径计算中的权重。

```cpp
struct Router : public Base {
    /* Router-LSA Link structure. */
    struct Link {
        in_addr_t link_id;
        in_addr_t link_data;
        LinkType type;
        uint8_t tos;
        uint16_t metric;

        Link(char *net_ptr);
    };

    uint16_t flags;
    uint16_t num_links;
    std::vector<Link> links;

    Router(char *net_ptr);
    size_t size() const override;
    void to_packet(char *packet) const override;
    bool operator==(const Router& rhs) const;
};
```

## Network LSA

由DR生成，描述该网段的接入路由器。Router LSA没有直接描述网络。可以通过Network LSA找到对应的**路由器ID**。

```cpp
/* Network-LSA structure. */
struct Network : public Base {
    in_addr_t network_mask;
    std::vector<in_addr_t> attached_routers;

    Network(char *net_ptr);
    size_t size() const override;
    void to_packet(char *packet) const override;
    bool operator==(const Network& rhs) const;
};
```