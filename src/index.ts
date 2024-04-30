import { setTimeout } from "node:timers/promises";
import { zts } from "./module/zts";

/**
 * Temporary, initialise the ZeroTierNode
 *
 * @param path if undefined, key is stored in memory (currently not retrievable)
 */
export function startNode(
  path?: string,
  eventHandler?: (event: number) => void
) {
  if (path) zts.init_from_storage(path);
  if (eventHandler) zts.init_set_event_handler(eventHandler);

  zts.node_start();
}

/**
 * Starts zt node, joins a network and returns ipv6 or else ipv4 address.
 * @param path 
 * @param nwid 
 * @param eventHandler 
 * @returns 
 */
export async function startNodeAndJoinNet(
  path: string | undefined,
  nwid: string,
  eventHandler?: (event: number) => void
): Promise<string> {
  startNode(path, eventHandler);
  while (!zts.node_is_online()) {
    await setTimeout(50);
  }
  zts.net_join(nwid);
  while (!zts.net_transport_is_ready(nwid)) {
    await setTimeout(50);
  }

  try {
    return zts.addr_get_str(nwid, true)
  } catch (error) {
    return zts.addr_get_str(nwid, false)
  }
}

export * from "./module/zts";
export * as dgram from "./module/dgram";
export * as net from "./module/net";
