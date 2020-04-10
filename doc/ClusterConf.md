## Cluster Configuration

Cluster configuration is stored in `cluster.conf` in the root directory of the repository.

This file will be automatically copied to the binary directory by CMake, and will be used by Galois (under default settings).

The configuration file contains several **records**, with each **record** in a single line. A record's format is as follows:

```
<node-id> <hostname> <node-ip> <nic-ip> <node-type>
```

* `node-id` gives each node a unique ID. It is **REQUIRED** that they be natural numbers starting from 0 and increasing by 1 (i.e. 0, 1, 2, 3, ...).
* `hostname` is the host name of each node. *This field is currently useless.*
* `node-ip` is the node's IPv4 address. **IPv6 addresses are not supported.**
* `nic-ip` is the node's RDMA NIC's IPv4 address. You should see this IP by typing `ifconfig` in the node's terminal. 
* `node-type` is the node's type. It should be one of the followings:
  - `DMS`: LocoFS Data Metadata Server
  - `FMS`: LocoFS File Metadata Server
  - `DS`: LocoFS Data Server
  - `CLI`: LocoFS Client
