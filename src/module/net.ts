import { EventEmitter } from "node:events";
import {
  AddrInfo,
  InternalError,
  InternalServer,
  InternalSocket,
  SocketErrors,
  zts,
} from "./zts";
import { Duplex, DuplexOptions, PassThrough } from "node:stream";

import * as node_net from "node:net";
import { checkPort } from "./util";
import { setTimeout } from "node:timers/promises";

export class Server extends EventEmitter implements node_net.Server {
  listening = false;

  private internalServer?: InternalServer;
  connections!: number; // wrong in @types/node, not used
  maxConnections!: number; // can be set to configure but normally undefined

  private connAmount = 0;

  constructor(
    options: Record<string, never>,
    connectionListener?: (socket: Socket) => void,
  ) {
    super();
    this.once("close", () => (this.connAmount = -1));
    if (connectionListener) this.on("connection", connectionListener);
  }

  private internalConnectionHandler(
    error: InternalError | undefined,
    socket: InternalSocket,
    addrInfo: AddrInfo,
  ): void {
    if (
      this.maxConnections !== undefined &&
      this.maxConnections <= this.connAmount
    ) {
      //socket.
      // TODO terminate incoming connection (after obtaining address)
      // TODO local address
      this.emit("drop", {});
      return;
    }

    const s = new Socket({}, socket, addrInfo);

    this.connAmount++;
    s.once("close", () => {
      this.connAmount--;
      if (!this.internalServer && this.connAmount === 0) {
        this.emit("close");
      }
    });
    process.nextTick(() => this.emit("connection", s));
  }

  private async internalListen(port: number, host: string): Promise<void> {
    try {
      this.internalServer = await zts.Server.createServer(
        port,
        host,
        this.internalConnectionHandler.bind(this),
      );
      this.listening = true;
      this.emit("listening");
    } catch (error: unknown) {
      // TODO translate to nodejs error, for example
      //  reason.code === SocketErrors.ERR_USE -> reason.code = "EADDRINUSE"
      this.handleError(error);
    }
  }

  address() {
    return this.internalServer?.address() ?? null;
  }

  [Symbol.asyncDispose](): Promise<void> {
    return new Promise((resolve) => this.close(() => resolve()));
  }

  listen(
    port?: number,
    hostname?: string,
    backlog?: number,
    listeningListener?: () => void,
  ): this;
  listen(
    port?: number,
    hostname?: string,
    listeningListener?: () => void,
  ): this;
  listen(port?: number, backlog?: number, listeningListener?: () => void): this;
  listen(port?: number, listeningListener?: () => void): this;
  listen(path: string, backlog?: number, listeningListener?: () => void): this;
  listen(path: string, listeningListener?: () => void): this;
  listen(options: node_net.ListenOptions, listeningListener?: () => void): this;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  listen(handle: any, backlog?: number, listeningListener?: () => void): this;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  listen(handle: any, listeningListener?: () => void): this;
  listen(...args: unknown[]): this {
    if (this.listening) throw Error("Server already listening");

    let options: node_net.ListenOptions & {
      port: number;
      callback?: () => void;
    } = { port: 0 };

    const arg0 = args[0];
    if (arg0 && typeof arg0 === "object") {
      // listen options
      if (!checkPort(arg0))
        throw Error("Port must be specified if options object is used.");
      options = arg0;
      const arg1 = args[1];
      if (typeof arg1 === "function") options.callback = arg1 as () => void;
    } else {
      options = extractListenArgs(args);
    }

    if (options.callback) this.once("listening", options.callback);

    if (options.signal && !options.signal.aborted)
      options.signal.onabort = () => this.close();
    // TODO backlog and other node_net.ListenOptions (ipv4/ipv6)

    this.internalListen(options.port, options.host ?? "::");

    return this;
  }

  close(callback?: ((err?: Error | undefined) => void) | undefined): this {
    this.listening = false;
    if (!this.internalServer) {
      if (callback)
        this.once("close", () => callback(Error("ERR_SERVER_NOT_RUNNING")));

      this.emit("close");
    } else {
      this.internalServer.close().then(() => {
        if (this.connAmount === 0) this.emit("close");
      });
      this.internalServer = undefined;
    }
    return this;
  }

