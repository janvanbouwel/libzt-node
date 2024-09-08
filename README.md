# Nodejs libzt binding

Idiomatic Node.js bindings for [libzt](https://github.com/zerotier/libzt), intended to be a drop-in replacement for `node:net` and `node:dgram`.

This project is very early access, please open a github issue for bugs or feature requests.

## Installation

Prebuilt binaries are included with the npm package for a number of platforms, so installing should be as easy as:

```bash
npm install libzt
```

## Usage

### Initialisation

ZeroTier has to be initialised before it can be used. See also the official [libzt docs](https://docs.zerotier.com/sockets).

```ts
import { node } from "libzt";

// initialises node from storage, new id will be created if path doesn't exist
const nodeId = await node.start({
  path: "path/to/id",
  eventListener: (event) => console.log(event),
});

console.log(nodeId); // the node's id

const nwid = "ff0000ffff000000"; // Zerotier network id
await node.joinNetwork(nwid);

// get assigned address
const address = node.getIPv6Address(nwid); // or IPv4
```

### Sockets

As the end goal is feature parity with `node:net` and `node:dgram`, it should ideally be as simple as replacing

```ts
import net from "node:net";
import dgram from "node:dgram";
```

with

```ts
import { net, dgram } from "libzt";
```

Using ZeroTier sockets with other modules is also possible. For example for `node:http`, [inject a connection into a server using `httpServer.emit("connection", socket)`](https://nodejs.org/docs/latest/api/http.html#event-connection) and [use the `createConnection` option when creating a request](https://nodejs.org/docs/latest/api/http.html#httprequesturl-options-callback).

## License

The code for these bindings is licensed under the [ISC license](LICENSE). However, ZeroTier and libzt are licensed under the [BSL version 1.1](https://github.com/zerotier/libzt/blob/main/README.md#licensing) which limits certain commercial uses. See also [here](ext/libzt/ext/THIRDPARTY.txt) licenses for other included third party code.
