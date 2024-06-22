# 邻接

## 邻居状态机

完整的邻居状态机如图下面两张图片所示：

由Hello报文触发：
![](./imgs/neighbor_state_machine_hello.png)

由数据库更新触发：
![](./imgs/neighbor_state_machine_db.png)

## 建立邻接关系

建立邻接关系的核心是`Hello`报文的处理。

通过`Hello`报文发现邻接路由器，邻居状态机从`Down`进入`Init`状态。

再收到`Hello`报文后，如果邻居的`Hello`报文中包含自己，就根据网络类型进入`2-Way`或`ExStart`状态。前者代表不需要建立**邻接**关系，会保持`2-way`状态并持续等待`adj_ok?`事件；后者代表需要建立**邻接**关系，接口会进行选举。

## 同步数据库

### Exstart和Exchange状态

在`ExStart`状态下，选举出Master和Slave，这里rfc2328写的并不是很详细。在初始的时候，会给邻接数据结构一个设定的`dd_seq_num`值。

主从关系的确立的核心**认可主从关系**。对两个路由器而言，最初都认为自己是Master。这时如果一个路由器发现对方发送的、带有I、M、MS标志位的`DD`报文的`Router ID`比自己的大，就会认可自己是Slave，并将自己的`dd_seq_num`改为对方的值，进入`Exchange`状态；如果不然，这个`DD`报文**会被忽略**，这个时候未来会成为Master的这个路由器**并没有认可自己为Master**。

在Slave认可主从关系后，会回应一个不带有I、MS标志位的`DD`报文，这个报文（一般）**会带有自己数据库的摘要信息**，即`LSA Header`（这一点需要抓包才能看出来，标准中略过了这里的细节）。路由器接收到这个不带有I、MS标志位的`DD`报文，并且发现其中的`dd_seq_num`和自己相同（Slave已经认可了主从关系），且对方`Router ID`小于自己，就认可自己为Master，进入`Exchange`状态。需要注意的是，需要处理这个`DD`报文中的摘要信息。

在`Exchange`阶段，只有Master能够主动发送`DD`报文，并且超时重传。Slave只能在接收到Master的`DD`报文后立即回复。而且，Master只有在收到了希望接收的`DD`报文后才能发送下一个`DD`报文，否则等待超时重传。相应的是，如果两边报文都合理接收的话，这一阶段是没有等待的。

需要注意的是，当Slave已经收到了不含M标志位的报文，在发送`DD`报文时发现没有`LSA`需要发送，就会进入`Loading`状态或`Full`状态。而Master在收到不含M标志位的报文后，发现自己的`db_summary_list`为空，就会进入`Loading`或`Full`状态。S此处lave状态切换会先于Master。

