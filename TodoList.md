# Todo List

## Critical

* 搞清楚 RDMA CM 的用法，改好 branch 上代码

* 读路径：读 k + Delta，取前 k 个解码
* 写路径：写到固定位置即可（一个虚拟地址对应一个物理位置）。如果有 failed node 那么进行降级写，node 恢复后读出期间哪些块被写了，逐块解码恢复。
  * 暂定 2PC，**需要创新**。
* Failure Detection

## Normal

* `ClusterConfig` 增加接口来按下标访问 DS 的 node ID。
* 不同的节点类型
* `MemoryConfig` 单独放在一个文件里
