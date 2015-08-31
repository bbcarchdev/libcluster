# libcluster

This is a library for implementing both statically-configured and dynamic
clusters of nodes which need to be able to identify themselves within the
cluster. Each node can be single- or multi-threaded (or multi-process).

The API design is such that a cluster connection object is created
(with `cluster_create()`), has callbacks and parameters set based upon
the application's configuration, and then the cluster is joined with
`cluster_join()`. The application can then proceed with its normal processing,
during which libcluster will invoke a callback whenever it determines that
the cluster shape has changed (i.e., because nodes have joined or departed).

An example cluster of three nodes (with two, one, and four threads
respectively) might look like this:-

```
+-------+----------------+--------+
| Index | Node           | Thread |
+-------+----------------+--------+ 
|     0 | node1          | 0      |
|     1 | node1          | 1      |
|     2 | node2          | 0      |
|     3 | node3          | 0      |
|     4 | node3          | 1      |
|     5 | node3          | 2      |
|     6 | node3          | 3      |
+-------+----------------+--------+
```

Thus in this cluster, there are a total of seven threads.

The aim of libcluster is for each thread within the cluster to be able to
obtain this total, its cluster-unique index, and be notified when either
changes. In this example, the second thread of node3 is 4/7, while the only
thread of node2 is 2/7. With a consistent snapshot of the combination of
cluster-wide thread index and total thread count, applications can divide
work between nodes.

Applications using libcluster do not have to be multi-threaded, although
depending upon the clustering type in use, it may launch and manage its own
threads. Single-threaded applications are considered by libcluster to be an
application which has a single worker thread.

Cluster connections are configured with:

* A cluster *key*: an identifier which identifies the cluster (typically the application name)
* A cluster *environment*: an identifier which differentiates different environments (e.g., `production`, `dev`, etc.)
* An instance *identifier*: a value unique to this specific instance (by default, a UUID is generated for the instance, but applications can override this)
* An instance *thread count*: the number of worker threads or sub-processes this instance is responsible for (by default, this is set to one, but applications which use worker thread or sub-process pools can specify different values)

For static clusters (that is, clusters where nodes do not directly co-ordinate,
but the same fixed configuration is applied to all members of the cluster),
each node is also configured with:

* The base thread index: the numeric index from (0..total-1) of the first thread of this node within the cluster.
* The total thread count: the total number of threads in the whole cluster.

