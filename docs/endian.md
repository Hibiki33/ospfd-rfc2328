# 网络协议字节序问题

x86架构的CPU是小端字节序，而网络协议是大端字节序。在发送和接收数据时需要进行字节序的转换。

## 字节序转换函数

在 `<netinet/in.h>` 中有4个函数，其中 **n** 指 **network**，**h** 指 **host**。

```c
uint16_t htons(uint16_t netshort);
uint32_t htonl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
uint32_t ntohl(uint32_t netlong);
```