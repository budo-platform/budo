interface GLDrawOptions {
  /** Primitive topology. Default: "triangles". */
  mode?: "triangles" | "triangle_strip" | "triangle_fan" | "lines" | "line_strip" | "points";
  /** First vertex (drawArrays) or index (drawElements). Default: 0. */
  first?: number;
  /** Vertex / index count. Required. */
  count: number;
  /** Render target id. Default: the target bound with `bindRenderTarget`, else the screen; `-1` forces the screen. */
  target?: number;
  /** Instances to draw, with per-instance attributes (setAttribute divisor 1). Default: 1. */
  instanceCount?: number;
  /** Enable depth testing. Default: true. */
  depthTest?: boolean;
  /** Write to the depth buffer. Default: true. */
  depthWrite?: boolean;
  /** Cull-face mode. Default: "none". */
  cull?: "none" | "back" | "front";
  /** Blend mode: "alpha" (straight alpha), "premult" (premultiplied colors), "add", or "none". Default: "alpha". */
  blend?: "none" | "alpha" | "add" | "premult";
}
