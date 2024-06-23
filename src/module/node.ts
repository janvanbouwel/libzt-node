import { setTimeout } from "timers/promises";
import { zts } from "./zts";
import { node } from "..";

// INIT

export function initFromStorage(path: string) {
  if (state !== NodeState.INIT) {
    throw Error("Can only init node when stopped");
  }
  zts.init_from_storage(path);
}

export function initFromMemory(key: Uint8Array) {
  if (state !== NodeState.INIT) {
    throw Error("Can only init node when stopped");
  }
  zts.init_from_memory(key);
}

enum NodeState {
  INIT,
  STOPPED,
  STARTED,
  FREED,
}

let state: NodeState = NodeState.INIT;
let onEvent: (event: number) => void = () => undefined;

export function setEventHandler(callback: (event: number) => void) {
  onEvent = callback;
}

export async function start(
  opts: { ref: boolean } = { ref: true },
): Promise<string> {
  switch (state) {
    case NodeState.STARTED:
      throw Error("Node already started");
    case NodeState.FREED:
      throw Error("Node already freed");
  }

  if (state === NodeState.INIT) {
    zts.init_set_event_handler((event) => {
      onEvent(event);
    });
  }
  state = NodeState.STARTED;
  zts.node_start();
  if (!opts.ref) zts.unref();

  while (!zts.node_is_online()) {
    await setTimeout(50);
  }

  return zts.node_get_id();
}

export function stop() {
  if (state !== NodeState.STARTED) {
    throw Error("Node was not running");
  }

  state = NodeState.STOPPED;
  zts.node_stop();
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

export function leaveNetwork(nwid: string) {
  if (state !== NodeState.STARTED) {
    throw Error("Node was not started");
  }
  zts.net_leave(nwid);
}
