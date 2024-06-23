import { net, startNodeAndJoinNet, node } from "../index";

async function main() {
  console.log(`
TCP example using ad-hoc network.

usage: <cmd> server
usage: <cmd> client <server ip>
    `);

  if (process.argv.length < 3) return;

  // handle cli arguments
  const server = process.argv[2] === "server";
  const serverIP = process.argv[3];

  // Setup the ZT node
  console.log("starting node");
  const nwid = "ff0000ffff000000";

  const addr = await startNodeAndJoinNet(
    `./id/${server ? "server" : "client"}`,
    nwid,
    (event) => console.log(event),
  );

  console.log(addr);

  // code for both server and client is almost drop-in an example from the nodejs documentation
  if (server) {
    // https://nodejs.org/dist/latest-v20.x/docs/api/net.html#netcreateserveroptions-connectionlistener
    const server = new net.Server({}, (c) => {
      // 'connection' listener.
      console.log("client connected");
      c.on("end", () => {
        console.log("client disconnected");
      });
      c.write("hello\r\n"); // "hello" is written to the client
      c.pipe(c); // socket is readable for receiving and writable for sending. This pipes the client to itself, i.e. a trivial echo server.
    });
    server.on("error", (err) => {
      throw err;
    });
    server.listen(8124, undefined, () => {
      console.log("server bound");
    });
  } else {
    // https://nodejs.org/dist/latest-v20.x/docs/api/net.html#netcreateconnectionoptions-connectlistener

    const client = net.connect(8124, serverIP, () => {
      // 'connect' listener.
      console.log("connected to server!");
      client.write("world!\r\n");
    });
    client.on("data", (data: Buffer) => {
      console.log(data.toString()); // data listener, received data is printed out
      client.end();
    });
    client.on("end", () => {
      console.log("disconnected from server");
      node.free();
    });
  }
}

main();
