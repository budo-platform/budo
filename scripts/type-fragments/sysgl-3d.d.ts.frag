  // ---- 3D pipeline ----------------------------------------------------------

  /** Create a vertex or index buffer. If `data` is given, uploads it immediately. */
  createBuffer(target: "vertex" | "index", data?: ArrayBufferView, usage?: "static" | "dynamic" | "stream"): number;

  /** Replace the contents of an existing buffer (or a sub-range from `byteOffset`). */
  updateBuffer(bufferId: number, data: ArrayBufferView, byteOffset?: number): void;

  /** Destroy a buffer created with createBuffer. */
  destroyBuffer(bufferId: number): void;

  /** Create an empty 2D texture. If `pixels` is given, uploads RGBA8 data. */
  createTexture2D(width: number, height: number, format: "rgba8" | "rgb8" | "r8", pixels?: ArrayBufferView): number;

  /** Decode a PNG/JPG file from the project directory and upload it as a 2D texture. */
  loadTexture2D(path: string): number;


  /** Decode 6 image files (order: +X, -X, +Y, -Y, +Z, -Z) and upload them as a cubemap. */
  loadTextureCube(paths: [string, string, string, string, string, string]): number;

  /** Replace a sub-rectangle of a 2D texture with new pixel data. */
  updateTexture2D(textureId: number, x: number, y: number, w: number, h: number, pixels: ArrayBufferView): void;

  /** Destroy a texture created with createTexture2D, loadTexture2D, or loadTextureCube. */
  destroyTexture(textureId: number): void;

  /** Create an empty vertex layout. Returns a layout ID; bind attributes via setAttribute. */
  createVertexLayout(): number;

  /** Bind one vertex attribute slot to a buffer (use setIndexBuffer for the index buffer). */
  setAttribute(layoutId: number, location: number, bufferId: number, size: number, type: "float" | "byte" | "ubyte" | "short" | "ushort" | "int" | "uint", normalized: boolean, stride: number, offset: number, divisor?: number): void;

  /** Attach an index buffer (u16 or u32) to a vertex layout. */
  setIndexBuffer(layoutId: number, bufferId: number, type: "u16" | "u32"): void;

  /** Destroy a vertex layout (does not destroy the underlying buffers). */
  destroyVertexLayout(layoutId: number): void;

  /** Look up the GLSL `layout(location=N)` of a named attribute in a program. */
  getAttribLocation(programId: number, name: string): number;

  /** Set a mat3 uniform from a 9-element Float32Array. */
  setUniformMatrix3(programId: number, name: string, mat9: Float32Array): void;

  /** Set a mat4 uniform from a 16-element Float32Array. */
  setUniformMatrix4(programId: number, name: string, mat16: Float32Array): void;

  /** Set a float-array uniform (1/2/3/4-component variants). */
  setUniform1fv(programId: number, name: string, values: Float32Array): void;
  setUniform2fv(programId: number, name: string, values: Float32Array): void;
  setUniform3fv(programId: number, name: string, values: Float32Array): void;
  setUniform4fv(programId: number, name: string, values: Float32Array): void;

  /** Set an int-array uniform. */
  setUniform1iv(programId: number, name: string, values: Int32Array): void;

  /** Bind a 2D texture (created via createTexture2D / loadTexture2D) to a sampler uniform. */
  bindTexture2D(programId: number, uniformName: string, textureId: number, textureUnit: number): void;

  /** Bind a cubemap texture to a sampler uniform. */
  bindTextureCube(programId: number, uniformName: string, textureId: number, textureUnit: number): void;

  /** Draw immediately against a vertex layout (with optional index buffer + GL state). Set options.instanceCount, with per-instance attributes (setAttribute divisor 1), to draw instances. */
  drawMesh(programId: number, layoutId: number, options: GLDrawOptions): void;

