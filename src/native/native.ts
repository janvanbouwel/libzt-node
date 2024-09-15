import * as loadBinding from "pkg-prebuilds";
import * as options from "../binding-options";
import { SocketErrorCode } from "./enums";

module.exports = loadBinding(
  // project root
  `${__dirname}/../..`,
  options,
);

export interface ZtsError extends Error {
  code?: number;
  errno?: number;
}

export interface InternalError extends Error {
  code?: SocketErrorCode;
}

export interface AddrInfo {
  localAddr: string;
  localPort: number;
  localFamily: "IPv4" | "IPv6";
  remoteAddr: string;
  remotePort: number;
  remoteFamily: "IPv4" | "IPv6";
}

export declare function init_from_storage(path: string): void;
export declare function init_from_memory(key: Uint8Array): void;

export declare function node_start(callback: (event: number) => void): void;

export declare function node_is_online(): boolean;
export declare function node_get_id(): string;
export declare function node_free(): void;

export declare function ref(): void;
export declare function unref(): void;

export declare function net_join(nwid: string): void;
export declare function net_leave(nwid: string): void;
export declare function net_transport_is_ready(nwid: string): boolean;

export declare function addr_get_str(nwid: string, ipv6: boolean): string;

export declare class Socket {
  constructor();
  setEmitter(emit: (event: string, ...args: unknown[]) => boolean): void;
  connect(port: number, address: string): void;
  ack(length: number): void;
  send(data: Uint8Array): Promise<number>;
  shutdown_wr(): void;
  ref(): void;
  unref(): void;
  nagle(enable: boolean): void;
}

export declare class Server {
  address(): { port: number; address: string; family: "IPv6" | "IPv4" };
  close(): Promise<void>;
  ref(): void;
  unref(): void;
  static createServer(
    port: number,
    address: string,
    onConnection: (
      error: InternalError | undefined,
      socket: Socket,
      addrInfo: AddrInfo,
    ) => void,
  ): Promise<Server>;
}

export declare class UDP {
  constructor(
    ipv6: boolean,
    recvCallback: (data: Uint8Array, addr: string, port: number) => void,
  );

  send(data: Uint8Array, addr: string, port: number): Promise<void>;
  bind(addr: string, port: number): Promise<void>;
  close(): Promise<void>;

  address(): { port: number; address: string; family: "udp6" | "udp4" };
  remoteAddress(): { port: number; address: string; family: "udp6" | "udp4" };

  connect(addr: string, port: number): Promise<void>;
  disconnect(): void;

  ref(): void;
  unref(): void;
}
