## Programming rules when using Galois

### Order of initializing global configurations

`cmdConf`, `memConf`, `clusterConf` and `myNodeConf` are 4 global configuration structures that need initializing.

You MUST initialize them in the following order:

* `cmdConf` first, manually.
* `memConf` then, by initializing an `ECAL` instance (at constructor).
* `clusterConf` and `myNodeConf` finally, by initializing an `RPCInterface` instance (at constructor, will be automatically done by `ECAL` constructor)

In practice, you can manually new a `CmdLineConfig` instance, give it to `cmdConf`, and instantiate an `ECAL`. Then you may operate on Galois as you like.

### Not invoking any RPCs before ECAL constructs

Generally the instantiation of one and only one `ECAL` is the best way to warm up the whole Galois system. Please notice that it is PROHIBITED to invoke any RPCs before the `ECAL` instance is fully constructed (i.e. the constructor safely exited). Otherwise, your RPCs can collide with recovery process and cause unrecoverable errors.

Of course, it's also your freedom to not to use ECAL.
