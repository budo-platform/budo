;; WebAssembly Shapes Demo
;; Demonstrates basic shape drawing with the Skia canvas API
;;
;; This example draws various shapes on the canvas including:
;; - Filled rectangles
;; - Circles
;; - Lines
;; - Rounded rectangles
;; - Custom paths (triangle)

(module
  ;; Import canvas functions from the host
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
  
  ;; Path functions
  (import "env" "path_create" (func $path_create (result i32)))
  (import "env" "path_move_to" (func $path_move_to (param i32 f32 f32)))
  (import "env" "path_line_to" (func $path_line_to (param i32 f32 f32)))
  (import "env" "path_close" (func $path_close (param i32)))
  (import "env" "path_draw" (func $path_draw (param i32)))

  ;; Memory
  (memory (export "memory") 1)

  ;; Color constants (ARGB format)
  ;; Red: 0xFFFF6B6B, Teal: 0xFF4ECDC4, Blue: 0xFF45B7D1
  ;; Green: 0xFF96CEB4, Yellow: 0xFFFFEAA7, Purple: 0xFF9B59B6
  
  ;; Global to store triangle path ID
  (global $triangle_path (mut i32) (i32.const -1))

  ;; Initialization function - called once at startup
  (func $init (export "init")
    ;; Enable anti-aliasing
    (call $canvas_set_anti_alias (i32.const 1))
    
    ;; Create a triangle path for later use
    (global.set $triangle_path (call $path_create))
    
    ;; Define triangle vertices (centered at origin, will be translated)
    (call $path_move_to (global.get $triangle_path) (f32.const 0.0) (f32.const -50.0))
    (call $path_line_to (global.get $triangle_path) (f32.const 43.3) (f32.const 25.0))
    (call $path_line_to (global.get $triangle_path) (f32.const -43.3) (f32.const 25.0))
    (call $path_close (global.get $triangle_path))
  )

  ;; Frame function - called every frame with timestamp in milliseconds
  (func $frame (export "frame") (param $timestamp f32)
    (local $width f32)
    (local $height f32)
    (local $cx f32)
    (local $cy f32)
    
    ;; Get canvas dimensions
    (local.set $width (f32.convert_i32_s (call $window_get_width)))
    (local.set $height (f32.convert_i32_s (call $window_get_height)))
    (local.set $cx (f32.div (local.get $width) (f32.const 2.0)))
    (local.set $cy (f32.div (local.get $height) (f32.const 2.0)))
    
    ;; Clear canvas with light gray background (0xFFF0F0F0)
    (call $canvas_clear (i32.const 0xFFF5F5F5))
    
    ;; Draw title area - dark blue rounded rect
    (call $canvas_set_fill_color (i32.const 0xFF2C3E50))
    (call $canvas_draw_round_rect
      (f32.const 50.0) (f32.const 30.0)
      (f32.sub (local.get $width) (f32.const 100.0)) (f32.const 60.0)
      (f32.const 10.0) (f32.const 10.0))
    
    ;; Row 1: Basic shapes
    ;; Red filled rectangle
    (call $canvas_set_fill_color (i32.const 0xFFFF6B6B))
    (call $canvas_draw_rect (f32.const 80.0) (f32.const 130.0) (f32.const 100.0) (f32.const 80.0))
    
    ;; Teal circle
    (call $canvas_set_fill_color (i32.const 0xFF4ECDC4))
    (call $canvas_draw_circle (f32.const 280.0) (f32.const 170.0) (f32.const 50.0))
    
    ;; Blue rounded rectangle
    (call $canvas_set_fill_color (i32.const 0xFF45B7D1))
    (call $canvas_draw_round_rect
      (f32.const 380.0) (f32.const 130.0)
      (f32.const 100.0) (f32.const 80.0)
      (f32.const 15.0) (f32.const 15.0))
    
    ;; Purple triangle (using path)
    (call $canvas_set_fill_color (i32.const 0xFF9B59B6))
    ;; Note: paths are drawn at their defined location
    ;; We need to manually offset the triangle
    (call $path_draw (global.get $triangle_path))
    
    ;; Row 2: Stroked shapes
    ;; Orange stroked rectangle
    (call $canvas_set_stroke_color (i32.const 0xFFF39C12))
    (call $canvas_set_stroke_width (f32.const 4.0))
    (call $canvas_draw_rect (f32.const 80.0) (f32.const 280.0) (f32.const 100.0) (f32.const 80.0))
    
    ;; Green stroked circle
    (call $canvas_set_stroke_color (i32.const 0xFF27AE60))
    (call $canvas_set_stroke_width (f32.const 5.0))
    (call $canvas_draw_circle (f32.const 280.0) (f32.const 320.0) (f32.const 50.0))
    
    ;; Pink stroked rounded rect
    (call $canvas_set_stroke_color (i32.const 0xFFE91E63))
    (call $canvas_set_stroke_width (f32.const 3.0))
    (call $canvas_draw_round_rect
      (f32.const 380.0) (f32.const 280.0)
      (f32.const 100.0) (f32.const 80.0)
      (f32.const 20.0) (f32.const 20.0))
    
    ;; Row 3: Lines pattern
    (call $canvas_set_stroke_color (i32.const 0xFF34495E))
    (call $canvas_set_stroke_width (f32.const 2.0))
    
    ;; Draw a series of lines
    (call $canvas_draw_line (f32.const 80.0) (f32.const 430.0) (f32.const 200.0) (f32.const 430.0))
    (call $canvas_draw_line (f32.const 80.0) (f32.const 450.0) (f32.const 200.0) (f32.const 430.0))
    (call $canvas_draw_line (f32.const 80.0) (f32.const 470.0) (f32.const 200.0) (f32.const 430.0))
    (call $canvas_draw_line (f32.const 80.0) (f32.const 490.0) (f32.const 200.0) (f32.const 430.0))
    
    ;; Draw a colorful cross pattern
    (call $canvas_set_stroke_width (f32.const 4.0))
    (call $canvas_set_stroke_color (i32.const 0xFFE74C3C))
    (call $canvas_draw_line (f32.const 280.0) (f32.const 400.0) (f32.const 280.0) (f32.const 500.0))
    (call $canvas_set_stroke_color (i32.const 0xFF3498DB))
    (call $canvas_draw_line (f32.const 230.0) (f32.const 450.0) (f32.const 330.0) (f32.const 450.0))
    
    ;; Nested circles
    (call $canvas_set_stroke_width (f32.const 2.0))
    (call $canvas_set_stroke_color (i32.const 0xFF1ABC9C))
    (call $canvas_draw_circle (f32.const 430.0) (f32.const 450.0) (f32.const 15.0))
    (call $canvas_set_stroke_color (i32.const 0xFF9B59B6))
    (call $canvas_draw_circle (f32.const 430.0) (f32.const 450.0) (f32.const 30.0))
    (call $canvas_set_stroke_color (i32.const 0xFFE67E22))
    (call $canvas_draw_circle (f32.const 430.0) (f32.const 450.0) (f32.const 45.0))
  )
)
