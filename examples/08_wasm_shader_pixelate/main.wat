;; WebAssembly Pixelate Shader Demo
;; Draws animated shapes and applies a pixelation post-processing shader.
;; Pixel size oscillates over time for a dynamic retro look.

(module
  ;; Canvas drawing
  (import "env" "canvas_clear" (func $canvas_clear (param i32)))
  (import "env" "canvas_draw_rect" (func $canvas_draw_rect (param f32 f32 f32 f32)))
  (import "env" "canvas_draw_round_rect" (func $canvas_draw_round_rect (param f32 f32 f32 f32 f32 f32)))
  (import "env" "canvas_draw_circle" (func $canvas_draw_circle (param f32 f32 f32)))
  (import "env" "canvas_draw_line" (func $canvas_draw_line (param f32 f32 f32 f32)))
  (import "env" "canvas_set_fill_color" (func $canvas_set_fill_color (param i32)))
  (import "env" "canvas_set_stroke_color" (func $canvas_set_stroke_color (param i32)))
  (import "env" "canvas_set_stroke_width" (func $canvas_set_stroke_width (param f32)))
  (import "env" "canvas_set_anti_alias" (func $canvas_set_anti_alias (param i32)))
  (import "env" "window_get_width" (func $window_get_width (result i32)))
  (import "env" "window_get_height" (func $window_get_height (result i32)))

  ;; Math
  (import "env" "sin" (func $sin (param f32) (result f32)))
  (import "env" "cos" (func $cos (param f32) (result f32)))

  ;; OpenGL shader functions
  (import "env" "gl_create_program" (func $gl_create_program (param i32 i32 i32 i32) (result i32)))
  (import "env" "gl_draw_fullscreen" (func $gl_draw_fullscreen (param i32)))
  (import "env" "gl_set_uniform_1f" (func $gl_set_uniform_1f (param i32 i32 i32 f32)))

  (memory (export "memory") 1)

  ;; String data in memory
  ;; Offset 0: "shader.vert" (11 bytes)
  ;; Offset 16: "shader.frag" (11 bytes)
  ;; Offset 32: "u_pixel_size" (12 bytes)
  (data (i32.const 0) "shader.vert")
  (data (i32.const 16) "shader.frag")
  (data (i32.const 32) "u_pixel_size")

  ;; Shader program ID
  (global $shader_id (mut i32) (i32.const -1))

  ;; Color palette
  (global $COL_BG i32 (i32.const 0xFF1B2838))
  (global $COL_RED i32 (i32.const 0xFFFF4444))
  (global $COL_CYAN i32 (i32.const 0xFF00CED1))
  (global $COL_YELLOW i32 (i32.const 0xFFFFD700))
  (global $COL_GREEN i32 (i32.const 0xFF32CD32))
  (global $COL_MAGENTA i32 (i32.const 0xFFFF00FF))
  (global $COL_ORANGE i32 (i32.const 0xFFFF8C00))

  (func $init (export "init")
    (call $canvas_set_anti_alias (i32.const 1))

    ;; Create shader program: gl_create_program(vert_ptr, vert_len, frag_ptr, frag_len)
    (global.set $shader_id
      (call $gl_create_program
        (i32.const 0) (i32.const 11)     ;; "shader.vert"
        (i32.const 16) (i32.const 11)))   ;; "shader.frag"
  )

  (func $frame (export "frame") (param $timestamp f32)
    (local $width f32)
    (local $height f32)
    (local $cx f32)
    (local $cy f32)
    (local $t f32)
    (local $i i32)
    (local $angle f32)
    (local $x f32)
    (local $y f32)
    (local $r f32)
    (local $pixel_size f32)

    (local.set $width (f32.convert_i32_s (call $window_get_width)))
    (local.set $height (f32.convert_i32_s (call $window_get_height)))
    (local.set $cx (f32.div (local.get $width) (f32.const 2.0)))
    (local.set $cy (f32.div (local.get $height) (f32.const 2.0)))
    ;; Convert ms to seconds
    (local.set $t (f32.div (local.get $timestamp) (f32.const 1000.0)))

    (call $canvas_clear (global.get $COL_BG))

    ;; Draw a ring of colored circles
    (local.set $i (i32.const 0))
    (block $break
      (loop $loop
        (br_if $break (i32.ge_s (local.get $i) (i32.const 8)))

        (local.set $angle (f32.add
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 0.7854))  ;; PI/4
          (f32.mul (local.get $t) (f32.const 0.6))))

        (local.set $x (f32.add (local.get $cx)
          (f32.mul (call $cos (local.get $angle)) (f32.const 120.0))))
        (local.set $y (f32.add (local.get $cy)
          (f32.mul (call $sin (local.get $angle)) (f32.const 120.0))))
        (local.set $r (f32.add (f32.const 22.0)
          (f32.mul (call $sin (f32.add (local.get $t) (f32.convert_i32_s (local.get $i)))) (f32.const 8.0))))

        ;; Alternate colors
        (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 3)))
          (then (call $canvas_set_fill_color (global.get $COL_RED)))
          (else
            (if (i32.eq (i32.rem_u (local.get $i) (i32.const 3)) (i32.const 1))
              (then (call $canvas_set_fill_color (global.get $COL_CYAN)))
              (else (call $canvas_set_fill_color (global.get $COL_YELLOW))))))

        (call $canvas_draw_circle (local.get $x) (local.get $y) (local.get $r))

        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)
      )
    )

    ;; Central pulsing square
    (call $canvas_set_fill_color (global.get $COL_GREEN))
    (local.set $r (f32.add (f32.const 40.0)
      (f32.mul (call $sin (f32.mul (local.get $t) (f32.const 2.0))) (f32.const 15.0))))
    (call $canvas_draw_round_rect
      (f32.sub (local.get $cx) (local.get $r))
      (f32.sub (local.get $cy) (local.get $r))
      (f32.mul (local.get $r) (f32.const 2.0))
      (f32.mul (local.get $r) (f32.const 2.0))
      (f32.const 8.0) (f32.const 8.0))

    ;; Orbiting small dots
    (local.set $i (i32.const 0))
    (block $break2
      (loop $loop2
        (br_if $break2 (i32.ge_s (local.get $i) (i32.const 12)))

        (local.set $angle (f32.add
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 0.5236))  ;; PI/6
          (f32.mul (local.get $t) (f32.const 1.5))))

        (local.set $x (f32.add (local.get $cx)
          (f32.mul (call $cos (local.get $angle)) (f32.const 200.0))))
        (local.set $y (f32.add (local.get $cy)
          (f32.mul (call $sin (local.get $angle)) (f32.const 200.0))))

        (call $canvas_set_fill_color (global.get $COL_MAGENTA))
        (call $canvas_draw_circle (local.get $x) (local.get $y) (f32.const 8.0))

        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop2)
      )
    )

    ;; Diagonal lines
    (call $canvas_set_stroke_color (global.get $COL_ORANGE))
    (call $canvas_set_stroke_width (f32.const 3.0))
    (call $canvas_draw_line (f32.const 30.0) (f32.const 30.0) (f32.const 180.0) (f32.const 120.0))
    (call $canvas_draw_line (f32.const 30.0) (f32.const 120.0) (f32.const 180.0) (f32.const 30.0))

    ;; Apply pixelation shader
    ;; Pixel size oscillates between 4 and 14
    (local.set $pixel_size (f32.add (f32.const 9.0)
      (f32.mul (call $sin (f32.mul (local.get $t) (f32.const 0.5))) (f32.const 5.0))))

    (call $gl_set_uniform_1f
      (global.get $shader_id)
      (i32.const 32) (i32.const 12)        ;; "u_pixel_size"
      (local.get $pixel_size))

    (call $gl_draw_fullscreen (global.get $shader_id))
  )
)
