## Environment variables

Sometimes you might have to use environment variables to control the behaviors of Galois. Environment variables can change the default values of fields in `cmdConf`, which is a `CmdLineConfig` instance.

The following environment variables are detectable by Galois:

* `CLUSTERCONF`: path to the cluster configuration file (default: `cluster.conf`).
* `PMEMDEV`: path to the persistent memory device (default: `/mnt/gjfs/sim0`).
* `PMEMSZ`: size (in bytes) of the persistent memory space (default: `1048576`).
* `PORT`: TCP port used by RDMA connections (default: `40345`).
* `RECOVER`: if set, and is not `NO` or `OFF`, Galois will try to recover its data from other nodes. Notice that it is CASE SENSITIVE!

If some Galois executable crashed unexpectedly, you might find that it cannot perform `rdma_bind_addr` when you run it again. Under such situations, you can change the port (on all nodes!) and try again. Also, if you want to test whether Galois can recover from an (injected) failure, you can set `RECOVER` to `ON` or other reasonable values. 

Because Galois needs super user privilege, it is recommended that you use `sudo -E`. For example, if you want to change the port to `44396` and then run the File Metadata Server, then `cd` to the binary directory (`build/bin`), and do as follows:

```sh
export PORT=44396
sudo -E ./FMServer
```
