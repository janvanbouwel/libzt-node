import { EventEmitter } from "events";
import { InternalError, zts } from "./zts";
import { isIPv6 } from "net";

interface UDPSocketEvents {
  close: () => void;
  message: (
    msg: Uint8Array,
    rinfo: {
      address: string;
      family: "udp4" | "udp6";
      port: number;
      size: number;
    },
  ) => void;
  listening: () => void;
  error: (err: InternalError) => void;
}

class Socket extends EventEmitter {
  private ipv6;
  private internal;
  private bound = false;
  private connected = false;
  private closed = false;

  constructor(ipv6: boolean) {
    super();
    this.ipv6 = ipv6;

    this.internal = new zts.UDP(ipv6, (data, addr, port) => {
      this.emit("message", data, {
        address: addr,
        family: isIPv6(addr) ? "udp6" : "udp4",
        port,
        size: data.length,
      });
    });
    // unbound socket should not hold process open
    this.unref();

    this.on("connect", () => (this.connected = true));
    this.once("listening", () => {
      this.bound = true;
      this.ref();
    });
  }

  ref() {
    this.internal.ref();
    return this;
  }

  unref() {
    this.internal.unref();
    return this;
  }

  address() {
    this.checkClosed();
    if (!this.bound) throw Error("Unbound socket");
    return this.internal.address();
  }

  remoteAddress() {
    this.checkClosed();
    if (!this.connected) throw Error("Socket not connected");
    return this.internal.remoteAddress();
  }

  bind(
    port?: number,
    address?: string,
    callback?: UDPSocketEvents["listening"],
  ) {
    this.checkClosed();
    if (this.bound) throw Error("Socket already bound");
    if (!address) address = "";
    if (!port) port = 0;

    if (callback) this.once("listening", () => callback());

    this.internal
      .bind(address, port)
      .then(() => this.emit("listening"))
      .catch((reason) => this.handleError()(reason));
  }

  send(
    msg: Uint8Array,
    port?: number,
    address?: string,
    callback?: (err?: InternalError) => void,
  ): void {
    this.checkClosed();
    if (this.connected) {
      port = 0;
    } else {
      if (!port) throw Error("Port must be specified on unconnected socket");
    }
    if (!address) address = this.ipv6 ? "::1" : "127.0.0.1";

    this.internal
      .send(msg, address, port)
      .then(() => {
        if (!this.bound) this.emit("listening");
        this.handleError(callback)();
      })
      .catch((reason) => this.handleError(callback)(reason));
  }

  close(callback?: () => void) {
    this.checkClosed();
    this.closed = true;
    if (callback) this.on("close", callback);
    this.internal.close().then(() => this.emit("close"));
  }

  connect(
    port: number,
    address: string,
    callback?: (error?: InternalError) => void,
  ) {
    this.checkClosed();
    if (!port) throw Error("Port must be specified");
    if (!address) address = this.ipv6 ? "::1" : "127.0.0.1";

    this.internal
      .connect(address, port)
      .then(() => {
        // if connect fails, shouldn't reuse callback for later try, so only add as listener here
        if (callback) this.once("connect", callback);
        this.emit("connect");
      })
      .catch((reason) => this.handleError(callback)(reason));
  }

  disconnect() {
    this.checkClosed();
    this.internal.disconnect();
    this.connected = false;
  }

  private handleError(callback?: (err?: InternalError) => void) {
    return (err?: Error) => {
      if (callback) callback(err);
      else {
        if (!err) return;

        if (!this.emit("error", err)) {
          throw err;
        }
      }
    };
  }

  private checkClosed() {
    if (this.closed) throw Error("Socket closed");
  }
}

export function createSocket(
  options: { type: "udp4" | "udp6" },
  callback?: UDPSocketEvents["message"],
) {
  const ipv6 = options.type === "udp6";
  const s = new Socket(ipv6);
  if (callback) s.on("message", callback);

  return s;
}