  getConnections(_cb: (error: Error | null, count: number) => void): this {
    _cb(null, this.connAmount);
    return this;
  }

  ref(): this {
    this.internalServer?.ref();
    return this;
  }
  /*
   * TODO ref only works while the server is listening.
   * Additionally, if a server is unref'd, then closed and then starts listening again, it will be ref'd again
   */
  unref(): this {
    if (this.internalServer) this.internalServer.unref();
    else this.once("listening", () => this.internalServer?.unref());
    return this;
  }

  private handleError(error: unknown) {
    if (!this.emit("error", error)) throw error;
  }
}

export function createServer(
  connectionListener?: (socket: Socket) => void,
): Server {
  return new Server({}, connectionListener);
}

/**
 *
 */

class Socket extends Duplex /*implements node_net.Socket*/ {
  private internalEvents = new EventEmitter();
  private internalSocket: InternalSocket;

  private connected = false;

  localAddress?: string | undefined;
  localPort?: number | undefined;
  localFamily?: string | undefined;
  remoteAddress?: string | undefined;
  remotePort?: number | undefined;
  remoteFamily?: string | undefined;

  bytesRead = 0;
  bytesWritten = 0;
  bytesAcked = 0;

  // internal socket writes to receiver, receiver writes to duplex (which is read by client application)
  private receiver = new PassThrough();

  constructor(
    options: node_net.SocketConstructorOpts,
    internal?: InternalSocket,
    addrInfo?: AddrInfo,
  ) {
    super({
      allowHalfOpen: options.allowHalfOpen ?? false,
      readable: options.readable,
      writable: options.writable,
    } as DuplexOptions);

    if (internal) this.connected = true;
    if (addrInfo) this.setAddrInfo(addrInfo);
    this.internalSocket = internal ?? new zts.Socket();

    // events from native socket
    this.internalEvents.on("connect", (addrInfo: AddrInfo) => {
      this.connected = true;
      this.setAddrInfo(addrInfo);
      this.emit("connect");
    });
    this.internalEvents.on("data", (data?: Uint8Array) => {
      if (data) {
        // console.log(`received ${data.length}`);

        this.bytesRead += data.length;
        this.receiver.write(data, undefined, () => {
          // console.log(`acking ${data.length}`);
          this.internalSocket.ack(data.length);
        });
      } else {
        // other side closed the connection
        this.receiver.end();
        if (this.readableLength === 0) this.resume();
      }
    });
    this.internalEvents.on("sent", (length) => {
      // console.log(`internal has sent ${length}`);
      this.bytesAcked += length;
    });
    this.internalEvents.on("close", () => {
      // TODO: is this actually necessary?
      // console.log("internal socket closed");
    });
    this.internalEvents.on("error", (error: InternalError) => {
      console.log(error);
      this.destroy(error);
    });

    this.internalSocket.setEmitter((event: string, ...args: unknown[]) =>
      this.internalEvents.emit(event, ...args),
    );

    // setup passthrough for receiving data
    this.receiver.on("data", (chunk) => {
      if (!this.push(chunk)) this.receiver.pause();
    });
    this.receiver.on("end", () => {
      this.push(null);
      // if (this.receiver.readableLength === 0 && this.receiver.isPaused()) this.receiver.resume();
    });
    this.receiver.pause();
  }

  protected setAddrInfo(addrInfo: AddrInfo) {
    this.localAddress = addrInfo.localAddr;
    this.localPort = addrInfo.localPort;
    this.localFamily = addrInfo.localFamily;
    this.remoteAddress = addrInfo.remoteAddr;
    this.remotePort = addrInfo.remotePort;
    this.remoteFamily = addrInfo.remoteFamily;
  }

  _read() {
    this.receiver.resume();
  }

  _write(
    chunk: Uint8Array,
    _: unknown,
    callback: (error?: Error | null) => void,
  ): void {
    if (!this.connected)
      this.once("connect", () => this.realWrite(chunk, callback));
    else this.realWrite(chunk, callback);
  }

