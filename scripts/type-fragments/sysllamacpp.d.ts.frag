// -- sys.llamacpp object ------------------------------------------------------

type LlamaCppRole = "system" | "user" | "assistant";
interface LlamaCppMessage { role: LlamaCppRole; content: string; }
interface LlamaCppLoadOptions {
  contextSize?: number; device?: "auto" | "cpu" | string;
  gpuLayers?: "auto" | number; threads?: "auto" | number;
  batchSize?: "auto" | number; useMmap?: boolean; allowFallback?: boolean;
}
interface LlamaCppGenerateOptions {
  maxTokens?: number; minTokens?: number; temperature?: number; topK?: number; topP?: number;
  minP?: number; repetitionPenalty?: number; seed?: number; stop?: string[];
}
interface LlamaCppResult {
  finishReason: "stop" | "length" | "cancelled"; text: string;
  promptTokens: number; generatedTokens: number;
  promptTokensPerSecond: number; generatedTokensPerSecond: number;
}
interface LlamaCppStreamCallbacks {
  onText(text: string): void;
  onComplete(result: LlamaCppResult): void;
  onError(error: Error): void;
}
interface LlamaCppGeneration { readonly running: boolean; cancel(): void; }
interface LlamaCppChat {
  send(text: string, options: LlamaCppGenerateOptions, callbacks: LlamaCppStreamCallbacks): LlamaCppGeneration;
  send(text: string, callbacks: LlamaCppStreamCallbacks): LlamaCppGeneration;
  getMessages(): LlamaCppMessage[]; clear(): void; cancel(): void; destroy(): void;
}
interface LlamaCppModelInfo {
  name: string; architecture: string; quantization: string; parameterCount: number;
  fileSize: number; modelContextSize: number; activeContextSize: number;
  vocabularySize: number; chatTemplate: string; backend: string; device: string;
  gpuLayers: number; totalLayers: number; estimatedMemoryBytes: number; fallbackReason: string;
}
interface LlamaCppModel {
  getInfo(): LlamaCppModelInfo;
  createChat(options?: { systemPrompt?: string; contextSize?: number }): LlamaCppChat;
  destroy(): void;
}
interface LlamaCppDeviceInfo {
  id: string; backend: string; name: string; available: boolean;
  memoryBytes: number | null; reason: string;
}
interface SysLlamaCpp {
  isAvailable(): boolean; getDevices(): LlamaCppDeviceInfo[];
  loadModel(path: string, callback: (model: LlamaCppModel | null, error: Error | null) => void): void;
  loadModel(path: string, options: LlamaCppLoadOptions, callback: (model: LlamaCppModel | null, error: Error | null) => void): void;
  getError(): string;
}
