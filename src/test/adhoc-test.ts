import { setTimeout } from "timers/promises";

// import net = require("node:net");

import { events, net, startNodeAndJoinNet, zts } from "../index";
import * as util from "node:util";
import assert = require("node:assert");
import { fork } from "node:child_process";

async function test() {
  setTimeout(30_000, undefined, { ref: false }).then(() =>
    assert(false, "Test took too long"),
  );

  const payload = "abcdefgh";

  const server = process.argv.length < 3;
  if (server)
    console.log(`
This test starts a server, opens a client in a child process that connects to the server and tries to verify that cleanup of threads/exiting happens properly.
(Not the case right now.)
    `);

  const oldLog = console.log;
  const log = (...args: unknown[]) => {
    oldLog(server ? "S:    " : "   C: ", ...args);
  };
  console.log = log;

  log(`Process started with args: ${process.argv}`);

  const port = 5000;

  const nwid = "ff0000ffff000000";
  const address = await startNodeAndJoinNet(
    `./id/adhoc-test/${server ? "server" : "client"}`,
    nwid,
    (event) =>
      log(
        `       e: ${event}, ${events[event]
          .replace("ZTS_EVENT_", "")
          .toLowerCase()}`,
      ),
  );
  log(`Address: ${address}`);

  if (server) {
    log("Starting server");

    const server = new net.Server({}, (socket) => {
      log("socket connected");
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
        zts.node_free();
        log("freed node");
      });
      await setTimeout(500);
      server.close();
    });
  } else {
    const address = process.argv[2];
    log(`Connecting to: ${address}`);
    const s = net.connect(port, address, () => {
      log("Connected");
      s.write(Buffer.from(payload));

      let result = "";
      s.on("data", (data) => (result += data.toString()));
      s.on("end", () => {
        log(`ENDED, total received data: ${result}`);
        assert.strictEqual(result, payload);
      });

      s.on("close", async () => {
        setTimeout(500);
        log("socket closed, freeing node and hopefully exiting");
        zts.node_free();
      });
    });

    s.on("error", (err) => {
      assert(false, err);
    });
  }
}

test();
