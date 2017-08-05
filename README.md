## kadnode(1) - P2P name resolution daemon

## SYNOPSIS

`kadnode`  [--value-id identifier] [--port port] [--daemon] [...]

`kadnode-ctl`  [...]

## DESCRIPTION

**KadNode** is a small P2P DNS resolution daemon based on a Distributed Hash Table (DHT).
KadNode can intercept and resolve DNS request in the systems background.
Every instance can also announce any domain like e.g. myname.p2p. People on others
devices running KadNode can then enter myname.p2p into their browsers or console in order
to reach the announcing host. To avoid name clashes, cryptographic public/private key pairs can be used.
This makes it possible to have a personal and decentralized DynDNS service.
The [DHT](https://github.com/jech/dht) is identical to the one used in the Transmission Bittorrent client and works'on the Internet as well as on local networks.

Features:

* IPv4/IPv6 support
* TLS support (e.g. can use browser CA chain)
* Plain public key links as <public-hex-key>.p2p
* UPnP/NAT-PMP support
* local peer discovery
* small size, ~70KB depending on features
* public/secret key authentication
* command line interface (kadnode-ctl)
* NSS support through /etc/nsswitch.conf
* integrated simplified DNS server and proxy (handles A, AAAA, and SRV requests)
* packages for ArchLinux/Debian/FreeBSD/MacOSX/LEDE/Windows
* peer file import/export on startup/shutdown and every 24h

Crypto provided by [mbedtls](https://github.com/ARMmbed/mbedtls/), also used by OpenWrt.

Most features are optional and can be left out to reduce the binary size.


## JOIN THE SWARM

KadNode needs to know at least one active peer to join / bootstrap into the swarm.
There are three ways to archieve this:

1. Provide one or more peers to the command line arguments. These could be public BitTorrent trackers, or other KadNode instances:
```
kadnode --peer bttracker.debian.org --peer 192.168.1.1
```

2. Use the local peer discovery feature. Just start kadnode and it will try to discover other node in the local network.

3. Ping a node using the KadNode console if present:
```
kadnode-ctl ping bttracker.debian.org
```

Also provide a --peerfile argument to let KadNode backup its peerlist on shutdown and every 24h.
This ensures successful boostrapping on the next startup.


## AUTHENTICATION

KadNode provides two authentication schemes. One works via TLS. The other one uses raw secret/public keys and is hence called bob, because ... it is simple.

### Via TLS

Typically there are two KadNode instances involved.

One node announcing a hostname, e.g. mynode.p2p. The other one looking for the announcing IP address.

```
kadnode --tls-server-cert mynode.crt,mynode.key
```

mynode.crt needs to have the common name field set to 'mynode.p2p'.
This files are used to announce mynode.p2p on the network and to prove ownership.

As alternative, ownerhip can be proven by a https server running on the same host.
In this case, kadnode only needs to announce the hostname:

```
kadnode --announce mynode.p2p
```

The other node doing the lookup for mynode.p2p needs to have access to the root certificate that has been used to sign mynode.crt. These can be a common web browsers certificate authorities.

```
kadnode --tls-client-cert /usr/share/ca-certificates/mozilla
```

Of course you can create your own certificate authority.

### Via BOB

First create an elliptic curve secret key file:

```
kadnode --bob-create-key mysecretkey.pem
Generating secp256r1 key pair...
Public key: c492192ac20144ed2a43d57e7239f5ef5f6bb418a51600980e55ff565cc916a4
Wrote secret key to mysecretkey.pem
```

Now load the secret key on KadNode startup:
```
kadnode --bob-load-key mysecretkey.pem
```

Other nodes can now resolve c492192ac20144ed2a43d57e7239f5ef5f6bb418a51600980e55ff565cc916a4.p2p to the ipaddress of the announcing host without the need to share a secret key beforehand.

## OPTIONS

  * `--announce` *name[:port]*  
    Announce a domain. The domain is expected to authenticate on port 443 via TLS (e.g. HTTPS).
    This option may occur multiple times.

  * `--peerfile` *file*  
    Import peers for bootstrapping and write good peers  
    to this file every 24 hours and on shutdown.

  * `--user` *name*  
    Change the UUID after start.

  * `--port` *port*  
    Bind the DHT to this port (Default: 6881).  

  * `--config` *file*  
    Provide a configuration file with one command line  
    option on each line. Comments start after '#'.

  * `--ifname` *interface*  
    Bind to this specific interface.

  * `--fwd-disable`  
    Disable UPnP/NAT-PMP to forward router ports.

  * `--daemon`  
    Run in background.

  * `--query-tld` *domain*  
    Top level domain used to filter queries to be resolved by KadNode. (Default: ".p2p")

  * `--verbosity` *level*  
    Verbosity level: quiet, verbose or debug (Default: verbose).

  * `--pidfile` *file-path*  
    Write process pid to a file.

  * `--lpd-addr` *address*  
    Send LPD packets to this multicast address as long no peers were found.  
    Default: 239.192.152.143:6771 / [ff15::efc0:988f]:6771

  * `--lpd-disable`  
    Disable Local Peer Discovery (LPD).

  * `--cmd-disable-stdin`  
    Disable the local control interface.

  * `--cmd-port` *port*  
    Bind the remote control interface to this local port (Default: 1700).

  * `--dns-port` *port*  
    Bind the DNS server interface to this local port (Default: 3535).

  * `--dns-server` *address*  
    IP address of an external DNS server. Enables DNS proxy mode (Default: none).

  * `--dns-proxy-enable`  
    Enable DNS proxy mode. Reads /etc/resolv.conf by default.

  * `--dns-proxy-server` *ip-address*  
    Use IP address of an external DNS server instead of resolv.conf.

  * `--nss-port` *port*  
    Bind the "Name Service Switch" to this local port (Default: 4053).

  * `--tls-client-cert` *path*  
    Path to file or folder of CA root certificates.  
    This option may occur multiple times.

  * `--tls-server-cert` *tuple*  
    Add a comma separated tuple of server certificate file and key.  
    The certificates Common Name is announced.
    This option may occur multiple times.  
    Example: kadnode.crt,kadnode.key

  * `--bob-create-key` *file*  
    Write a new secp256r1 secret key in PEM format to the file.  
    The public key will be printed to the terminal before exit.

  * `--bob-load-key` *file*  
    Read a secret key in PEM format and announce the public key.  
    This option may occur multiple times.

  * `--ipv4, -4, --ipv6, -6`  
    Enable IPv4 or IPv6 only mode for the DHT (Default: IPv4+IPv6).

  * `-h`, `--help`  
    Print the list of accepted options.

  * `-v`, `--version`  
    Print program version and included features.

### KadNode Console Commands

When not started in background, KadNode accepts a variety of commands from standard input.

  * `status`  
    Print the node id, the number of known nodes / searches / stored hashes and more.

  * `lookup` *query*  
    Lookup the IP addresses of all nodes that claim to satisfy the query.  
    The first call will start the search.

  * `announce` [*query*[<i>:*port*</i>] [<i>*minutes*</i>]]  
    Announce that this instance is associated with a query  
    and an optional port. The default port is random (but not equal 0).  
    No *minutes* trigger a single announcement. Negative *minutes*  
    last for the entire runtime. Otherwise the lifetime is set *minutes* into the future.  
    No arguments will announce all identifiers at once.

  * `import` *addr*  
    Send a ping to another KadNode instance to establish a connection.

  * `export`  
    Print a few good nodes.

  * `list` [`blacklist`|`buckets`|`constants`|`forwardings`|`results`|`searches`|`storage`|`values`]  
    List various internal data structures.

  * `blacklist` *addr*  
    Blacklist a specifc IP address.

## KadNode External Console

KadNode allows a limited set of commands to be send from any user from other consoles.

`kadnode-ctl` [-p port] [status|lookup|announce|import|export|blacklist]

  * `-p` *port*  
    The port used to connect to the command shell of a local KadNode instance (Default: 1700).

  * `-h`  
    Print this help.

## PORT FORWARDINGS

If KadNode runs on a computer in a private network, it will try to establish a port forwarding for the DHT port and ports used for announcements.
Port forwarding only works if UPnP/NAT-PMP is compiled into KadNode and is supported by the gateway/router.
This is useful to make a local service (e.g. a web server) reachable from the Internet without the need to
configure port forwardings manually.

## LICENSE

  MIT/X11

## AUTHORS

  * KadNode: Moritz Warning (http://github.com/mwarning)
  * Kademlia: Juliusz Chroboczek