For dynamic clusters, libcluster uses [etcd](https://github.com/coreos/etcd)
to coordinate membership and determine the thread indices and totals. Rather
than statically configuring the base thread index and total count as with
static clusters, applications instead provide the base URI of the etcd server
(or proxy).

When configured in this fashion, each node launches 'ping' and 'balancer'
threads. The ping thread periodically writes to a key in an etcd directory,
where the key's name is the instance identifier and the key's value is the
per-instance thread count. The key's TTL is set so that if the application
unexpectedly terminates, the entry will eventually vanish.

Meanwhile, the 'balancer' thread monitors the same etcd directory for changes.
When they occur, the list of instances is retrieved and sorted, and the
'balancing' callback provided by the application is invoked if either the
base thread index or total thread count have changed.

When using etcd-based clustering, the directory that libcluster uses is
`/v2/keys/CLUSTER-KEY/CLUSTER-ENV` relative to the supplied registry URI.

See `cluster-test.c` for a complete example of how to work with the API.

## Limitations

Cluster re-balancing may not occur at precisely the same time on every node,
for a variety of factors. Applications should therefore ensure that the thread
index is treated as _should-be unique_: in other words, under normal
circumstances conflicts should not occur, but steps should be taken to avoid
race conditions in operations which depend upon them. For example, if multiple
nodes use the thread index as a key in a database update, the update should
be wrapped in a transaction with appropriate rollback and retry mechanisms.

As the cost of implementing this is generally minimal, a normally-operating
cluster can divide work extremely efficiently, while still providing
safety during a re-balancing event.

## Example - using `cluster-test`

Using `cluster-test`, one can simulate a cluster of as many nodes as might
be desirable. `cluster-test` does not actually perform any work itself (that
is, it simply sleeps and waits to be terminated), but invokes the libcluster
API as a real-world application.

`cluster-test` has the following usage:

```
Usage: cluster-test [OPTIONS]

OPTIONS are one or more of:
  -h                        Print this message and exit
  -v                        Be more verbose
  -k KEY                    Set the cluster key to KEY
  -e ENV                    Set the cluster environment to ENV
  -i ID                     Set the instance identifier to ID
  -n COUNT                  Set the number of threads to COUNT
 etcd-based clustering:
  -r URI                    Set the cluster registry URI
 Static clustering:
  -I INDEX                  Set this instance index to INDEX
  -T COUNT                  Set the cluster total to COUNT
```

Assuming an etcd server or proxy running on `127.0.0.1:2379`, you might invoke
`cluster-test` as follows:

```
$ cluster-test -v -k mycluster -e dev -r http://127.0.0.1:2379/
libcluster<7>: libcluster: etcd: re-balancing cluster mycluster/dev:
libcluster<7>: * 2014f53242aa4b56b5292bb755123436 [0]
libcluster<5>: libcluster: etcd: cluster mycluster/dev has re-balanced: new base is 0 (was -1), new total is 1 (was 0)
libcluster<7>: libcluster: re-balanced; this instance has index 0 (1 threads) from a total of 1
cluster-test: cluster has re-balanced:
   instance index:        0
   instance thread count: 1
   total thread count:    1
cluster-test: cluster joined; sleeping until terminated
libcluster<7>: libcluster: etcd: re-balancing thread started for mycluster/dev at <http://127.0.0.1:2379/>
libcluster<7>: libcluster: etcd: ping thread starting with ttl=120, refresh=30
libcluster<7>: libcluster: etcd: re-balancing cluster mycluster/dev:
libcluster<7>: * 2014f53242aa4b56b5292bb755123436 [0]
```

(Some `libcurl` output has been removed from the above transcript for brevity)

In the example above, `2014f53242aa4b56b5292bb755123436` was the
automatically-generated unique identifier for this node.

In another window, you can then run a second copy of `cluster-test` with the
same parameters:

```
$ cluster-test -v -k mycluster -e dev -r http://127.0.0.1:2379/
libcluster<7>: libcluster: etcd: re-balancing cluster mycluster/dev:
libcluster<7>:   2014f53242aa4b56b5292bb755123436 [0]
libcluster<7>: * 976f8500b22d4e7e9db3468b97d677cd [1]
libcluster<5>: libcluster: etcd: cluster mycluster/dev has re-balanced: new base is 1 (was -1), new total is 2 (was 0)
libcluster<7>: libcluster: re-balanced; this instance has index 1 (1 threads) from a total of 2
cluster-test: cluster has re-balanced:
   instance index:        1
   instance thread count: 1
   total thread count:    2
cluster-test: cluster joined; sleeping until terminated
libcluster<7>: libcluster: etcd: re-balancing thread started for mycluster/dev at <http://127.0.0.1:2379/>
libcluster<7>: libcluster: etcd: ping thread starting with ttl=120, refresh=30
libcluster<7>: libcluster: etcd: re-balancing cluster mycluster/dev:
libcluster<7>:   2014f53242aa4b56b5292bb755123436 [0]
libcluster<7>: * 976f8500b22d4e7e9db3468b97d677cd [1]
```

Here, `976f8500b22d4e7e9db3468b97d677cd` is the unique identifier for the
second instance. The asterisk next to the identifier indicates that it
corresponds to the current instance. The number in square brackets is the
base thread index for that instance.

In the first window, we can see that the initial node is aware of the second
instance:

```
libcluster<7>: libcluster: etcd: re-balancing cluster mycluster/dev:
libcluster<7>: * 2014f53242aa4b56b5292bb755123436 [0]
libcluster<7>:   976f8500b22d4e7e9db3468b97d677cd [1]
libcluster<5>: libcluster: etcd: cluster mycluster/dev has re-balanced: new base is 0 (was 0), new total is 2 (was 1)
libcluster<7>: libcluster: re-balanced; this instance has index 0 (1 threads) from a total of 2
cluster-test: cluster has re-balanced:
   instance index:        0
   instance thread count: 1
   total thread count:    2
```

Both instances will continue running (doing essentially nothing) until you
press Ctrl+C. When you do, the instance will be removed from the cluster
and the other instances will re-balance themselves accordingly. In the first
window (assuming this is first one that you terminate):

```
^Ccluster-test: signal received, will terminate
cluster-test: will now leave the cluster
libcluster<7>: libcluster: etcd: 'leaving' flag has been set, will terminate ping thread
libcluster<7>: libcluster: etcd: ping thread is terminating
libcluster<7>: libcluster: etcd: wait result was 0
libcluster<7>: libcluster: etcd: reading state from registry directory
libcluster<7>: libcluster: etcd: re-balancing cluster cluster-test/production:
libcluster<7>:   976f8500b22d4e7e9db3468b97d677cd [0]
libcluster<5>: libcluster: etcd: this instance is no longer a member of cluster-test/production
libcluster<7>: libcluster: re-balanced; this instance has index -1 (1 threads) from a total of 1
cluster-test: cluster has re-balanced:
   instance index:        -1
   instance thread count: 1
   total thread count:    1
libcluster<7>: libcluster: etcd: 'leaving' flag has been set, will terminate balancing thread
libcluster<7>: libcluster: etcd: balancing thread is terminating
cluster-test: successfully left the cluster
```

Meanwhile, in the second, as the first instance terminates:

```
libcluster<7>: libcluster: etcd: wait result was 0
libcluster<7>: libcluster: etcd: reading state from registry directory
libcluster<7>: libcluster: etcd: re-balancing cluster cluster-test/production:
libcluster<7>: * 976f8500b22d4e7e9db3468b97d677cd [0]
libcluster<5>: libcluster: etcd: cluster cluster-test/production has re-balanced: new base is 0 (was 1), new total is 1 (was 2)
libcluster<7>: libcluster: re-balanced; this instance has index 0 (1 threads) from a total of 1
cluster-test: cluster has re-balanced:
   instance index:        0
   instance thread count: 1
   total thread count:    1
libcluster<7>: libcluster: etcd: waiting for changes to cluster-test/production
```

You can also try the `-n` option to `cluster-test`, which specifies the thread
count that is passed to libcluster. For example, three instances which match
the arrangement given at the beginning of this document could be started with:

```
$ cluster-test -i node1 -n 2 -r http://127.0.0.1:2379/
```

```
$ cluster-test -i node2 -r http://127.0.0.1:2379/
```

```
$ cluster-test -i node3 -n 4 -r http://127.0.0.1:2379/
```

Note that `cluster-test` does not actually create the specified number of
worker threads - because it does not really perform any work, it only passes
the value to libcluster to adjust the instance/thread numbering.

The above commands would result in output similar to the following, if invoked
one-by-one:-

```
libcluster<7>: libcluster: etcd: re-balancing cluster cluster-test/production:
libcluster<7>: * node1 [0]
libcluster<7>:   node2 [2]
libcluster<7>:   node3 [3]
libcluster<5>: libcluster: etcd: cluster cluster-test/production has re-balanced: new base is 0 (was 0), new total is 7 (was 3)
libcluster<7>: libcluster: re-balanced; this instance has index 0 (2 threads) from a total of 7
cluster-test: cluster has re-balanced:
   instance index:        0
   instance thread count: 2
   total thread count:    7
```

This transcript was from `node1`, after both `node2` and `node3` were started.
We can see that the base index of `node2` is `2`, because `node1` has thread
indices `0` and `1`; meanwhile `node3` has thread indices `3..6`, giving a
total thread count of `7`.
