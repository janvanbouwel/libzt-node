import { setTimeout } from "timers/promises";

// import net = require("node:net");

import { ZtsNodeEvent, net, node, startNodeAndJoinNet } from "../index";
import * as util from "node:util";
import assert = require("node:assert");
import { fork } from "node:child_process";

async function test() {
  setTimeout(50_000, undefined, { ref: false }).then(() =>
    assert(false, "Test took too long"),
  );

  const payload = "abcdefgh";

  const runServer = process.argv.length < 3;
  if (runServer)
    console.log(`
This test starts a server, opens a client in a child process that connects to the server and tries to verify that cleanup of threads/exiting happens properly.
    `);

  const oldLog = console.log;
  const log = (...args: unknown[]) => {
    oldLog(runServer ? "S:    " : "   C: ", ...args);
  };
  console.log = log;

  log(`Process started with args: ${process.argv}`);

  const port = 5000;

  const nwid = "ff0000ffff000000";
  const address = await startNodeAndJoinNet(undefined, nwid, (event) =>
    log(
      `       e: ${event}, ${ZtsNodeEvent[event]
        .replace("ZTS_EVENT_", "")
        .toLowerCase()}`,
    ),
  );
  log(`Address: ${address}`);

  if (runServer) {
    log("Starting server");

    const server = new net.Server({}, (socket) => {
      log("socket connected");
      log("local addr: ", socket.address());
      log(
        "remote addr: ",
        socket.remoteAddress,
        socket.remotePort,
        socket.remoteFamily,
      );
      socket.once("data", (data) => {
        log(`Received data: ${data}`);
        log(`Sending data:  ${data}`);
        socket.end(data);
      });
      socket.on("end", () => log("socket ended"));
      socket.on("close", () => log("socket closed"));
      socket.on("error", (error) => {
        throw error;
      });
    });
    server.unref();
    server.listen(port, "::", () => {
      log(`Listening, address: ${util.format(server.address())}`);
    });

    // server.on("error", (err)=>assert(false, `Server errored: ${util.format(err)}`));

    await setTimeout(1000);

    const child = fork(__filename, [address]);
    child.on("exit", async (code) => {
      log(`Client exited with code ${code}`);
      assert.strictEqual(code, 0);

      server.on("close", () => {
        log("closed server");
      });
      await setTimeout(500);
      // server.close((err) => {
      //   if (err) {
      //     console.log(`server closed with error`);
      //     console.log(err);
      //   }
      // });
    });
  } else {
    const address = process.argv[2];
    log(`Connecting to: ${address}`);
    const client = net.connect(port, address, () => {
      log("Connected");
      log("local addr: ", client.address());
      log(
        "remote addr: ",
        client.remoteAddress,
        client.remotePort,
        client.remoteFamily,
      );

      client.write(Buffer.from(payload));

      let result = "";
      client.on("data", (data) => (result += data.toString()));
      client.on("end", () => {
        log(`ENDED, total received data: ${result}`);
        assert.strictEqual(result, payload);
      });

      client.on("close", async () => {
        setTimeout(500);
        log("socket closed");
      });
    });

    client.on("error", (err) => {
      assert(false, err);
    });
  }
}

test();
