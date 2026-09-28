;; WebAssembly Interactive Demo
;; Demonstrates mouse and keyboard interaction with the Skia canvas API
;;
;; This example shows:
;; - Mouse tracking and cursor following
;; - Click detection for drawing
;; - Particle effects
;; - Trail effects following the mouse

(module
  ;; Import canvas functions
  (import "env" "canvas_clear" (func $canvas_clear (param i32)))
  (import "env" "canvas_draw_rect" (func $canvas_draw_rect (param f32 f32 f32 f32)))
  (import "env" "canvas_draw_circle" (func $canvas_draw_circle (param f32 f32 f32)))
  (import "env" "canvas_draw_line" (func $canvas_draw_line (param f32 f32 f32 f32)))
  (import "env" "canvas_set_fill_color" (func $canvas_set_fill_color (param i32)))
  (import "env" "canvas_set_stroke_color" (func $canvas_set_stroke_color (param i32)))
  (import "env" "canvas_set_stroke_width" (func $canvas_set_stroke_width (param f32)))
  (import "env" "canvas_set_alpha" (func $canvas_set_alpha (param i32)))
  (import "env" "canvas_set_anti_alias" (func $canvas_set_anti_alias (param i32)))
  (import "env" "window_get_width" (func $window_get_width (result i32)))
  (import "env" "window_get_height" (func $window_get_height (result i32)))
  
  ;; Input functions
  (import "env" "input_get_mouse_x" (func $input_get_mouse_x (result i32)))
  (import "env" "input_get_mouse_y" (func $input_get_mouse_y (result i32)))
  (import "env" "input_get_mouse_button" (func $input_get_mouse_button (param i32) (result i32)))
  
  ;; Math functions
  (import "env" "sin" (func $sin (param f32) (result f32)))
  (import "env" "cos" (func $cos (param f32) (result f32)))
  (import "env" "sqrt" (func $sqrt (param f32) (result f32)))
  
  ;; Memory for trail points and particles
  ;; Trail: 30 points, each with x, y (8 bytes each = 240 bytes)
  ;; Particles: stored after trail, 50 particles with x, y, vx, vy, life, color (24 bytes each)
  (memory (export "memory") 1)
  
  ;; Constants
  (global $TRAIL_SIZE i32 (i32.const 30))
  (global $TRAIL_START i32 (i32.const 0))
  (global $PARTICLE_START i32 (i32.const 256))
  (global $MAX_PARTICLES i32 (i32.const 50))
  (global $PARTICLE_SIZE i32 (i32.const 24))
  
  ;; State
  (global $trail_index (mut i32) (i32.const 0))
  (global $particle_spawn_timer (mut f32) (f32.const 0.0))
  (global $prev_mouse_x (mut f32) (f32.const 0.0))
  (global $prev_mouse_y (mut f32) (f32.const 0.0))
  
  ;; Color palette
  (global $COLOR_0 i32 (i32.const 0xFFFF6B6B))
  (global $COLOR_1 i32 (i32.const 0xFF4ECDC4))
  (global $COLOR_2 i32 (i32.const 0xFF45B7D1))
  (global $COLOR_3 i32 (i32.const 0xFFFFEAA7))
  (global $COLOR_4 i32 (i32.const 0xFF9B59B6))
  (global $COLOR_5 i32 (i32.const 0xFF1ABC9C))
  
  ;; Initialization
  (func $init (export "init")
    (local $i i32)
    (local $offset i32)
    
    (call $canvas_set_anti_alias (i32.const 1))
    
    ;; Initialize trail points to center
    (local.set $i (i32.const 0))
    (block $break
      (loop $continue
        (local.set $offset (i32.add (global.get $TRAIL_START) (i32.mul (local.get $i) (i32.const 8))))
        (f32.store (local.get $offset) (f32.const 400.0))
        (f32.store (i32.add (local.get $offset) (i32.const 4)) (f32.const 300.0))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $continue (i32.lt_s (local.get $i) (global.get $TRAIL_SIZE)))
      )
    )
    
    ;; Initialize particles to dead state (life = 0)
    (local.set $i (i32.const 0))
    (block $break2
      (loop $continue2
        (local.set $offset (i32.add (global.get $PARTICLE_START) (i32.mul (local.get $i) (global.get $PARTICLE_SIZE))))
        (f32.store (i32.add (local.get $offset) (i32.const 16)) (f32.const 0.0)) ;; life = 0
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $continue2 (i32.lt_s (local.get $i) (global.get $MAX_PARTICLES)))
      )
    )
  )
  
  ;; Simple random number generator (LCG)
  (global $rand_seed (mut i32) (i32.const 12345))
  
  (func $random (result f32)
    (local $next i32)
    (local.set $next (i32.add
      (i32.mul (global.get $rand_seed) (i32.const 1103515245))
      (i32.const 12345)))
    (global.set $rand_seed (local.get $next))
    ;; Return value between 0 and 1
    (f32.div
      (f32.convert_i32_u (i32.and (local.get $next) (i32.const 0x7FFFFFFF)))
      (f32.const 2147483647.0))
  )
  
  ;; Spawn a particle at position
  (func $spawn_particle (param $x f32) (param $y f32)
    (local $i i32)
    (local $offset i32)
    (local $life f32)
    (local $angle f32)
    (local $speed f32)
    (local $color_idx i32)
    (local $color i32)
    
    ;; Find a dead particle slot
    (local.set $i (i32.const 0))
    (block $found
      (loop $search
        (local.set $offset (i32.add (global.get $PARTICLE_START) (i32.mul (local.get $i) (global.get $PARTICLE_SIZE))))
        (local.set $life (f32.load (i32.add (local.get $offset) (i32.const 16))))
        
        (if (f32.le (local.get $life) (f32.const 0.0))
          (then
            ;; Found a dead particle, spawn new one
            (local.set $angle (f32.mul (call $random) (f32.const 6.28318)))
            (local.set $speed (f32.add (f32.mul (call $random) (f32.const 3.0)) (f32.const 1.0)))
            
            ;; x, y
            (f32.store (local.get $offset) (local.get $x))
            (f32.store (i32.add (local.get $offset) (i32.const 4)) (local.get $y))
            ;; vx, vy
            (f32.store (i32.add (local.get $offset) (i32.const 8))
              (f32.mul (call $cos (local.get $angle)) (local.get $speed)))
            (f32.store (i32.add (local.get $offset) (i32.const 12))
              (f32.mul (call $sin (local.get $angle)) (local.get $speed)))
            ;; life (1.0 to 0.0)
            (f32.store (i32.add (local.get $offset) (i32.const 16)) (f32.const 1.0))
            ;; color (random from palette)
            (local.set $color_idx (i32.trunc_f32_s (f32.mul (call $random) (f32.const 6.0))))
            (if (i32.eq (local.get $color_idx) (i32.const 0))
              (then (local.set $color (global.get $COLOR_0))))
            (if (i32.eq (local.get $color_idx) (i32.const 1))
              (then (local.set $color (global.get $COLOR_1))))
            (if (i32.eq (local.get $color_idx) (i32.const 2))
              (then (local.set $color (global.get $COLOR_2))))
            (if (i32.eq (local.get $color_idx) (i32.const 3))
              (then (local.set $color (global.get $COLOR_3))))
            (if (i32.eq (local.get $color_idx) (i32.const 4))
              (then (local.set $color (global.get $COLOR_4))))
            (if (i32.ge_s (local.get $color_idx) (i32.const 5))
              (then (local.set $color (global.get $COLOR_5))))
            (i32.store (i32.add (local.get $offset) (i32.const 20)) (local.get $color))
            
            (br $found)
          )
        )
        
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $search (i32.lt_s (local.get $i) (global.get $MAX_PARTICLES)))
      )
    )
  )
  
  ;; Update and draw particles
  (func $update_particles
    (local $i i32)
    (local $offset i32)
    (local $x f32)
    (local $y f32)
    (local $vx f32)
    (local $vy f32)
    (local $life f32)
    (local $color i32)
    (local $alpha i32)
    (local $radius f32)
    
    (local.set $i (i32.const 0))
    (block $break
      (loop $continue
        (local.set $offset (i32.add (global.get $PARTICLE_START) (i32.mul (local.get $i) (global.get $PARTICLE_SIZE))))
        (local.set $life (f32.load (i32.add (local.get $offset) (i32.const 16))))
        
        (if (f32.gt (local.get $life) (f32.const 0.0))
          (then
            ;; Load particle data
            (local.set $x (f32.load (local.get $offset)))
            (local.set $y (f32.load (i32.add (local.get $offset) (i32.const 4))))
            (local.set $vx (f32.load (i32.add (local.get $offset) (i32.const 8))))
            (local.set $vy (f32.load (i32.add (local.get $offset) (i32.const 12))))
            (local.set $color (i32.load (i32.add (local.get $offset) (i32.const 20))))
            
            ;; Update position
            (local.set $x (f32.add (local.get $x) (local.get $vx)))
            (local.set $y (f32.add (local.get $y) (local.get $vy)))
            
            ;; Apply gravity
            (local.set $vy (f32.add (local.get $vy) (f32.const 0.1)))
            
            ;; Decrease life
            (local.set $life (f32.sub (local.get $life) (f32.const 0.02)))
            
            ;; Store updated values
            (f32.store (local.get $offset) (local.get $x))
            (f32.store (i32.add (local.get $offset) (i32.const 4)) (local.get $y))
            (f32.store (i32.add (local.get $offset) (i32.const 12)) (local.get $vy))
            (f32.store (i32.add (local.get $offset) (i32.const 16)) (local.get $life))
            
            ;; Draw particle with alpha based on life
            (local.set $alpha (i32.trunc_f32_s (f32.mul (local.get $life) (f32.const 255.0))))
            (local.set $radius (f32.add (f32.mul (local.get $life) (f32.const 8.0)) (f32.const 2.0)))
            
            ;; Modify color alpha
            (local.set $color (i32.or
              (i32.and (local.get $color) (i32.const 0x00FFFFFF))
              (i32.shl (local.get $alpha) (i32.const 24))))
            
            (call $canvas_set_fill_color (local.get $color))
            (call $canvas_draw_circle (local.get $x) (local.get $y) (local.get $radius))
          )
        )
        
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $continue (i32.lt_s (local.get $i) (global.get $MAX_PARTICLES)))
      )
    )
  )
  
  ;; Update trail with new position
  (func $update_trail (param $x f32) (param $y f32)
    (local $offset i32)
    
    ;; Store position at current index
    (local.set $offset (i32.add (global.get $TRAIL_START) (i32.mul (global.get $trail_index) (i32.const 8))))
    (f32.store (local.get $offset) (local.get $x))
    (f32.store (i32.add (local.get $offset) (i32.const 4)) (local.get $y))
    
    ;; Increment and wrap index
    (global.set $trail_index
      (i32.rem_s
        (i32.add (global.get $trail_index) (i32.const 1))
        (global.get $TRAIL_SIZE)))
  )
  
  ;; Draw the trail
  (func $draw_trail
    (local $i i32)
    (local $idx i32)
    (local $offset i32)
    (local $x f32)
    (local $y f32)
    (local $prev_x f32)
    (local $prev_y f32)
    (local $alpha i32)
    (local $width f32)
    (local $first i32)
    
    (local.set $first (i32.const 1))
    (local.set $i (i32.const 0))
    
    (block $break
      (loop $continue
        ;; Calculate actual index (oldest to newest)
        (local.set $idx (i32.rem_s
          (i32.add (global.get $trail_index) (local.get $i))
          (global.get $TRAIL_SIZE)))
        
        (local.set $offset (i32.add (global.get $TRAIL_START) (i32.mul (local.get $idx) (i32.const 8))))
        (local.set $x (f32.load (local.get $offset)))
        (local.set $y (f32.load (i32.add (local.get $offset) (i32.const 4))))
        
        (if (local.get $first)
          (then
            (local.set $first (i32.const 0))
          )
          (else
            ;; Calculate alpha and width based on position in trail
            (local.set $alpha (i32.trunc_f32_s
              (f32.mul (f32.div (f32.convert_i32_s (local.get $i)) (f32.convert_i32_s (global.get $TRAIL_SIZE))) (f32.const 200.0))))
            (local.set $alpha (i32.add (local.get $alpha) (i32.const 55)))
            
            (local.set $width (f32.add
              (f32.mul (f32.div (f32.convert_i32_s (local.get $i)) (f32.convert_i32_s (global.get $TRAIL_SIZE))) (f32.const 6.0))
              (f32.const 2.0)))
            
            ;; Draw line segment with gradient color
            (call $canvas_set_stroke_color (i32.or (i32.const 0x004ECDC4) (i32.shl (local.get $alpha) (i32.const 24))))
            (call $canvas_set_stroke_width (local.get $width))
            (call $canvas_draw_line (local.get $prev_x) (local.get $prev_y) (local.get $x) (local.get $y))
          )
        )
        
        (local.set $prev_x (local.get $x))
        (local.set $prev_y (local.get $y))
        
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $continue (i32.lt_s (local.get $i) (global.get $TRAIL_SIZE)))
      )
    )
  )
  
  ;; Draw cursor with ripple effect
  (func $draw_cursor (param $x f32) (param $y f32) (param $time f32) (param $clicked i32)
    (local $ripple f32)
    (local $radius f32)
    
    ;; Outer ripple when clicking
    (if (local.get $clicked)
      (then
        (local.set $ripple (f32.mul
          (call $sin (f32.mul (local.get $time) (f32.const 0.02)))
          (f32.const 10.0)))
        (local.set $radius (f32.add (f32.const 25.0) (local.get $ripple)))
        
        (call $canvas_set_stroke_color (i32.const 0x80FF6B6B))
        (call $canvas_set_stroke_width (f32.const 3.0))
        (call $canvas_draw_circle (local.get $x) (local.get $y) (local.get $radius))
        
        (call $canvas_set_stroke_color (i32.const 0x60FF6B6B))
        (call $canvas_set_stroke_width (f32.const 2.0))
        (call $canvas_draw_circle (local.get $x) (local.get $y) (f32.add (local.get $radius) (f32.const 10.0)))
      )
    )
    
    ;; Inner circle
    (call $canvas_set_fill_color (i32.const 0xFFFFFFFF))
    (call $canvas_draw_circle (local.get $x) (local.get $y) (f32.const 12.0))
    
    ;; Outline
    (call $canvas_set_stroke_color (i32.const 0xFF4ECDC4))
    (call $canvas_set_stroke_width (f32.const 3.0))
    (call $canvas_draw_circle (local.get $x) (local.get $y) (f32.const 12.0))
    
    ;; Center dot
    (call $canvas_set_fill_color (i32.const 0xFF4ECDC4))
    (call $canvas_draw_circle (local.get $x) (local.get $y) (f32.const 4.0))
  )
  
  ;; Main frame function
  (func $frame (export "frame") (param $timestamp f32)
    (local $width f32)
    (local $height f32)
    (local $mouse_x f32)
    (local $mouse_y f32)
    (local $left_click i32)
    (local $dx f32)
    (local $dy f32)
    (local $dist f32)
    
    ;; Get dimensions
    (local.set $width (f32.convert_i32_s (call $window_get_width)))
    (local.set $height (f32.convert_i32_s (call $window_get_height)))
    
    ;; Get mouse position
    (local.set $mouse_x (f32.convert_i32_s (call $input_get_mouse_x)))
    (local.set $mouse_y (f32.convert_i32_s (call $input_get_mouse_y)))
    (local.set $left_click (call $input_get_mouse_button (i32.const 0)))
    
    ;; Clear canvas with dark blue background
    (call $canvas_clear (i32.const 0xFF16213E))
    
    ;; Update trail (every frame)
    (call $update_trail (local.get $mouse_x) (local.get $mouse_y))
    
    ;; Spawn particles when clicking
    (if (local.get $left_click)
      (then
        ;; Spawn multiple particles per frame
        (call $spawn_particle (local.get $mouse_x) (local.get $mouse_y))
        (call $spawn_particle (local.get $mouse_x) (local.get $mouse_y))
        (call $spawn_particle (local.get $mouse_x) (local.get $mouse_y))
      )
    )
    
    ;; Also spawn particles when moving fast
    (local.set $dx (f32.sub (local.get $mouse_x) (global.get $prev_mouse_x)))
    (local.set $dy (f32.sub (local.get $mouse_y) (global.get $prev_mouse_y)))
    (local.set $dist (call $sqrt (f32.add (f32.mul (local.get $dx) (local.get $dx)) (f32.mul (local.get $dy) (local.get $dy)))))
    
    (if (f32.gt (local.get $dist) (f32.const 30.0))
      (then
        (call $spawn_particle (local.get $mouse_x) (local.get $mouse_y))
      )
    )
    
    ;; Store previous mouse position
    (global.set $prev_mouse_x (local.get $mouse_x))
    (global.set $prev_mouse_y (local.get $mouse_y))
    
    ;; Draw trail first (behind everything)
    (call $draw_trail)
    
    ;; Update and draw particles
    (call $update_particles)
    
    ;; Draw cursor on top
    (call $draw_cursor (local.get $mouse_x) (local.get $mouse_y) (local.get $timestamp) (local.get $left_click))
    
    ;; Draw instruction text area (simple rectangle as placeholder)
    (call $canvas_set_fill_color (i32.const 0x80000000))
    (call $canvas_draw_rect (f32.const 10.0) (f32.const 10.0) (f32.const 280.0) (f32.const 50.0))
    
    ;; Draw decorative corners
    (call $canvas_set_stroke_color (i32.const 0xFF4ECDC4))
    (call $canvas_set_stroke_width (f32.const 2.0))
    ;; Top-left corner
    (call $canvas_draw_line (f32.const 10.0) (f32.const 10.0) (f32.const 30.0) (f32.const 10.0))
    (call $canvas_draw_line (f32.const 10.0) (f32.const 10.0) (f32.const 10.0) (f32.const 30.0))
    ;; Top-right corner of instruction box
    (call $canvas_draw_line (f32.const 270.0) (f32.const 10.0) (f32.const 290.0) (f32.const 10.0))
    (call $canvas_draw_line (f32.const 290.0) (f32.const 10.0) (f32.const 290.0) (f32.const 30.0))
  )
)
