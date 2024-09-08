import * as node from "./module/node";

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
  eventListener?: (event: number) => void,
): Promise<string> {
  await node.start({ path, eventListener });

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
