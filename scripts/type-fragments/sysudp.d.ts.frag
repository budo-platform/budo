// -- sys.net.udp object -----------------------------------------------------------

/** A datagram delivered to a `sys.net.udp.onMessage` callback. */
interface UdpMessage {
  /** The raw datagram bytes, as an array of byte values (0 to 255). */
  data: number[];
  /** The remote host (numeric IP string). */
  host: string;
  /** The remote port. */
  port: number;
}

interface SysUDP {
  /**
   * Bind a UDP socket on the given port. Pass `0` to let the OS pick a free port
   * (use `getPort()` afterwards to discover it). Returns a non-negative socket id,
   * or `-1` on transient failure (port busy, permission denied, …).
   */
  bind(port: number): number;

  /** Return the actual port a previously-bound socket is listening on, or `-1`. */
  getPort(socketId: number): number;

  /**
   * Send a datagram to `host:port`. `data` is an array or typed array of byte
   * values (0 to 255). Returns true when the datagram was sent.
   */
  send(socketId: number, host: string, port: number, data: ArrayLike<number>): boolean;

  /**
   * Register a callback invoked once per pending datagram during the per-frame
   * polling pass. Pass `null` to unregister.
   */
  onMessage(socketId: number, callback: ((msg: UdpMessage) => void) | null): void;

  /** Close a socket and release its OS resources. */
  close(socketId: number): void;
}
