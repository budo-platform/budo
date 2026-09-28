;; WASM 3D Cube — proves WASM parity of the 3D pipeline (sys.gl + sys.math).
;;
;; Builds an 8-vertex cube via gl_create_buffer + gl_buffer_data, wires a vertex
;; layout (a_position only), runs all per-frame matrix math through the
;; sys.math host imports, and renders with gl_draw_mesh.
;;
;; The fragment shader colors each pixel from its local cube position so the
;; cube is recognizable without a lighting pass.

(module
  ;; ---- canvas / window imports ----
  (import "env" "canvas_clear"          (func $canvas_clear          (param i32)))
  (import "env" "canvas_set_fill_color"  (func $canvas_set_fill_color  (param i32)))
  (import "env" "canvas_draw_text"      (func $canvas_draw_text      (param i32 i32 f32 f32 f32) (result f32)))
  (import "env" "window_get_width"      (func $window_get_width      (result i32)))
  (import "env" "window_get_height"     (func $window_get_height     (result i32)))

  ;; ---- gl pipeline imports ----
  (import "env" "gl_create_program"        (func $gl_create_program       (param i32 i32 i32 i32) (result i32)))
  (import "env" "gl_create_buffer"         (func $gl_create_buffer        (result i32)))
  (import "env" "gl_buffer_data"           (func $gl_buffer_data          (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "gl_create_vertex_layout"  (func $gl_create_vertex_layout (result i32)))
  (import "env" "gl_set_attribute"         (func $gl_set_attribute        (param i32 i32 i32 i32 i32 i32 i32 i32 i32)))
  (import "env" "gl_set_index_buffer"      (func $gl_set_index_buffer     (param i32 i32 i32)))
  (import "env" "gl_set_uniform_matrix4fv" (func $gl_set_uniform_matrix4  (param i32 i32 i32 i32 i32)))
  (import "env" "gl_draw_mesh"             (func $gl_draw_mesh            (param i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)))

  ;; ---- sys.math imports ----
  (import "env" "math_mat4_identity"    (func $mat4_identity    (param i32)))
  (import "env" "math_mat4_perspective" (func $mat4_perspective (param i32 f32 f32 f32 f32)))
  (import "env" "math_mat4_lookat"      (func $mat4_lookat      (param i32 i32 i32 i32)))
  (import "env" "math_mat4_rotate_x"    (func $mat4_rotate_x    (param i32 i32 f32)))
  (import "env" "math_mat4_rotate_y"    (func $mat4_rotate_y    (param i32 i32 f32)))
  (import "env" "math_mat4_multiply"    (func $mat4_multiply    (param i32 i32 i32)))

  (memory (export "memory") 1)

  ;; ---- memory layout ----
  ;; 0   .. 96  : 8 cube vertex positions (8 * 3 floats)
  ;; 96  .. 168 : 36 u16 indices
  ;; 200 .. 209 : "cube.vert"
  ;; 220 .. 229 : "cube.frag"
  ;; 240 .. 245 : "u_mvp"
  ;; 256 .. 320 : title text
  ;; 384 .. 448 : proj (mat4)
  ;; 448 .. 512 : view (mat4)
  ;; 512 .. 576 : model (mat4)
  ;; 576 .. 640 : tmp (mat4)
  ;; 640 .. 704 : mvp (mat4)
  ;; 704 .. 716 : eye vec3
  ;; 716 .. 728 : target vec3
  ;; 728 .. 740 : up vec3

  (data (i32.const 200) "cube.vert")
  (data (i32.const 220) "cube.frag")
  (data (i32.const 240) "u_mvp")
  (data (i32.const 256) "WASM 3D Cube")

  (global $g_program (mut i32) (i32.const 0))
  (global $g_layout  (mut i32) (i32.const 0))

  ;; Helper: store 3 floats at the given memory offset.
  (func $store_v3 (param $off i32) (param $x f32) (param $y f32) (param $z f32)
    (f32.store (local.get $off)                  (local.get $x))
    (f32.store (i32.add (local.get $off) (i32.const 4))  (local.get $y))
    (f32.store (i32.add (local.get $off) (i32.const 8))  (local.get $z)))

  ;; Helper: store 6 indices (one face) starting at offset $off (in bytes).
  (func $store_face (param $off i32)
        (param $a i32) (param $b i32) (param $c i32)
        (param $d i32) (param $e i32) (param $f i32)
    (i32.store16 (local.get $off)                          (local.get $a))
    (i32.store16 (i32.add (local.get $off) (i32.const 2))  (local.get $b))
    (i32.store16 (i32.add (local.get $off) (i32.const 4))  (local.get $c))
    (i32.store16 (i32.add (local.get $off) (i32.const 6))  (local.get $d))
    (i32.store16 (i32.add (local.get $off) (i32.const 8))  (local.get $e))
    (i32.store16 (i32.add (local.get $off) (i32.const 10)) (local.get $f)))

  (func $init (export "init")
    (local $vbuf i32)
    (local $ibuf i32)

    ;; Vertices: 8 cube corners in [-1,+1]^3.
    (call $store_v3 (i32.const  0) (f32.const -1) (f32.const -1) (f32.const -1)) ;; v0
    (call $store_v3 (i32.const 12) (f32.const  1) (f32.const -1) (f32.const -1)) ;; v1
    (call $store_v3 (i32.const 24) (f32.const  1) (f32.const  1) (f32.const -1)) ;; v2
    (call $store_v3 (i32.const 36) (f32.const -1) (f32.const  1) (f32.const -1)) ;; v3
    (call $store_v3 (i32.const 48) (f32.const -1) (f32.const -1) (f32.const  1)) ;; v4
    (call $store_v3 (i32.const 60) (f32.const  1) (f32.const -1) (f32.const  1)) ;; v5
    (call $store_v3 (i32.const 72) (f32.const  1) (f32.const  1) (f32.const  1)) ;; v6
    (call $store_v3 (i32.const 84) (f32.const -1) (f32.const  1) (f32.const  1)) ;; v7

    ;; Indices, CCW when viewed from outside.
    (call $store_face (i32.const  96) (i32.const 4) (i32.const 5) (i32.const 6)
                                       (i32.const 4) (i32.const 6) (i32.const 7)) ;; +Z
    (call $store_face (i32.const 108) (i32.const 1) (i32.const 0) (i32.const 3)
                                       (i32.const 1) (i32.const 3) (i32.const 2)) ;; -Z
    (call $store_face (i32.const 120) (i32.const 5) (i32.const 1) (i32.const 2)
                                       (i32.const 5) (i32.const 2) (i32.const 6)) ;; +X
    (call $store_face (i32.const 132) (i32.const 0) (i32.const 4) (i32.const 7)
                                       (i32.const 0) (i32.const 7) (i32.const 3)) ;; -X
    (call $store_face (i32.const 144) (i32.const 7) (i32.const 6) (i32.const 2)
                                       (i32.const 7) (i32.const 2) (i32.const 3)) ;; +Y
    (call $store_face (i32.const 156) (i32.const 0) (i32.const 1) (i32.const 5)
                                       (i32.const 0) (i32.const 5) (i32.const 4)) ;; -Y

    ;; Camera vectors.
    (call $store_v3 (i32.const 704) (f32.const 0) (f32.const 0) (f32.const 5))    ;; eye
    (call $store_v3 (i32.const 716) (f32.const 0) (f32.const 0) (f32.const 0))    ;; target
    (call $store_v3 (i32.const 728) (f32.const 0) (f32.const 1) (f32.const 0))    ;; up

    ;; Build GL program.
    (global.set $g_program
      (call $gl_create_program
        (i32.const 200) (i32.const 9)
        (i32.const 220) (i32.const 9)))

    ;; Vertex buffer (positions).
    (local.set $vbuf (call $gl_create_buffer))
    (drop (call $gl_buffer_data
      (local.get $vbuf)
      (i32.const 0)   ;; target = vertex
      (i32.const 0)   ;; data ptr
      (i32.const 96)  ;; data len
      (i32.const 0))) ;; usage = static

    ;; Index buffer.
    (local.set $ibuf (call $gl_create_buffer))
    (drop (call $gl_buffer_data
      (local.get $ibuf)
      (i32.const 1)   ;; target = index
      (i32.const 96)  ;; data ptr
      (i32.const 72)  ;; data len
      (i32.const 0))) ;; usage = static

    ;; Vertex layout: a_position at standard location 0.
    (global.set $g_layout (call $gl_create_vertex_layout))
    (call $gl_set_attribute
      (global.get $g_layout)
      (i32.const 0)        ;; location = a_position
      (local.get $vbuf)
      (i32.const 3)        ;; size
      (i32.const 0)        ;; type = float
      (i32.const 0)        ;; normalized
      (i32.const 12)       ;; stride
      (i32.const 0)        ;; offset
      (i32.const 0))       ;; divisor
    (call $gl_set_index_buffer
      (global.get $g_layout)
      (local.get $ibuf)
      (i32.const 0)))      ;; index type = u16

  (func $frame (export "frame") (param $timestamp f32)
    (local $w i32)
    (local $h i32)
    (local $aspect f32)
    (local $t f32)

    (local.set $w (call $window_get_width))
    (local.set $h (call $window_get_height))
    (local.set $aspect (f32.div (f32.convert_i32_s (local.get $w))
                                (f32.convert_i32_s (local.get $h))))
    (local.set $t (f32.div (local.get $timestamp) (f32.const 1000.0)))

    ;; Background + caption.
    (call $canvas_clear (i32.const 0xFF101020))
    (call $canvas_set_fill_color (i32.const 0xFFFFFFFF))
    (drop (call $canvas_draw_text (i32.const 256) (i32.const 12)
                                  (f32.const 20) (f32.const 32) (f32.const 22)))

    ;; Projection.
    (call $mat4_perspective
      (i32.const 384)
      (f32.const 1.0471975512)   ;; 60° in radians
      (local.get $aspect)
      (f32.const 0.1)
      (f32.const 100.0))

    ;; View (lookAt eye, target, up).
    (call $mat4_lookat
      (i32.const 448)
      (i32.const 704)
      (i32.const 716)
      (i32.const 728))

    ;; Model = identity → rotateY(t * 0.7) → rotateX(t * 0.4).
    (call $mat4_identity (i32.const 512))
    (call $mat4_rotate_y (i32.const 512) (i32.const 512)
                         (f32.mul (local.get $t) (f32.const 0.7)))
    (call $mat4_rotate_x (i32.const 512) (i32.const 512)
                         (f32.mul (local.get $t) (f32.const 0.4)))

    ;; mvp = proj * view * model.
    (call $mat4_multiply (i32.const 576) (i32.const 448) (i32.const 512))
    (call $mat4_multiply (i32.const 640) (i32.const 384) (i32.const 576))

    ;; Upload uniform.
    (call $gl_set_uniform_matrix4
      (global.get $g_program)
      (i32.const 240) (i32.const 5)   ;; "u_mvp"
      (i32.const 640)                 ;; values ptr
      (i32.const 1))                  ;; count

    ;; Draw the cube.
    (call $gl_draw_mesh
      (global.get $g_program)
      (global.get $g_layout)
      (i32.const 0)    ;; mode = triangles
      (i32.const 0)    ;; first
      (i32.const 36)   ;; count
      (i32.const -1)   ;; render target = default framebuffer
      (i32.const 1)    ;; depth_test
      (i32.const 1)    ;; depth_write
      (i32.const 1)    ;; cull = back
      (i32.const 0)    ;; blend = none
      (i32.const 1))))  ;; instance count
