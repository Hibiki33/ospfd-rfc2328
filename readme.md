# OSPF 

## Overview

本项目是“北京航空航天大学-计算机网络实验-提高层次实验”的代码，实现了OSPFv2协议的基本功能，包括邻接关系的建立、链路状态数据库的同步、最短路径和路由表的计算等，基于RFC 2328标准实现。并在模拟环境和实验室环境进行了组网测试。

## Structure

本项目的文件和代码结构如下：

- `./docs`：文档
- `./gns3`：GNS3配置文件
- `./src`：OSPF实现源码
    - `packet`：各类OSPF报文和LSA数据结构、收发报文处理
    - `interface`：接口数据结构、接口状态和事件
    - `lsdb`：链路状态数据库类
    - `neighbor`：邻接数据结构、邻接状态和事件
    - `route`：路由表数据结构、路由表更新、最短路算法
    - `transit`：recv和send线程
    - `utils`：工具函数
- `xmake.lua`和`makefile`：编译配置文件

## Acknowledgements

- [RFC-2328](./docs/rfc2328.txt)
- [frr-ospfd](https://github.com/FRRouting/frr/tree/master/ospfd)
- [quagga-ospfd](https://github.com/Quagga/quagga/tree/master/ospfd)
- [RuOSPFd](https://github.com/Xlucidator/RuOSPFd/tree/master)