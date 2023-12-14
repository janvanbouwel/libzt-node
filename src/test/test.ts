import { setTimeout } from "timers/promises";

import { startNode, zts, dgram, net } from "../index";

async function main() {
  console.log(`
Might be outdated.
Uses ad-hoc network.

usage: <cmd> udp|tcp server|client <port> <server ip>
    `);
  if (process.argv.length < 3) return;

  const protocol = process.argv[2];
  const server = process.argv[3] === "server";
  const port = parseInt(process.argv[4]);
  const host = process.argv[5];

  console.log("starting node");
  startNode(undefined, (event) => console.log(event));

  console.log("waiting for node to come online");
  while (!zts.node_is_online()) {
    await setTimeout(50);
  }

  console.log(zts.node_get_id());

  const nwid = "ff0000ffff000000";

  zts.net_join(nwid);

  while (!zts.net_transport_is_ready(nwid)) {
    await setTimeout(50);
  }

  try {
    console.log(zts.addr_get_str(nwid, true));
  } catch (error) {
    console.log(error);
  }

  if (protocol === "udp") {
    if (server) {
      const s = dgram.createSocket({ type: "udp6" }, (msg, rinfo) => {
        console.log(`${msg}`);
        console.log(rinfo);
        s.send(msg, rinfo.port, rinfo.address);
      });
      s.bind(port);
      console.log(s.address());
    } else {
      const s = dgram.createSocket({ type: "udp6" }, (msg, rinfo) => {
        console.log(`${msg}`);
        console.log(rinfo);
      });
      s.on("error", (err) => {
        console.log(err);
      });
      s.bind(port);
      console.log(s.address());
      // eslint-disable-next-line no-constant-condition
      while (true) {
        console.log("sending");
        const msg = Buffer.from("abcdefg");
        s.send(msg, port, host, () => console.log("sent"));
        await setTimeout(1000);
      }
    }
  } else if (protocol === "tcp") {
    if (server) {
      console.log("starting server");
      const server = new net.Server({}, (socket) => {
        console.log(
          `new connection ${
            //socket.remoteAddress
            undefined
          }`,
        );
        socket.on("data", (data) => {
          console.log(`${data}`);
          socket.write(data);
        });
        socket.on("error", () => console.log("error"));
      });
      server.listen(port, "::", () => {
        console.log(server.address());
        console.log("listening");
      });
      server.on("error", (err) => console.log(err));
    } else {
      const s = net.connect(port, host);

      let i = 0;
      // s.setTimeout(1000);
      s.on("connect", () => {
        console.log("connected");
        s.write(Buffer.from("ping" + i));
        i++;
      });

      s.on("data", async (data) => {
        console.log(`${data}`);
        await setTimeout(100);

        s.write(Buffer.from("ping" + i));
        i++;
      });

      s.on("error", (err) => console.log(err));
    }
  }
}

main();
