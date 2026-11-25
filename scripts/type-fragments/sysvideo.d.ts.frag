// -- sys.video object ---------------------------------------------------------

interface DemoVideoError {
  code: string;
  message: string;
}

interface DemoVideoPreviewCallbacks {
  /** Called once before the first frame with the shorter input's duration. */
  onReady?(durationUs: number): void;
  onFrame(texture: number, ptsUs: number): void;
  onEnd(): void;
  onError(error: DemoVideoError): void;
}

interface DemoVideoExportCallbacks {
  onProgress?(completedFrames: number): void;
  onComplete(path: string): void;
  onError(error: DemoVideoError): void;
}

interface VideoReaderInfo {
  width: number;
  height: number;
  frameRateNum: number;
  frameRateDen: number;
  sampleAspectRatioNum: 1;
  sampleAspectRatioDen: 1;
  rotationDegrees: 0;
  timeBaseNum: number;
  timeBaseDen: number;
  durationUs: number;
  seekable: true;
  exactFrameOrdinals: true;
  /** Frame PTS values are relative to the stream's first presentation time. */
  timeOrigin: "stream-start";
  codec: "h264";
  colorPrimaries: "bt709";
  colorTransfer: "bt709";
  colorMatrix: "bt709";
  colorRange: "full" | "limited";
}

interface VideoReaderCallbacks {
  onReady(info: VideoReaderInfo): void;
  onError(error: DemoVideoError): void;
}

interface VideoFrameCallbacks {
  /** Texture contents are stable until releaseFrame(frameHandle). */
  onFrame(frameHandle: number, texture: number, ptsUs: number, durationUs: number,
          ordinal: number, width: number, height: number): void;
  onEof(): void;
  onError(error: DemoVideoError): void;
}

interface VideoFrameWriterOptions {
  /** Positive even dimensions, at most 16 Mi pixels. */
  width: number;
  height: number;
  /** 1..1,000,000 frames; finish rejects incomplete output. */
  totalFrames: number;
  /** Rational constant frame rate, 1..240 fps. Defaults to 30/1. Numerator <= 240000, denominator <= 10000. */
  frameRateNum?: number;
  frameRateDen?: number;
  /** Positive bits/second, default 8000000. */
  videoBitRate?: number;
  /** Omit all audio fields for silent video. Otherwise use 44100 or 48000 Hz and 1 or 2 channels. */
  audioSampleRate?: 44100 | 48000;
  audioChannels?: 1 | 2;
  /** Positive bits/second, default 128000 when audio is enabled. */
  audioBitRate?: number;
}

interface VideoFrameWriterCallbacks {
  /** Called after opening (0) and each encoded frame. Submit only after this acknowledgement. */
  onReady(completedFrames: number): void;
  /** Called after successful atomic publication with a virtual files/ path. The writer handle is now invalid. */
  onComplete(path: string): void;
  /** Terminal failure or video.cancelled; staging output is discarded and the handle is invalid. */
  onError(error: DemoVideoError): void;
}

