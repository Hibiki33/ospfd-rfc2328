# 代码结构

## 文件结构
    
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

```shell
.
├── docs/
├── gns3/
├── makefile
├── readme.md
├── src/
│   ├── interface.cpp
│   ├── interface.hpp
│   ├── lsdb.cpp
│   ├── lsdb.hpp
│   ├── main.cpp
│   ├── neighbor.cpp
│   ├── neighbor.hpp
│   ├── packet.cpp
│   ├── packet.hpp
│   ├── route.cpp
│   ├── route.hpp
│   ├── transit.cpp
│   ├── transit.hpp
│   └── utils.hpp
├── style.sh
└── xmake.lua
```