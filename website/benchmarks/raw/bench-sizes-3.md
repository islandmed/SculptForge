## With the GPU (sculptforge --bench)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 195 |
| BVH build | 283 |
| First GPU upload | 331 |
| GPU memory (buffers) | 59 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 146 MB = 37 B/triangle |
| BVH | 68 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 117 MB = 29 B/triangle |
| Process memory (RSS) | 487 MB (+403 MB = 101 B/triangle) |
| Peak while dividing (RSS) | 403 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 26k (15) | 0.64 | 0.74 | 0.51 | 0.00 | 0.88 | **2.89** | 0.10 | 2.2 | 2.0 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 159k (33) | 1.72 | 1.62 | 1.00 | 0.00 | 2.62 | **7.24** | 0.12 | 6.1 | 5.5 | 4.6 MB |
| Clay | 7k | 17k (10) | 0.42 | 0.60 | 0.29 | 0.00 | 0.58 | **1.97** | 0.02 | 1.4 | 1.4 | 0.9 MB |
| Clay Block | 8k | 20k (11) | 0.58 | 0.61 | 0.36 | 0.00 | 1.57 | **3.21** | 0.02 | 2.0 | 2.1 | 0.8 MB |
| Smooth | 5k | 14k (9) | 0.60 | 0.42 | 0.20 | 0.00 | 0.56 | **1.82** | 0.03 | 1.5 | 1.7 | 0.6 MB |
| Smooth (large, r 0.4) | 58k | 85k (23) | 2.54 | 1.07 | 0.71 | 0.00 | 1.68 | **6.30** | 0.07 | 4.8 | 3.5 | 2.5 MB |
| Inflate | 5k | 13k (9) | 0.28 | 0.42 | 0.20 | 0.00 | 0.60 | **1.56** | 0.09 | 1.5 | 1.8 | 0.6 MB |
| Sharp Crease | 4k | 13k (9) | 0.29 | 0.39 | 0.19 | 0.00 | 0.55 | **1.48** | 0.03 | 1.7 | 1.0 | 0.5 MB |
| Layer | 5k | 13k (9) | 0.39 | 0.44 | 0.20 | 0.00 | 0.59 | **1.68** | 0.03 | 1.4 | 1.5 | 0.6 MB |
| Move | 4k | 10k (8) | 0.04 | 0.23 | 0.15 | 0.00 | 0.68 | **1.14** | 0.02 | 1.0 | 0.4 | 0.1 MB |
| Move (large, r 0.4) | 42k | 63k (20) | 0.19 | 0.81 | 0.40 | 0.00 | 1.34 | **3.02** | 0.04 | 1.5 | 2.1 | 0.7 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.55 | 0.45 | 0.20 | 0.00 | 0.62 | **1.87** | 0.03 | 1.9 | 1.9 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.23 | 0.00 | 0.00 | 0.00 | 0.88 | **1.16** | 0.03 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.25 | 0.00 | 0.00 | 0.00 | 0.88 | **1.19** | 0.03 | 0.2 | 0.2 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 995k, after edits) | 120 |
| Higher Level (back up) | 4 |
| Lower Level again (no edits) | 4 |
| Higher Level again (no edits) | 1 |
| Motion proxy build (first move after an upload) | 17 |
| Orbit: frame time, 4x antialiasing | 0.5 (1960 fps) |
| Orbit: frame time, no antialiasing | 0.2 (4562 fps) |
| Orbit: triangles drawn | 164k (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.2 (5141 fps) |
| Orbit: triangles drawn (motion proxy) | 164k (4%) |
| GPU memory with the motion proxy | 76 MB = 19.1 B/triangle |

### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 701 |
| BVH build | 997 |
| First GPU upload | 1489 |
| GPU memory (buffers) | 235 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 583 MB = 37 B/triangle |
| BVH | 272 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 470 MB = 29 B/triangle |
| Process memory (RSS) | 1792 MB (+1217 MB = 76 B/triangle) |
| Peak while dividing (RSS) | 1272 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 80k (27) | 1.25 | 1.21 | 0.64 | 0.00 | 1.88 | **5.30** | 0.09 | 6.8 | 8.2 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 561k (62) | 4.95 | 0.80 | 2.83 | 0.01 | 9.19 | **18.34** | 13.95 | 21.3 | 22.1 | 18.5 MB |
| Clay | 28k | 49k (19) | 1.04 | 0.84 | 0.59 | 0.00 | 1.35 | **4.09** | 0.08 | 4.3 | 4.3 | 3.5 MB |
| Clay Block | 33k | 56k (22) | 1.07 | 0.87 | 0.57 | 0.00 | 4.43 | **7.20** | 0.07 | 6.3 | 6.1 | 3.8 MB |
| Smooth | 19k | 37k (17) | 2.11 | 0.75 | 0.66 | 0.00 | 1.04 | **4.77** | 0.08 | 3.8 | 4.0 | 2.7 MB |
| Smooth (large, r 0.4) | 233k | 293k (46) | 5.94 | 3.03 | 1.56 | 0.01 | 4.98 | **15.90** | 0.13 | 15.8 | 19.0 | 11.0 MB |
| Inflate | 18k | 36k (17) | 0.76 | 0.76 | 0.65 | 0.00 | 1.08 | **3.44** | 0.07 | 3.6 | 3.9 | 2.6 MB |
| Sharp Crease | 18k | 35k (17) | 0.76 | 0.64 | 0.62 | 0.00 | 1.02 | **3.23** | 0.07 | 3.5 | 3.4 | 2.5 MB |
| Layer | 19k | 36k (18) | 1.17 | 0.68 | 0.63 | 0.00 | 1.01 | **3.69** | 0.07 | 3.8 | 3.5 | 2.6 MB |
| Move | 16k | 32k (13) | 0.14 | 0.63 | 0.22 | 0.00 | 1.71 | **2.86** | 0.02 | 0.7 | 0.8 | 0.3 MB |
| Move (large, r 0.4) | 168k | 217k (37) | 0.39 | 1.88 | 1.01 | 0.01 | 3.76 | **7.42** | 0.06 | 4.4 | 4.2 | 2.7 MB |
| Recipe (pattern) | 19k | 36k (18) | 1.62 | 0.77 | 0.56 | 0.00 | 1.13 | **4.30** | 0.07 | 3.7 | 4.0 | 2.7 MB |
| Mask | 19k | 33k (17) | 0.62 | 0.00 | 0.00 | 0.00 | 2.18 | **2.98** | 0.08 | 0.6 | 0.6 | 1.3 MB |
| Paint | 19k | 33k (17) | 0.60 | 0.00 | 0.00 | 0.00 | 2.53 | **3.31** | 0.07 | 0.9 | 0.9 | 2.7 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 4.0M, after edits) | 458 |
| Higher Level (back up) | 5 |
| Lower Level again (no edits) | 18 |
| Higher Level again (no edits) | 5 |
| Motion proxy build (first move after an upload) | 84 |
| Orbit: frame time, 4x antialiasing | 0.9 (1129 fps) |
| Orbit: frame time, no antialiasing | 0.4 (2636 fps) |
| Orbit: triangles drawn | 600k (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.4 (2633 fps) |
| Orbit: triangles drawn (motion proxy) | 600k (4%) |
| GPU memory with the motion proxy | 304 MB = 19.1 B/triangle |

### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1579 |
| BVH build | 2291 |
| First GPU upload | 3458 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 1217 MB = 37 B/triangle |
| BVH | 567 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 980 MB = 29 B/triangle |
| Process memory (RSS) | 3653 MB (+2129 MB = 64 B/triangle) |
| Peak while dividing (RSS) | 2526 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 150k (39) | 1.84 | 2.01 | 0.93 | 0.00 | 3.19 | **8.33** | 0.14 | 16.8 | 14.5 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (92) | 9.92 | 1.00 | 5.70 | 0.01 | 19.70 | **37.46** | 34.56 | 52.3 | 53.0 | 38.7 MB |
| Clay | 58k | 90k (28) | 1.42 | 1.30 | 0.74 | 0.00 | 2.26 | **6.14** | 0.08 | 9.1 | 9.3 | 7.2 MB |
| Clay Block | 66k | 102k (30) | 1.71 | 1.34 | 0.77 | 0.01 | 9.54 | **13.74** | 0.09 | 10.3 | 10.6 | 8.0 MB |
| Smooth | 39k | 65k (25) | 2.29 | 1.21 | 0.55 | 0.00 | 1.76 | **6.20** | 0.08 | 8.2 | 8.2 | 5.7 MB |
| Smooth (large, r 0.4) | 468k | 563k (65) | 12.67 | 6.26 | 2.90 | 0.01 | 9.67 | **32.19** | 0.21 | 26.4 | 27.5 | 20.5 MB |
| Inflate | 38k | 63k (24) | 1.13 | 1.25 | 0.62 | 0.00 | 1.95 | **5.36** | 0.08 | 8.1 | 7.2 | 5.6 MB |
| Sharp Crease | 37k | 62k (24) | 1.13 | 1.20 | 0.55 | 0.00 | 1.83 | **5.09** | 0.09 | 7.5 | 7.7 | 5.4 MB |
| Layer | 37k | 63k (24) | 1.70 | 1.19 | 0.51 | 0.00 | 1.68 | **5.45** | 0.09 | 7.5 | 7.7 | 5.6 MB |
| Move | 33k | 55k (23) | 0.22 | 0.90 | 0.38 | 0.00 | 3.50 | **5.38** | 0.03 | 1.8 | 1.8 | 0.5 MB |
| Move (large, r 0.4) | 351k | 440k (49) | 0.65 | 3.82 | 2.08 | 0.01 | 8.43 | **15.69** | 0.07 | 7.8 | 7.1 | 5.8 MB |
| Recipe (pattern) | 38k | 63k (24) | 2.27 | 1.14 | 0.58 | 0.00 | 1.84 | **6.21** | 0.08 | 7.1 | 7.6 | 5.6 MB |
| Mask | 38k | 60k (24) | 0.98 | 0.00 | 0.00 | 0.00 | 4.72 | **6.05** | 0.09 | 1.4 | 1.3 | 2.8 MB |
| Paint | 38k | 60k (24) | 1.18 | 0.00 | 0.00 | 0.00 | 5.68 | **7.22** | 0.09 | 1.9 | 1.8 | 5.7 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 8.3M, after edits) | 1046 |
| Higher Level (back up) | 11 |
| Lower Level again (no edits) | 48 |
| Higher Level again (no edits) | 13 |
| Motion proxy build (first move after an upload) | 167 |
| Orbit: frame time, 4x antialiasing | 1.4 (725 fps) |
| Orbit: frame time, no antialiasing | 0.8 (1327 fps) |
| Orbit: triangles drawn | 1.5M (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.4 (2371 fps) |
| Orbit: triangles drawn (motion proxy) | 396k (1%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |

