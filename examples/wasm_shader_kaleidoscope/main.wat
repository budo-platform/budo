;; WebAssembly Kaleidoscope Shader Demo
;; Draws colorful animated shapes, then applies a kaleidoscope
;; mirror effect via a post-processing shader.

(module
  ;; Canvas
  (import "env" "canvas_clear" (func $canvas_clear (param i32)))
  (import "env" "canvas_draw_rect" (func $canvas_draw_rect (param f32 f32 f32 f32)))
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

  ;; GL shader
  (import "env" "gl_create_program" (func $gl_create_program (param i32 i32 i32 i32) (result i32)))
  (import "env" "gl_draw_fullscreen" (func $gl_draw_fullscreen (param i32)))
  (import "env" "gl_set_uniform_1f" (func $gl_set_uniform_1f (param i32 i32 i32 f32)))

  (memory (export "memory") 1)

  ;; Strings
  (data (i32.const 0) "shader.vert")
  (data (i32.const 16) "shader.frag")
  (data (i32.const 32) "u_segments")

  (global $shader_id (mut i32) (i32.const -1))

  ;; Palette
  (global $C0 i32 (i32.const 0xFF264653))
  (global $C1 i32 (i32.const 0xFF2A9D8F))
  (global $C2 i32 (i32.const 0xFFE9C46A))
  (global $C3 i32 (i32.const 0xFFF4A261))
  (global $C4 i32 (i32.const 0xFFE76F51))
  (global $C5 i32 (i32.const 0xFF606C38))
  (global $C6 i32 (i32.const 0xFF283618))

  (func $init (export "init")
    (call $canvas_set_anti_alias (i32.const 1))
    (global.set $shader_id
      (call $gl_create_program
        (i32.const 0) (i32.const 11)
        (i32.const 16) (i32.const 11)))
  )

  (func $frame (export "frame") (param $timestamp f32)
    (local $w f32)
    (local $h f32)
    (local $cx f32)
    (local $cy f32)
    (local $t f32)
    (local $i i32)
    (local $angle f32)
    (local $x f32)
    (local $y f32)
    (local $r f32)

    (local.set $w (f32.convert_i32_s (call $window_get_width)))
    (local.set $h (f32.convert_i32_s (call $window_get_height)))
    (local.set $cx (f32.div (local.get $w) (f32.const 2.0)))
    (local.set $cy (f32.div (local.get $h) (f32.const 2.0)))
    (local.set $t (f32.div (local.get $timestamp) (f32.const 1000.0)))

    ;; Rich background for the kaleidoscope to multiply
    (call $canvas_clear (global.get $C0))

    ;; Layer 1: large slow-rotating circles near center
    (local.set $i (i32.const 0))
    (block $b1
      (loop $l1
        (br_if $b1 (i32.ge_s (local.get $i) (i32.const 4)))

        (local.set $angle (f32.add
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 1.5708))  ;; PI/2
          (f32.mul (local.get $t) (f32.const 0.4))))
        (local.set $x (f32.add (local.get $cx)
          (f32.mul (call $cos (local.get $angle)) (f32.const 60.0))))
        (local.set $y (f32.add (local.get $cy)
          (f32.mul (call $sin (local.get $angle)) (f32.const 60.0))))
        (local.set $r (f32.add (f32.const 35.0)
          (f32.mul (call $sin (f32.add (local.get $t) (f32.convert_i32_s (local.get $i)))) (f32.const 10.0))))

        (if (i32.le_s (local.get $i) (i32.const 1))
          (then (call $canvas_set_fill_color (global.get $C1)))
          (else (call $canvas_set_fill_color (global.get $C3))))

        (call $canvas_draw_circle (local.get $x) (local.get $y) (local.get $r))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l1)
      )
    )

    ;; Layer 2: radiating lines from center
    (call $canvas_set_stroke_width (f32.const 4.0))
    (local.set $i (i32.const 0))
    (block $b2
      (loop $l2
        (br_if $b2 (i32.ge_s (local.get $i) (i32.const 10)))

        (local.set $angle (f32.add
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 0.6283))  ;; 2*PI/10
          (f32.mul (local.get $t) (f32.const 0.25))))

        (local.set $x (f32.add (local.get $cx)
          (f32.mul (call $cos (local.get $angle)) (f32.const 180.0))))
        (local.set $y (f32.add (local.get $cy)
          (f32.mul (call $sin (local.get $angle)) (f32.const 180.0))))

        (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 2)))
          (then (call $canvas_set_stroke_color (global.get $C2)))
          (else (call $canvas_set_stroke_color (global.get $C4))))

        (call $canvas_draw_line (local.get $cx) (local.get $cy) (local.get $x) (local.get $y))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l2)
      )
    )

    ;; Layer 3: outer ring of small circles
    (local.set $i (i32.const 0))
    (block $b3
      (loop $l3
        (br_if $b3 (i32.ge_s (local.get $i) (i32.const 16)))

        (local.set $angle (f32.add
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 0.3927))  ;; 2*PI/16
          (f32.mul (local.get $t) (f32.const 0.7))))
        (local.set $x (f32.add (local.get $cx)
          (f32.mul (call $cos (local.get $angle)) (f32.const 140.0))))
        (local.set $y (f32.add (local.get $cy)
          (f32.mul (call $sin (local.get $angle)) (f32.const 140.0))))

        (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 3)))
          (then (call $canvas_set_fill_color (global.get $C4)))
          (else
            (if (i32.eq (i32.rem_u (local.get $i) (i32.const 3)) (i32.const 1))
              (then (call $canvas_set_fill_color (global.get $C2)))
              (else (call $canvas_set_fill_color (global.get $C1))))))

        (call $canvas_draw_circle (local.get $x) (local.get $y) (f32.const 12.0))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l3)
      )
    )

    ;; Center dot
    (call $canvas_set_fill_color (global.get $C2))
    (call $canvas_draw_circle (local.get $cx) (local.get $cy) (f32.const 18.0))

    ;; Apply kaleidoscope shader: segments oscillate between 4 and 12
    (call $gl_set_uniform_1f
      (global.get $shader_id)
      (i32.const 32) (i32.const 10)   ;; "u_segments"
      (f32.add (f32.const 8.0)
        (f32.mul (call $sin (f32.mul (local.get $t) (f32.const 0.3))) (f32.const 4.0))))

    (call $gl_draw_fullscreen (global.get $shader_id))
  )
)
