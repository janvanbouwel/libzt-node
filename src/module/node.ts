import { setTimeout } from "timers/promises";
import { zts } from "./zts";

// INIT

interface NodeStartOpts {
  /**
   * If specified, libzt will use this folder to read and write its identity and cached networks, peers... Will be created if it doesn't exist.
   */
  path?: string;
  /**
   * If specified, libzt will use the provided key as its identity.
   * TODO: investigate how this interacts with path.
   */
  key?: Uint8Array;
  /**
   * Whether the libzt node will prevent the node process from exiting. (This only applies to the core libzt node and not to any opened sockets, which have their own ref option.)
   * Default: false
   */
  ref?: boolean;
  /**
   * This callback receives info about libzt's events. API highly subject to change to become more idiomatic.
   * @param event
   * @returns
   */
  eventListener?: (event: number) => void;
}

enum NodeState {
  INIT,
  STARTED,
  FREED,
}

let state: NodeState = NodeState.INIT;
let onEvent: (event: number) => void = () => undefined;

export function setEventHandler(callback: (event: number) => void) {
  onEvent = callback;
}

export async function start(opts?: NodeStartOpts): Promise<string> {
  if (state !== NodeState.INIT) {
    throw Error("Node has already been started or freed.");
  }

  if (opts) {
    if (opts.key) {
      zts.init_from_memory(opts.key);
    }
    if (opts.path) {
      zts.init_from_storage(opts.path);
    }
    if (opts.eventListener) {
      onEvent = opts.eventListener;
    }
  }

  state = NodeState.STARTED;
  zts.node_start((event) => {
    onEvent(event);
  });
  if (opts && opts.ref === true) zts.ref();

  while (!zts.node_is_online()) {
    await setTimeout(50);
  }

  return zts.node_get_id();
}

export function free() {
  if (state === NodeState.INIT) {
    throw Error("Node was never started");
  }

  if (state !== NodeState.FREED) {
    state = NodeState.FREED;
    zts.node_free();
  }
}

export function ref() {
  if (state !== NodeState.STARTED) {
    throw Error("Can only ref when running");
  }
  zts.ref();
}

export function unref() {
  if (state !== NodeState.STARTED) {
    throw Error("Can only ref when running");
  }
  zts.unref();
}

export function id(): string {
  return zts.node_get_id();
}

// NETWORK

export async function joinNetwork(nwid: string) {
  if (state !== NodeState.STARTED) {
    throw Error("Node was not started");
  }
  zts.net_join(nwid);
  while (!zts.net_transport_is_ready(nwid)) {
    await setTimeout(50);
  }
}

export function leaveNetwork(nwid: string) {
  if (state !== NodeState.STARTED) {
    throw Error("Node was not started");
  }
  zts.net_leave(nwid);
}

export function getIPv4Address(nwid: string) {
  if (state !== NodeState.STARTED) {
    throw Error("Node was not started");
  }
  return zts.addr_get_str(nwid, false);
}

export function getIPv6Address(nwid: string) {
  if (state !== NodeState.STARTED) {
    throw Error("Node was not started");
  }
  return zts.addr_get_str(nwid, true);
}
