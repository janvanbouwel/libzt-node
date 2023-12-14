import { zts } from "./module/zts";

/**
 * Temporary, initialise the ZeroTierNode
 *
 * @param path if undefined, key is stored in memory (currently not retrievable)
 */
export function startNode(
  path?: string,
  eventHandler?: (event: number) => void,
) {
  if (path) zts.init_from_storage(path);
  if (eventHandler) zts.init_set_event_handler(eventHandler);

  zts.node_start();
}

export * from "./module/zts";
export * as dgram from "./module/dgram";
export * as net from "./module/net";
