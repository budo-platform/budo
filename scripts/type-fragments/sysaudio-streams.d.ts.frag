// -- Audio streams and decoders ----------------------------------------------

/** Options for sys.audio.openOutput and sys.audio.openInput. */
interface AudioStreamOptions {
  /** 1 or 2. Default: 2 for outputs, 1 for inputs. */
  channels?: 1 | 2;
  /** How much audio the queue holds, in milliseconds (1 to 2000), fixed. Leave it out for an adaptive queue (outputs): as small as this device and app allow, growing after an underrun and shrinking back when steady. */
  latencyMs?: number;
}

/** Handed to stream callbacks with each chunk. The object is reused: read it during the call. */
interface AudioStreamInfo {
  /** Frames in this chunk (256). */
  readonly frames: number;
  readonly channels: number;
  /** The device rate: samples per second per channel. */
  readonly sampleRate: number;
  /** Stream time of the chunk's first frame, in seconds. */
  readonly time: number;
  /** The queue's size, in seconds. */
  readonly latency: number;
  /** Outputs: times the queue ran dry. */
  readonly underruns?: number;
  /** Inputs: times captured audio was dropped because the app fell behind. */
  readonly overruns?: number;
}

/**
 * Fills (output) or receives (input) one chunk: `samples` is interleaved
 * (left, right, left, …), `info.frames * info.channels` long. Output buffers
 * start silent; input samples are valid during the call only.
 */
type AudioStreamCallback = (samples: Float32Array, info: AudioStreamInfo) => void;

/** Options for sys.audio.openDecoder and sys.audio.createDecoder. */
interface AudioDecoderOptions {
  /** Rate of the decoded frames; 0 (default) keeps the file's. Use sys.audio.getSampleRate() to play. */
  sampleRate?: number;
  /** 1 or 2 (default). */
  channels?: 1 | 2;
}

/** State of a decoder (sys.audio.getDecoderInfo). */
interface AudioDecoderInfo {
  /** Rate and channels of the decoded frames. */
  readonly sampleRate: number;
  readonly channels: number;
  /** The file's own rate and channels; 0 until known (push decoders). */
  readonly sourceSampleRate: number;
  readonly sourceChannels: number;
  /** Seconds in all; -1 when unknown (push decoders). */
  readonly duration: number;
  /** Seconds decoded so far. */
  readonly position: number;
  /** The format is known. */
  readonly ready: boolean;
  /** Every frame has been decoded. */
  readonly ended: boolean;
  /** Push decoders: feed more bytes to go on. */
  readonly needsData: boolean;
  readonly seekable: boolean;
}

/** sys.files.getReadInfo: a file open for block reads. */
interface FileReadInfo {
  /** Size in bytes. */
  readonly size: number;
  /** Where the next read starts, in bytes. */
  readonly position: number;
}

/** Options for sys.audio.detectPitch. */
interface PitchOptions {
  /** Lowest pitch reported, in Hz. Default: 50. */
  minFrequency?: number;
  /** Highest pitch reported, in Hz. Default: 4000. */
  maxFrequency?: number;
  /** Clarity (0 to 1) under which there is no pitch. Default: 0.7. */
  minClarity?: number;
  /** RMS level in dB under which there is no pitch. Default: -60. */
  minLevel?: number;
}

/** The result of sys.audio.detectPitch. */
interface PitchResult {
  /** The pitch in Hz, or 0 when there is none (noise, silence, out of range). */
  frequency: number;
  /** How periodic the sound is, 0 to 1: above 0.9 for a clear note. */
  clarity: number;
  /** RMS level of the samples, in dB. */
  level: number;
}
