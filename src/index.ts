import * as node from "./module/node";

/**
 * Temporary, initialise the ZeroTierNode
 *
 * @param path if undefined, key is stored in memory (currently not retrievable)
 */
export async function startNode(
  path?: string,
  eventHandler?: (event: number) => void,
) {
  if (path) node.initFromStorage(path);
  if (eventHandler) node.setEventHandler(eventHandler);

  await node.start();
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
  eventHandler?: (event: number) => void,
): Promise<string> {
  await startNode(path, eventHandler);

  await node.joinNetwork(nwid);

  try {
    return node.getIPv6Address(nwid);
  } catch (error) {
    return node.getIPv4Address(nwid);
  }
}

export { events, SocketErrors } from "./module/zts";
export { node };
export * as dgram from "./module/dgram";
export * as net from "./module/net";
