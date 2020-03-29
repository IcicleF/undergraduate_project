## Prerequisites of Galois

Galois is tested in Ubuntu 18.04 LTS.

Before you start, make sure your computers have RDMA NIC. Otherwise, Galois will not work.

Several packages should be installed before compiling:

* A C++14 compiler (`clang++` is recommended)
* CMake 3.5+
* IB verbs & RDMA CM APIs
* GFlags
* Kyoto Cabinet
* Intel ISA-L
* FUSE 3
* Boost

We recommend that you follow these steps to install them:

1. Install packages by `apt-get` (as super user)

```sh
apt-get install build-essential cmake libibverbs-dev librdmacm-dev libgoogle-glog-dev libgoogle-perftools-dev libgflags-dev libboost-all-dev libkyotocabinet-dev
```

2. Install Intel ISA-L following instructions [here](https://github.com/intel/isa-l).
3. Install FUSE 3 following instructions [here](https://github.com/libfuse/libfuse).
