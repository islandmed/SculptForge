;; SculptForge sample plugin: Noise Displace (a deformer). Moves every vertex along its normal by smooth 3D value
;; noise: amount (in scene units), scale (noise cells per unit) and seed. ABI 1 (src/core/Plugin.h); written by hand
;; in WebAssembly text so the tests need no compiler (sdk/plugin/examples/noise_displace.c is the same in C).
;; The manifest is added as a custom section by tools/plugin/sfplugin.py, or pluginWithManifest() in the tests:
;;   tests/data/plugins/noise_displace.json
(module
  (memory (export "memory") 1)
  (global $heap (mut i32) (i32.const 1024)) ;; 16..48: the output record; the heap starts at 1024

  ;; A bump allocator: 8-byte aligned, grows the memory as needed, 0 when it can't.
  (func (export "sf_alloc") (param $n i32) (result i32)
    (local $p i32) (local $end i32) (local $need i32)
    (local.set $p (i32.and (i32.add (global.get $heap) (i32.const 7)) (i32.const -8)))
    (local.set $end (i32.add (local.get $p) (local.get $n)))
    (if (i32.or (i32.lt_u (local.get $end) (local.get $p)) (i32.gt_u (local.get $end) (i32.const 0xFFFF0000)))
      (then (return (i32.const 0))))
    (local.set $need (i32.sub (i32.shr_u (i32.add (local.get $end) (i32.const 65535)) (i32.const 16)) (memory.size)))
    (if (i32.gt_s (local.get $need) (i32.const 0))
      (then (if (i32.eq (memory.grow (local.get $need)) (i32.const -1)) (then (return (i32.const 0))))))
    (global.set $heap (local.get $end))
    (local.get $p))

  ;; A lattice point's value in [-1, 1).
  (func $hash (param $x i32) (param $y i32) (param $z i32) (param $s i32) (result f32)
    (local $h i32)
    (local.set $h (i32.xor (i32.xor (i32.mul (local.get $x) (i32.const 73856093))
                                    (i32.mul (local.get $y) (i32.const 19349663)))
                           (i32.xor (i32.mul (local.get $z) (i32.const 83492791))
                                    (i32.mul (local.get $s) (i32.const -1640531535)))))
    (local.set $h (i32.mul (i32.xor (local.get $h) (i32.shr_u (local.get $h) (i32.const 16))) (i32.const 0x45d9f3b)))
    (local.set $h (i32.mul (i32.xor (local.get $h) (i32.shr_u (local.get $h) (i32.const 16))) (i32.const 0x45d9f3b)))
    (local.set $h (i32.xor (local.get $h) (i32.shr_u (local.get $h) (i32.const 16))))
    (f32.sub (f32.mul (f32.convert_i32_u (i32.and (local.get $h) (i32.const 0xFFFFFF))) (f32.const 1.1920929e-7))
             (f32.const 1)))

  (func $fade (param $t f32) (result f32)
    (f32.mul (f32.mul (local.get $t) (local.get $t)) (f32.sub (f32.const 3) (f32.mul (f32.const 2) (local.get $t)))))

  (func $lerp (param $a f32) (param $b f32) (param $t f32) (result f32)
    (f32.add (local.get $a) (f32.mul (f32.sub (local.get $b) (local.get $a)) (local.get $t))))

  ;; Smooth value noise: the eight lattice values around the point, blended.
  (func $noise (param $x f32) (param $y f32) (param $z f32) (param $s i32) (result f32)
    (local $fx f32) (local $fy f32) (local $fz f32) (local $u f32) (local $v f32) (local $w f32)
    (local $ix i32) (local $iy i32) (local $iz i32) (local $jx i32) (local $jy i32) (local $jz i32)
    (local.set $fx (f32.floor (local.get $x)))
    (local.set $fy (f32.floor (local.get $y)))
    (local.set $fz (f32.floor (local.get $z)))
    (local.set $ix (i32.trunc_sat_f32_s (local.get $fx)))
    (local.set $iy (i32.trunc_sat_f32_s (local.get $fy)))
    (local.set $iz (i32.trunc_sat_f32_s (local.get $fz)))
    (local.set $jx (i32.add (local.get $ix) (i32.const 1)))
    (local.set $jy (i32.add (local.get $iy) (i32.const 1)))
    (local.set $jz (i32.add (local.get $iz) (i32.const 1)))
    (local.set $u (call $fade (f32.sub (local.get $x) (local.get $fx))))
    (local.set $v (call $fade (f32.sub (local.get $y) (local.get $fy))))
    (local.set $w (call $fade (f32.sub (local.get $z) (local.get $fz))))
    (call $lerp
      (call $lerp
        (call $lerp (call $hash (local.get $ix) (local.get $iy) (local.get $iz) (local.get $s))
                    (call $hash (local.get $jx) (local.get $iy) (local.get $iz) (local.get $s)) (local.get $u))
        (call $lerp (call $hash (local.get $ix) (local.get $jy) (local.get $iz) (local.get $s))
                    (call $hash (local.get $jx) (local.get $jy) (local.get $iz) (local.get $s)) (local.get $u))
        (local.get $v))
      (call $lerp
        (call $lerp (call $hash (local.get $ix) (local.get $iy) (local.get $jz) (local.get $s))
                    (call $hash (local.get $jx) (local.get $iy) (local.get $jz) (local.get $s)) (local.get $u))
        (call $lerp (call $hash (local.get $ix) (local.get $jy) (local.get $jz) (local.get $s))
                    (call $hash (local.get $jx) (local.get $jy) (local.get $jz) (local.get $s)) (local.get $u))
        (local.get $v))
      (local.get $w)))

  ;; Displaces the input positions in place and returns them.
  (func (export "sf_run") (param $in i32) (result i32)
    (local $n i32) (local $pos i32) (local $nrm i32) (local $prm i32) (local $i i32) (local $p i32) (local $q i32)
    (local $amount f32) (local $scale f32) (local $seed i32) (local $d f32)
    (local.set $n (i32.load offset=8 (local.get $in)))
    (local.set $pos (i32.load offset=20 (local.get $in)))
    (local.set $nrm (i32.load offset=24 (local.get $in)))
    (local.set $prm (i32.load offset=36 (local.get $in)))
    (local.set $amount (f32.demote_f64 (f64.load (local.get $prm))))
    (local.set $scale (f32.demote_f64 (f64.load offset=8 (local.get $prm))))
    (local.set $seed (i32.trunc_sat_f64_s (f64.load offset=16 (local.get $prm))))
    (block $done
      (loop $each
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $p (i32.add (local.get $pos) (i32.mul (local.get $i) (i32.const 12))))
        (local.set $q (i32.add (local.get $nrm) (i32.mul (local.get $i) (i32.const 12))))
        (local.set $d (f32.mul (local.get $amount)
          (call $noise (f32.mul (f32.load (local.get $p)) (local.get $scale))
                       (f32.mul (f32.load offset=4 (local.get $p)) (local.get $scale))
                       (f32.mul (f32.load offset=8 (local.get $p)) (local.get $scale))
                       (local.get $seed))))
        (f32.store (local.get $p)
          (f32.add (f32.load (local.get $p)) (f32.mul (f32.load (local.get $q)) (local.get $d))))
        (f32.store offset=4 (local.get $p)
          (f32.add (f32.load offset=4 (local.get $p)) (f32.mul (f32.load offset=4 (local.get $q)) (local.get $d))))
        (f32.store offset=8 (local.get $p)
          (f32.add (f32.load offset=8 (local.get $p)) (f32.mul (f32.load offset=8 (local.get $q)) (local.get $d))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $each)))
    (i32.store (i32.const 16) (i32.const 1))      ;; abi
    (i32.store (i32.const 20) (i32.const 0))      ;; status: ok
    (i32.store (i32.const 24) (local.get $n))     ;; vertex_count
    (i32.store (i32.const 28) (i32.const 0))      ;; triangle_count (a deformer keeps them)
    (i32.store (i32.const 32) (local.get $pos))   ;; positions
    (i32.store (i32.const 36) (i32.const 0))      ;; triangles
    (i32.store (i32.const 40) (i32.const 0))      ;; message
    (i32.store (i32.const 44) (i32.const 0))      ;; message_len
    (i32.const 16))
)
