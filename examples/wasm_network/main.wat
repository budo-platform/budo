;; WASM Network fetch demo
;;
;; Issues a single HTTPS GET via the host network bindings, polls each
;; frame for completion, then renders a status circle:
;;   yellow  — pending
;;   green   — HTTP 2xx
;;   red     — error or non-2xx
;;
;; Memory layout (offsets into linear memory):
;;    0  : "GET"            (3 bytes)
;;   16  : URL              (~64 bytes)
;;  128  : empty headers    (0 bytes)
;;  256  : response body buffer (1024 bytes max)

(module
  ;; Network host imports
  (import "env" "network_fetch"
    (func $network_fetch (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "network_fetch_status"
    (func $network_fetch_status (param i32) (result i32)))
  (import "env" "network_fetch_status_code"
    (func $network_fetch_status_code (param i32) (result i32)))
  (import "env" "network_fetch_response_body_len"
    (func $network_fetch_response_body_len (param i32) (result i32)))
  (import "env" "network_fetch_response_body"
    (func $network_fetch_response_body (param i32 i32 i32) (result i32)))
  (import "env" "network_fetch_release"
    (func $network_fetch_release (param i32)))

  ;; Canvas / paint imports for the status indicator
  (import "env" "canvas_clear"          (func $canvas_clear         (param i32)))
  (import "env" "canvas_draw_circle"    (func $canvas_draw_circle   (param f32 f32 f32)))
  (import "env" "canvas_set_fill_color"  (func $canvas_set_fill_color (param i32)))
  (import "env" "window_get_width"      (func $window_get_width     (result i32)))
  (import "env" "window_get_height"     (func $window_get_height    (result i32)))

  (memory (export "memory") 1)

  ;; "GET"
  (data (i32.const 0) "GET")
  ;; URL — small JSON endpoint, returns ~25 bytes
  (data (i32.const 16) "https://httpbin.org/uuid")

  ;; Bookkeeping
  (global $request_id (mut i32) (i32.const -1))
  (global $http_status (mut i32) (i32.const 0))
  (global $body_len (mut i32) (i32.const 0))
  (global $finished (mut i32) (i32.const 0))

  (func $init (export "init")
    ;; nothing — fetch is issued lazily from frame() so that the host
    ;; has wired up linear memory by the time we call network_fetch.
    nop)

  (func $frame (export "frame") (param $timestamp f32)
    (local $w f32)
    (local $h f32)
    (local $color i32)
    (local $status i32)

    (call $canvas_clear (i32.const 0xFF202020))

    ;; Lazy-issue the request on first frame
    (if (i32.eq (global.get $request_id) (i32.const -1))
      (then
        (global.set $request_id
          (call $network_fetch
            (i32.const 0)   (i32.const 3)    ;; method ptr/len
            (i32.const 16)  (i32.const 24)   ;; url ptr/len
            (i32.const 128) (i32.const 0)    ;; headers ptr/len
            (i32.const 0)   (i32.const 0))))) ;; body ptr/len

    (local.set $w (f32.convert_i32_s (call $window_get_width)))
    (local.set $h (f32.convert_i32_s (call $window_get_height)))

    ;; Default: yellow (pending)
    (local.set $color (i32.const 0xFFFFD400))

    ;; If we have a request and haven't finalized yet, poll
    (if (i32.and
          (i32.ge_s (global.get $request_id) (i32.const 0))
          (i32.eqz (global.get $finished)))
      (then
        (local.set $status (call $network_fetch_status (global.get $request_id)))
        (if (i32.eq (local.get $status) (i32.const 1))
          (then
            (global.set $http_status
              (call $network_fetch_status_code (global.get $request_id)))
            (global.set $body_len
              (call $network_fetch_response_body_len (global.get $request_id)))
            ;; Drain body into our buffer just to exercise the call
            (drop
              (call $network_fetch_response_body
                (global.get $request_id)
                (i32.const 256)
                (i32.const 1024)))
            (call $network_fetch_release (global.get $request_id))
            (global.set $finished (i32.const 1))))))

    (if (global.get $finished)
      (then
        (if (i32.and
              (i32.ge_s (global.get $http_status) (i32.const 200))
              (i32.lt_s (global.get $http_status) (i32.const 300)))
          (then (local.set $color (i32.const 0xFF44CC44)))    ;; green
          (else (local.set $color (i32.const 0xFFCC4444)))))) ;; red

    (call $canvas_set_fill_color (local.get $color))
    (call $canvas_draw_circle
      (f32.div (local.get $w) (f32.const 2.0))
      (f32.div (local.get $h) (f32.const 2.0))
      (f32.const 60.0))))
