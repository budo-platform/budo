  /**
    * Stage an input tensor for the next `run()` call.
   * The optional `shape` argument is accepted for API symmetry with WASM and ignored
   * when the model declares a fully-static shape.
   */
  setInput(modelId: number, name: string, data: Float32Array | Int32Array | BigInt64Array | Uint8Array, shape?: number[]): void;

  /**
    * Read a previously-staged or just-produced output tensor by name. Returns the
    * same TypedArray flavour as the model output declares.
   */
  getOutput(modelId: number, name: string): Float32Array | Int32Array | BigInt64Array | Uint8Array;

  /**
    * Run inference, optionally accepting an inputs map directly and returning
    * the outputs map.
   */
  run(modelId: number, inputs?: Record<string, Float32Array | Int32Array | BigInt64Array | Uint8Array>): Record<string, Float32Array | Int32Array | BigInt64Array | Uint8Array>;

