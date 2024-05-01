import { EventEmitter } from "node:events";
import { NativeError, nativeSocket, zts } from "./zts";
import { Duplex, DuplexOptions, PassThrough } from "node:stream";

import * as node_net from "node:net";

export class Server extends EventEmitter {
  private listening = false;

  private closed = false;
  private internal;
  private connAmount = 0;

  constructor(
    options: Record<string, never>,
    connectionListener?: (socket: Socket) => void,
  ) {
    super();
    this.once("close", () => (this.connAmount = -1));
    this.once("listening", () => (this.listening = true));
    if (connectionListener) this.on("connection", connectionListener);

    this.internal = new zts.Server((error, socket) => {
      const s = new Socket({}, socket);

      this.connAmount++;
      s.once("close", () => {
        this.connAmount--;
        if (this.closed && this.connAmount === 0) {
          this.emit("close");
        }
      });
      process.nextTick(() => this.emit("connection", s));
    });
  }

  listen(port: number, host = "::", callback?: () => void) {
    if (callback) this.on("listening", callback);

    this.internal
      .listen(port, host)
      .then(() => this.emit("listening"))
      .catch((reason) => console.log(reason));
  }

  address() {
    return this.internal.address();
  }

  close() {
    if (!this.listening) return;
    this.listening = false;
    this.internal.close().then(() => {
      this.closed = true;
      if (this.connAmount === 0) this.emit("close");
    });
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

class Socket extends Duplex {
  private internalEvents = new EventEmitter();
  private internal: nativeSocket;

  private connected = false;

  bytesRead = 0;
  bytesWritten = 0;

  // internal socket writes to receiver, receiver writes to duplex (which is read by client application)
  private receiver = new PassThrough();

  constructor(
    options: node_net.SocketConstructorOpts,
    internal?: nativeSocket,
  ) {
    super({
      allowHalfOpen: options.allowHalfOpen ?? false,
      readable: options.readable,
      writable: options.writable,
    } as DuplexOptions);

    if (internal) this.connected = true;
    this.internal = internal ?? new zts.Socket();

    // events from native socket
    this.internalEvents.on("connect", () => {
      this.connected = true;
      this.emit("connect");
    });
    this.internalEvents.on("data", (data?: Buffer) => {
      if (data) {
        // console.log(`received ${data.length}`);

        this.bytesRead += data.length;
        this.receiver.write(data, undefined, () => {
          // console.log(`acking ${data.length}`);
          this.internal.ack(data.length);
        });
      } else {
        // other side closed the connection
        this.receiver.end();
        if (this.readableLength === 0) this.resume();
      }
    });
    this.internalEvents.on("sent", (length) => {
      // console.log(`internal has sent ${length}`);
      this.bytesWritten += length;
    });
    this.internalEvents.on("close", () => {
      // TODO: is this actually necessary?
      // console.log("internal socket closed");
    });
    this.internalEvents.on("error", (error: NativeError) => {
      console.log(error);
      this.destroy(error);
    });
    this.internal.init((event: string, ...args: unknown[]) =>
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



  _read() {
    this.receiver.resume();
  }

  _write(
    chunk: Buffer,
    _: unknown,
    callback: (error?: Error | null) => void,
  ): void {
    if (!this.connected)
      this.once("connect", () => this.realWrite(chunk, callback));
    else this.realWrite(chunk, callback);
  }

  private async realWrite(
    chunk: Buffer,
    callback: (error?: Error | null) => void,
  ): Promise<void> {
    const currentWritten = this.bytesWritten;
    const length = await this.internal.send(chunk);
    
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
    if (currentWritten !== this.bytesWritten) continuation();
    // wait for more space to become available
    else this.internalEvents.once("sent", continuation);
  }

  _final(callback: (error?: Error | null | undefined) => void): void {
    if (this.connected) {
      this.internalEvents.once("close", callback);

      this.internal.shutdown_wr();
    } else {
      this.internal.shutdown_wr();
      callback();
    }
  }

  connect(
    options: node_net.TcpSocketConnectOpts,
    connectionListener?: () => void,
  ): this {
    if (this.connected) throw Error("Already connected");

    this.internal.connect(options.port, options.host ?? "127.0.0.1");
    if (connectionListener) this.once("connect", connectionListener);
    return this;
  }
} // class Socket

function _createConnection(
  options: node_net.TcpNetConnectOpts,
  connectionListener?: () => void,
): Socket {
  const socket = new Socket(options);

  process.nextTick(() => socket.connect(options), connectionListener);

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
      : _createConnection({ port }, connectionListener);
  }
  if (typeof host === "string") throw Error("Argument");
  if (!("port" in port)) throw Error("Only TCP sockets are possible");

  return _createConnection(port, host);
}

export const connect = createConnection;
