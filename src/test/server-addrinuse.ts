import { setTimeout } from "timers/promises";
import { net, startNode, startNodeAndJoinNet } from "../index";

const main = async () => {
  console.log(await startNodeAndJoinNet("id/server", "ff0000ffff000000", (event) =>
    console.log(event)
  ));

  const server1 = net
    .createServer(()=>console.log("socket on 1"))
    .listen(5000, () => console.log("listening 1"))
    .on("error", (err) => console.log(err.message));
  await setTimeout(1000);
  const server2 = net
    .createServer(()=>console.log("socket on 2"))
    .listen(5000, () => console.log("listening 2"))
    .on("error", (err) => console.log(err.message));
};

main();
