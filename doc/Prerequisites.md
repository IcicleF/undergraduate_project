## Prerequisites of Galois

Galois is tested in Ubuntu 18.04 LTS.

Several packages should be installed before compiling:

* A C++14 compiler (`clang++` is recommended)
* CMake 3.5+
* libibverbs
* librdmacm
* gflags
* glog
* gperftools
* RocksDB

We recommend that you run the following command (as super user) to install them:

```sh
apt-get install build-essential cmake libibverbs librdmacm-dev libgoogle-glog-dev libgoogle-perftools-dev libgflags-dev librocksdb-dev
```

### Notice

ISA-L and eRPC should also be installed. They are submodules of Galois.
