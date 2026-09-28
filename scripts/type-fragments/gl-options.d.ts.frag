interface GLDrawOptions {
  /** Primitive topology. Default: "triangles". */
  mode?: "triangles" | "triangle_strip" | "triangle_fan" | "lines" | "line_strip" | "points";
  /** First vertex (drawArrays) or index (drawElements). Default: 0. */
  first?: number;
  /** Vertex / index count. Required. */
  count: number;
  /** Optional render target id (default: backbuffer). */
  target?: number;
  /** Enable depth testing. Default: false. */
  depthTest?: boolean;
  /** Write to the depth buffer. Default: true when depthTest is true. */
  depthWrite?: boolean;
  /** Cull-face mode. Default: "none". */
  cull?: "none" | "back" | "front";
  /** Blend mode. Default: "none". */
  blend?: "none" | "alpha" | "add" | "premultiplied";
}
