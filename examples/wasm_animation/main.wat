;; WebAssembly Animation Demo
;; Demonstrates animated graphics with the Skia canvas API
;;
;; This example shows:
;; - Rotating shapes
;; - Bouncing balls
;; - Color cycling
;; - Smooth animations using sin/cos

(module
  ;; Import canvas functions from the host
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
  
  ;; Transform functions
  (import "env" "transform_save" (func $transform_save))
  (import "env" "transform_restore" (func $transform_restore))
  (import "env" "transform_translate" (func $transform_translate (param f32 f32)))
  (import "env" "transform_rotate" (func $transform_rotate (param f32)))
  (import "env" "transform_scale" (func $transform_scale (param f32 f32)))
  
  ;; Math functions
  (import "env" "sin" (func $sin (param f32) (result f32)))
  (import "env" "cos" (func $cos (param f32) (result f32)))
  
  ;; Constants
  (global $PI f32 (f32.const 3.14159265359))
  (global $TWO_PI f32 (f32.const 6.28318530718))
  
  ;; Ball state - using linear memory for 5 balls
  ;; Each ball: x, y, vx, vy, radius, color (6 floats = 24 bytes per ball)
  (memory (export "memory") 1)
  
  ;; Ball offsets
  (global $BALL_SIZE i32 (i32.const 24))
  (global $NUM_BALLS i32 (i32.const 8))
  
  ;; Initialization
  (func $init (export "init")
    ;; Enable anti-aliasing
    (call $canvas_set_anti_alias (i32.const 1))
    
    ;; Initialize balls with different positions and velocities
    ;; Ball 0: x=100, y=100, vx=2, vy=1.5, radius=25, color=0xFFFF6B6B
    (f32.store (i32.const 0) (f32.const 100.0))
    (f32.store (i32.const 4) (f32.const 100.0))
    (f32.store (i32.const 8) (f32.const 2.0))
    (f32.store (i32.const 12) (f32.const 1.5))
    (f32.store (i32.const 16) (f32.const 25.0))
    (i32.store (i32.const 20) (i32.const 0xFFFF6B6B))
    
    ;; Ball 1
    (f32.store (i32.const 24) (f32.const 200.0))
    (f32.store (i32.const 28) (f32.const 150.0))
    (f32.store (i32.const 32) (f32.const -1.5))
    (f32.store (i32.const 36) (f32.const 2.0))
    (f32.store (i32.const 40) (f32.const 30.0))
    (i32.store (i32.const 44) (i32.const 0xFF4ECDC4))
    
    ;; Ball 2
    (f32.store (i32.const 48) (f32.const 300.0))
    (f32.store (i32.const 52) (f32.const 200.0))
    (f32.store (i32.const 56) (f32.const 1.8))
    (f32.store (i32.const 60) (f32.const -1.2))
    (f32.store (i32.const 64) (f32.const 20.0))
    (i32.store (i32.const 68) (i32.const 0xFF45B7D1))
    
    ;; Ball 3
    (f32.store (i32.const 72) (f32.const 400.0))
    (f32.store (i32.const 76) (f32.const 250.0))
    (f32.store (i32.const 80) (f32.const -2.2))
    (f32.store (i32.const 84) (f32.const 1.8))
    (f32.store (i32.const 88) (f32.const 35.0))
    (i32.store (i32.const 92) (i32.const 0xFF96CEB4))
    
    ;; Ball 4
    (f32.store (i32.const 96) (f32.const 500.0))
    (f32.store (i32.const 100) (f32.const 300.0))
    (f32.store (i32.const 104) (f32.const 1.3))
    (f32.store (i32.const 108) (f32.const -2.5))
    (f32.store (i32.const 112) (f32.const 28.0))
    (i32.store (i32.const 116) (i32.const 0xFFFFEAA7))
    
    ;; Ball 5
    (f32.store (i32.const 120) (f32.const 150.0))
    (f32.store (i32.const 124) (f32.const 350.0))
    (f32.store (i32.const 128) (f32.const 2.5))
    (f32.store (i32.const 132) (f32.const 1.0))
    (f32.store (i32.const 136) (f32.const 22.0))
    (i32.store (i32.const 140) (i32.const 0xFF9B59B6))
    
    ;; Ball 6
    (f32.store (i32.const 144) (f32.const 350.0))
    (f32.store (i32.const 148) (f32.const 400.0))
    (f32.store (i32.const 152) (f32.const -1.7))
    (f32.store (i32.const 156) (f32.const -1.4))
    (f32.store (i32.const 160) (f32.const 32.0))
    (i32.store (i32.const 164) (i32.const 0xFFE67E22))
    
    ;; Ball 7
    (f32.store (i32.const 168) (f32.const 600.0))
    (f32.store (i32.const 172) (f32.const 200.0))
    (f32.store (i32.const 176) (f32.const -2.0))
    (f32.store (i32.const 180) (f32.const 2.2))
    (f32.store (i32.const 184) (f32.const 27.0))
    (i32.store (i32.const 188) (i32.const 0xFF1ABC9C))
  )

  ;; Update and draw a single ball
  (func $update_ball (param $idx i32) (param $width f32) (param $height f32)
    (local $offset i32)
    (local $x f32)
    (local $y f32)
    (local $vx f32)
    (local $vy f32)
    (local $radius f32)
    (local $color i32)
    
    ;; Calculate memory offset for this ball
    (local.set $offset (i32.mul (local.get $idx) (global.get $BALL_SIZE)))
    
    ;; Load ball state
    (local.set $x (f32.load (local.get $offset)))
    (local.set $y (f32.load (i32.add (local.get $offset) (i32.const 4))))
    (local.set $vx (f32.load (i32.add (local.get $offset) (i32.const 8))))
    (local.set $vy (f32.load (i32.add (local.get $offset) (i32.const 12))))
    (local.set $radius (f32.load (i32.add (local.get $offset) (i32.const 16))))
    (local.set $color (i32.load (i32.add (local.get $offset) (i32.const 20))))
    
    ;; Update position
    (local.set $x (f32.add (local.get $x) (local.get $vx)))
    (local.set $y (f32.add (local.get $y) (local.get $vy)))
    
    ;; Bounce off walls (left/right)
    (if (f32.lt (local.get $x) (local.get $radius))
      (then
        (local.set $x (local.get $radius))
        (local.set $vx (f32.neg (local.get $vx)))
      )
    )
    (if (f32.gt (local.get $x) (f32.sub (local.get $width) (local.get $radius)))
      (then
        (local.set $x (f32.sub (local.get $width) (local.get $radius)))
        (local.set $vx (f32.neg (local.get $vx)))
      )
    )
    
    ;; Bounce off walls (top/bottom)
    (if (f32.lt (local.get $y) (local.get $radius))
      (then
        (local.set $y (local.get $radius))
        (local.set $vy (f32.neg (local.get $vy)))
      )
    )
    (if (f32.gt (local.get $y) (f32.sub (local.get $height) (local.get $radius)))
      (then
        (local.set $y (f32.sub (local.get $height) (local.get $radius)))
        (local.set $vy (f32.neg (local.get $vy)))
      )
    )
    
    ;; Store updated state
    (f32.store (local.get $offset) (local.get $x))
    (f32.store (i32.add (local.get $offset) (i32.const 4)) (local.get $y))
    (f32.store (i32.add (local.get $offset) (i32.const 8)) (local.get $vx))
    (f32.store (i32.add (local.get $offset) (i32.const 12)) (local.get $vy))
    
    ;; Draw the ball
    (call $canvas_set_fill_color (local.get $color))
    (call $canvas_draw_circle (local.get $x) (local.get $y) (local.get $radius))
  )

  ;; Draw a rotating square at center
  (func $draw_rotating_square (param $cx f32) (param $cy f32) (param $angle f32)
    (local $size f32)
    
    (local.set $size (f32.const 60.0))
    
    (call $transform_save)
    (call $transform_translate (local.get $cx) (local.get $cy))
    (call $transform_rotate (local.get $angle))
    
    ;; Draw filled square
    (call $canvas_set_fill_color (i32.const 0xCC3498DB))
    (call $canvas_draw_rect
      (f32.neg (f32.div (local.get $size) (f32.const 2.0)))
      (f32.neg (f32.div (local.get $size) (f32.const 2.0)))
      (local.get $size)
      (local.get $size))
    
    ;; Draw outline
    (call $canvas_set_stroke_color (i32.const 0xFF2980B9))
    (call $canvas_set_stroke_width (f32.const 3.0))
    (call $canvas_draw_rect
      (f32.neg (f32.div (local.get $size) (f32.const 2.0)))
      (f32.neg (f32.div (local.get $size) (f32.const 2.0)))
      (local.get $size)
      (local.get $size))
    
    (call $transform_restore)
  )

  ;; Draw orbiting circles
  (func $draw_orbiting_circles (param $cx f32) (param $cy f32) (param $time f32)
    (local $i i32)
    (local $angle f32)
    (local $radius f32)
    (local $orbit_radius f32)
    (local $x f32)
    (local $y f32)
    (local $speed f32)
    
    (local.set $orbit_radius (f32.const 80.0))
    (local.set $radius (f32.const 15.0))
    (local.set $i (i32.const 0))
    
    (block $break
      (loop $continue
        ;; Calculate angle for this circle
        (local.set $speed (f32.add (f32.const 1.0) (f32.mul (f32.convert_i32_s (local.get $i)) (f32.const 0.5))))
        (local.set $angle (f32.add
          (f32.mul (f32.mul (local.get $time) (f32.const 0.002)) (local.get $speed))
          (f32.mul (f32.convert_i32_s (local.get $i)) (f32.div (global.get $TWO_PI) (f32.const 6.0)))))
        
        ;; Calculate position
        (local.set $x (f32.add (local.get $cx) (f32.mul (call $cos (local.get $angle)) (local.get $orbit_radius))))
        (local.set $y (f32.add (local.get $cy) (f32.mul (call $sin (local.get $angle)) (local.get $orbit_radius))))
        
        ;; Set color based on index
        (if (i32.eq (local.get $i) (i32.const 0))
          (then (call $canvas_set_fill_color (i32.const 0xFFE74C3C))))
        (if (i32.eq (local.get $i) (i32.const 1))
          (then (call $canvas_set_fill_color (i32.const 0xFFF39C12))))
        (if (i32.eq (local.get $i) (i32.const 2))
          (then (call $canvas_set_fill_color (i32.const 0xFF27AE60))))
        (if (i32.eq (local.get $i) (i32.const 3))
          (then (call $canvas_set_fill_color (i32.const 0xFF9B59B6))))
        (if (i32.eq (local.get $i) (i32.const 4))
          (then (call $canvas_set_fill_color (i32.const 0xFF3498DB))))
        (if (i32.eq (local.get $i) (i32.const 5))
          (then (call $canvas_set_fill_color (i32.const 0xFF1ABC9C))))
        
        ;; Draw circle
        (call $canvas_draw_circle (local.get $x) (local.get $y) (local.get $radius))
        
        ;; Increment and check loop condition
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $continue (i32.lt_s (local.get $i) (i32.const 6)))
      )
    )
  )

  ;; Draw pulsing ring
  (func $draw_pulsing_ring (param $cx f32) (param $cy f32) (param $time f32)
    (local $pulse f32)
    (local $base_radius f32)
    (local $radius f32)
    
    (local.set $base_radius (f32.const 40.0))
    
    ;; Calculate pulse (oscillates between 0.8 and 1.2)
    (local.set $pulse (f32.add
      (f32.const 1.0)
      (f32.mul (call $sin (f32.mul (local.get $time) (f32.const 0.005))) (f32.const 0.3))))
    
    (local.set $radius (f32.mul (local.get $base_radius) (local.get $pulse)))
    
    ;; Draw outer ring
    (call $canvas_set_stroke_color (i32.const 0xFFE91E63))
    (call $canvas_set_stroke_width (f32.const 4.0))
    (call $canvas_draw_circle (local.get $cx) (local.get $cy) (local.get $radius))
    
    ;; Draw inner ring (opposite pulse)
    (local.set $pulse (f32.sub (f32.const 2.0) (local.get $pulse)))
    (local.set $radius (f32.mul (f32.mul (local.get $base_radius) (f32.const 0.6)) (local.get $pulse)))
    
    (call $canvas_set_stroke_color (i32.const 0xFF9C27B0))
    (call $canvas_set_stroke_width (f32.const 3.0))
    (call $canvas_draw_circle (local.get $cx) (local.get $cy) (local.get $radius))
  )

  ;; Main frame function
  (func $frame (export "frame") (param $timestamp f32)
    (local $width f32)
    (local $height f32)
    (local $cx f32)
    (local $cy f32)
    (local $i i32)
    (local $rotation f32)
    
    ;; Get canvas dimensions
    (local.set $width (f32.convert_i32_s (call $window_get_width)))
    (local.set $height (f32.convert_i32_s (call $window_get_height)))
    (local.set $cx (f32.div (local.get $width) (f32.const 2.0)))
    (local.set $cy (f32.div (local.get $height) (f32.const 2.0)))
    
    ;; Clear canvas with dark background
    (call $canvas_clear (i32.const 0xFF1A1A2E))
    
    ;; Update and draw all bouncing balls
    (local.set $i (i32.const 0))
    (block $break
      (loop $continue
        (call $update_ball (local.get $i) (local.get $width) (local.get $height))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $continue (i32.lt_s (local.get $i) (global.get $NUM_BALLS)))
      )
    )
    
    ;; Draw central rotating square
    (local.set $rotation (f32.mul (local.get $timestamp) (f32.const 0.05)))
    (call $draw_rotating_square (local.get $cx) (local.get $cy) (local.get $rotation))
    
    ;; Draw orbiting circles around center
    (call $draw_orbiting_circles (local.get $cx) (local.get $cy) (local.get $timestamp))
    
    ;; Draw pulsing ring in the corner
    (call $draw_pulsing_ring (f32.const 100.0) (f32.const 100.0) (local.get $timestamp))
    (call $draw_pulsing_ring
      (f32.sub (local.get $width) (f32.const 100.0))
      (f32.sub (local.get $height) (f32.const 100.0))
      (local.get $timestamp))
  )
)
