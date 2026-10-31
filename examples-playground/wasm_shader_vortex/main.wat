;; WebAssembly Vortex / Swirl Shader Demo
;; Draws a colorful scene and applies an interactive vortex distortion
;; centered on the mouse position.

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

  ;; Input
  (import "env" "input_get_mouse_x" (func $input_get_mouse_x (result i32)))
  (import "env" "input_get_mouse_y" (func $input_get_mouse_y (result i32)))

  ;; Math
  (import "env" "sin" (func $sin (param f32) (result f32)))
  (import "env" "cos" (func $cos (param f32) (result f32)))

  ;; OpenGL shader
  (import "env" "gl_create_program" (func $gl_create_program (param i32 i32 i32 i32) (result i32)))
  (import "env" "gl_draw_fullscreen" (func $gl_draw_fullscreen (param i32)))
  (import "env" "gl_set_uniform_1f" (func $gl_set_uniform_1f (param i32 i32 i32 f32)))
  (import "env" "gl_set_uniform_2f" (func $gl_set_uniform_2f (param i32 i32 i32 f32 f32)))

  (memory (export "memory") 1)

  ;; String constants in linear memory
  ;; 0: "shader.vert" (11)
  ;; 16: "shader.frag" (11)
  ;; 32: "u_mouse" (7)
  ;; 48: "u_radius" (8)
  ;; 64: "u_twist" (7)
  (data (i32.const 0) "shader.vert")
  (data (i32.const 16) "shader.frag")
  (data (i32.const 32) "u_mouse")
  (data (i32.const 48) "u_radius")
  (data (i32.const 64) "u_twist")

  (global $shader_id (mut i32) (i32.const -1))

  ;; Colors
  (global $COL_BG i32 (i32.const 0xFF0D1B2A))
  (global $COL_1 i32 (i32.const 0xFF1B998B))
  (global $COL_2 i32 (i32.const 0xFFFF6B6B))
  (global $COL_3 i32 (i32.const 0xFFF4D35E))
  (global $COL_4 i32 (i32.const 0xFFEE964B))
  (global $COL_5 i32 (i32.const 0xFF2EC4B6))
  (global $COL_6 i32 (i32.const 0xFFE71D36))
  (global $COL_7 i32 (i32.const 0xFFBB86FC))
  (global $COL_GRID i32 (i32.const 0xFF1A3048))

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
    (local $mx f32)
    (local $my f32)
    (local $i i32)
    (local $angle f32)
    (local $x f32)
    (local $y f32)
    (local $r f32)
    (local $gx f32)

    (local.set $w (f32.convert_i32_s (call $window_get_width)))
    (local.set $h (f32.convert_i32_s (call $window_get_height)))
    (local.set $cx (f32.div (local.get $w) (f32.const 2.0)))
    (local.set $cy (f32.div (local.get $h) (f32.const 2.0)))
    (local.set $t (f32.div (local.get $timestamp) (f32.const 1000.0)))
    (local.set $mx (f32.convert_i32_s (call $input_get_mouse_x)))
    (local.set $my (f32.convert_i32_s (call $input_get_mouse_y)))

    (call $canvas_clear (global.get $COL_BG))

    ;; Background grid
    (call $canvas_set_stroke_color (global.get $COL_GRID))
    (call $canvas_set_stroke_width (f32.const 1.0))
    (local.set $gx (f32.const 0.0))
    (block $hbreak
      (loop $hloop
        (br_if $hbreak (f32.ge (local.get $gx) (local.get $w)))
        (call $canvas_draw_line (local.get $gx) (f32.const 0.0) (local.get $gx) (local.get $h))
        (local.set $gx (f32.add (local.get $gx) (f32.const 50.0)))
        (br $hloop)
      )
    )
    (local.set $gx (f32.const 0.0))
    (block $vbreak
      (loop $vloop
        (br_if $vbreak (f32.ge (local.get $gx) (local.get $h)))
        (call $canvas_draw_line (f32.const 0.0) (local.get $gx) (local.get $w) (local.get $gx))
        (local.set $gx (f32.add (local.get $gx) (f32.const 50.0)))
        (br $vloop)
      )
    )

    ;; Concentric rings in center
    (call $canvas_set_stroke_width (f32.const 3.0))
    (local.set $i (i32.const 0))
    (block $rbreak
      (loop $rloop
        (br_if $rbreak (i32.ge_s (local.get $i) (i32.const 6)))
        (local.set $r (f32.add (f32.const 35.0)
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 30.0))))
        ;; Alternate colors
        (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 2)))
          (then (call $canvas_set_stroke_color (global.get $COL_5)))
          (else (call $canvas_set_stroke_color (global.get $COL_7))))
        (call $canvas_draw_circle (local.get $cx) (local.get $cy) (local.get $r))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $rloop)
      )
    )

    ;; Animated orbiting squares
    (local.set $i (i32.const 0))
    (block $sbreak
      (loop $sloop
        (br_if $sbreak (i32.ge_s (local.get $i) (i32.const 5)))

        (local.set $angle (f32.add
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 1.2566))  ;; 2*PI/5
          (f32.mul (local.get $t) (f32.const 0.8))))

        (local.set $x (f32.add (local.get $cx)
          (f32.mul (call $cos (local.get $angle)) (f32.const 160.0))))
        (local.set $y (f32.add (local.get $cy)
          (f32.mul (call $sin (local.get $angle)) (f32.const 160.0))))

        ;; Pick color based on index
        (if (i32.eqz (local.get $i))
          (then (call $canvas_set_fill_color (global.get $COL_1)))
          (else
            (if (i32.eq (local.get $i) (i32.const 1))
              (then (call $canvas_set_fill_color (global.get $COL_2)))
              (else
                (if (i32.eq (local.get $i) (i32.const 2))
                  (then (call $canvas_set_fill_color (global.get $COL_3)))
                  (else
                    (if (i32.eq (local.get $i) (i32.const 3))
                      (then (call $canvas_set_fill_color (global.get $COL_4)))
                      (else (call $canvas_set_fill_color (global.get $COL_6))))))))))

        (call $canvas_draw_round_rect
          (f32.sub (local.get $x) (f32.const 22.0))
          (f32.sub (local.get $y) (f32.const 22.0))
          (f32.const 44.0) (f32.const 44.0)
          (f32.const 6.0) (f32.const 6.0))

        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $sloop)
      )
    )

    ;; Corner circles
    (call $canvas_set_fill_color (global.get $COL_2))
    (call $canvas_draw_circle (f32.const 50.0) (f32.const 50.0) (f32.const 30.0))
    (call $canvas_set_fill_color (global.get $COL_3))
    (call $canvas_draw_circle (f32.sub (local.get $w) (f32.const 50.0)) (f32.const 50.0) (f32.const 30.0))
    (call $canvas_set_fill_color (global.get $COL_5))
    (call $canvas_draw_circle (f32.const 50.0) (f32.sub (local.get $h) (f32.const 50.0)) (f32.const 30.0))
    (call $canvas_set_fill_color (global.get $COL_7))
    (call $canvas_draw_circle (f32.sub (local.get $w) (f32.const 50.0)) (f32.sub (local.get $h) (f32.const 50.0)) (f32.const 30.0))

    ;; Apply vortex shader with mouse position
    (call $gl_set_uniform_2f
      (global.get $shader_id)
      (i32.const 32) (i32.const 7)   ;; "u_mouse"
      (local.get $mx) (local.get $my))

    (call $gl_set_uniform_1f
      (global.get $shader_id)
      (i32.const 48) (i32.const 8)   ;; "u_radius"
      (f32.const 0.35))

    (call $gl_set_uniform_1f
      (global.get $shader_id)
      (i32.const 64) (i32.const 7)   ;; "u_twist"
      (f32.const 4.0))

    (call $gl_draw_fullscreen (global.get $shader_id))
  )
)