  private async realWrite(
    chunk: Uint8Array,
    callback: (error?: Error | null) => void,
  ): Promise<void> {
    const currentAcked = this.bytesAcked;
    const length = await this.internalSocket.send(chunk);
    this.bytesWritten += length;

    // everything was written out
    if (length === chunk.length) {
      callback();
      return;
    }

    // not everything was written out
    const continuation = () => {
      this.realWrite(chunk.subarray(length), callback);
    };

    // new space became available in the time it took to sync threads
    if (currentAcked !== this.bytesAcked) continuation();
    // wait for more space to become available
    else this.internalEvents.once("sent", continuation);
  }

  _final(callback: (error?: Error | null | undefined) => void): void {
    if (this.connected) {
      this.internalEvents.once("close", callback);
      this.internalSocket.shutdown_wr();
    } else {
      this.internalSocket.shutdown_wr();
      callback();
    }
  }

  connect(
    options: node_net.TcpSocketConnectOpts,
    connectionListener?: () => void,
  ): this {
    if (this.connected) throw Error("Already connected");

    if (connectionListener) this.once("connect", connectionListener);

    this._connect(options.port, options.host ?? "127.0.0.1", 120);
    return this;
  }

  private _connect(port: number, address: string, attempts: number) {
    this.internalEvents.once("connect_error", async (err: number) => {
      if (err === SocketErrors.ERR_RTE && attempts > 0) {
        await setTimeout(250);
        this._connect(port, address, attempts - 1);
      } else {
        const error = Error("Socket connect error");
        (error as unknown as { code: number }).code = err;
        // TODO: cleanup tsfn properly
        this.destroy(error);
      }
    });

    this.internalSocket.connect(port, address);
  }

  ref(): this {
    this.internalSocket.ref();
    return this;
  }

  unref(): this {
    this.internalSocket.unref();
    return this;
  }

  address(): node_net.AddressInfo | {} {
    if (this.localAddress) {
      return {
        address: this.localAddress,
        port: this.localPort,
        family: this.localFamily,
      };
    } else return {};
  }

  setTimeout(timeout: number, callback?: () => void): this {
    throw Error("not yet implemented"); // TODO
  }

  setNoDelay(noDelay: boolean = true): this {
    this.internalSocket.nagle(!noDelay);
    return this;
  }

  setKeepAlive(enable?: boolean, initialDelay?: number): this {
    throw Error("not yet implemented"); // TODO
  }

  resetAndDestroy(): this {
    throw Error("not yet implemented"); // TODO
  }

  destroySoon(): void {
    throw Error("not yet implemented"); // TODO
  }
} // class Socket

function _createConnection(
  options: node_net.TcpNetConnectOpts,
  connectionListener?: () => void,
): Socket {
  const socket = new Socket(options);

  process.nextTick(() => {
    socket.connect(options, connectionListener);
  });

  return socket;
} // class Socket

export function createConnection(
  options: node_net.NetConnectOpts,
  connectionListener?: () => void,
): Socket;
export function createConnection(
  port: number,
  host?: string,
  connectionListener?: () => void,
): Socket;
export function createConnection(
  path: string,
  connectionListener?: () => void,
): Socket;
export function createConnection(
  port: number | string | node_net.NetConnectOpts,
  host?: string | (() => void),
  connectionListener?: () => void,
): Socket {
  if (typeof port === "string") throw Error("Only TCP sockets are possible");
  if (typeof port === "number") {
    return typeof host === "string"
      ? _createConnection({ port, host }, connectionListener)
      : _createConnection({ port }, host);
  } else {
    // port is object
    if (typeof host === "string") throw Error("Argument");
    if (!("port" in port)) throw Error("Only TCP sockets are possible");

    return _createConnection(port, host);
  }
}

export const connect = createConnection;

/*
 * UTIL
 */

function extractListenArgs(args: unknown[]) {
  const options: {
    port: number;
    host?: string;
    backlog?: number;
    callback?: () => void;
  } = {
    port: 0,
  };
  if (args.length === 0) {
    return options;
  }
  let index = 0;

  let arg = args[index++];
  if (args.length >= index && typeof arg === "number") {
    options.port = arg;
    arg = args[index++];
  }

  if (args.length >= index && typeof arg === "string") {
    options.host = arg;
    arg = args[index++];
  }

  if (args.length >= index && typeof arg === "number") {
    options.backlog = arg;
    arg = args[index++];
  }

  if (args.length >= index && typeof arg === "function") {
    options.callback = arg as () => void;
  }

  return options;
}
