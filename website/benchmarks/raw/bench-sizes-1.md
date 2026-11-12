## With the GPU (sculptforge --bench)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 183 |
| BVH build | 275 |
| First GPU upload | 340 |
| GPU memory (buffers) | 59 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 146 MB = 37 B/triangle |
| BVH | 68 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 117 MB = 29 B/triangle |
| Process memory (RSS) | 491 MB (+407 MB = 102 B/triangle) |
| Peak while dividing (RSS) | 407 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 26k (15) | 0.68 | 0.79 | 0.53 | 0.00 | 0.87 | **3.01** | 0.07 | 2.2 | 2.5 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 159k (33) | 1.70 | 1.61 | 0.97 | 0.00 | 2.55 | **7.11** | 0.06 | 5.6 | 5.5 | 4.6 MB |
| Clay | 7k | 17k (10) | 0.47 | 0.70 | 0.30 | 0.00 | 0.67 | **2.23** | 0.03 | 1.6 | 1.6 | 0.9 MB |
| Clay Block | 8k | 20k (11) | 0.57 | 0.65 | 0.36 | 0.00 | 1.42 | **3.09** | 0.02 | 1.5 | 1.6 | 0.8 MB |
| Smooth | 5k | 14k (9) | 0.60 | 0.44 | 0.20 | 0.00 | 0.53 | **1.83** | 0.10 | 1.5 | 1.4 | 0.6 MB |
| Smooth (large, r 0.4) | 58k | 85k (23) | 2.30 | 1.04 | 0.65 | 0.00 | 1.69 | **5.99** | 0.07 | 3.4 | 3.4 | 2.5 MB |
| Inflate | 5k | 13k (9) | 0.28 | 0.43 | 0.20 | 0.00 | 0.55 | **1.52** | 0.03 | 1.4 | 1.6 | 0.6 MB |
| Sharp Crease | 4k | 13k (9) | 0.27 | 0.43 | 0.19 | 0.00 | 0.54 | **1.49** | 0.03 | 1.5 | 0.9 | 0.5 MB |
| Layer | 5k | 13k (9) | 0.40 | 0.45 | 0.20 | 0.00 | 0.55 | **1.65** | 0.03 | 1.6 | 1.5 | 0.6 MB |
| Move | 4k | 10k (8) | 0.04 | 0.25 | 0.16 | 0.00 | 0.70 | **1.19** | 0.01 | 0.5 | 0.4 | 0.1 MB |
| Move (large, r 0.4) | 42k | 63k (20) | 0.21 | 0.91 | 0.41 | 0.00 | 1.40 | **3.23** | 0.04 | 1.9 | 1.2 | 0.7 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.56 | 0.47 | 0.20 | 0.00 | 0.57 | **1.85** | 0.03 | 1.9 | 1.5 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.23 | 0.00 | 0.00 | 0.00 | 0.72 | **1.00** | 0.03 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.24 | 0.00 | 0.00 | 0.00 | 0.87 | **1.16** | 0.03 | 0.2 | 0.2 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 995k, after edits) | 109 |
| Higher Level (back up) | 1 |
| Lower Level again (no edits) | 5 |
| Higher Level again (no edits) | 1 |
| Motion proxy build (first move after an upload) | 17 |
| Orbit: frame time, 4x antialiasing | 0.5 (1980 fps) |
| Orbit: frame time, no antialiasing | 0.2 (4778 fps) |
| Orbit: triangles drawn | 164k (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.2 (5584 fps) |
| Orbit: triangles drawn (motion proxy) | 164k (4%) |
| GPU memory with the motion proxy | 76 MB = 19.1 B/triangle |

### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 700 |
| BVH build | 993 |
| First GPU upload | 1394 |
| GPU memory (buffers) | 235 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 583 MB = 37 B/triangle |
| BVH | 272 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 470 MB = 29 B/triangle |
| Process memory (RSS) | 1753 MB (+1178 MB = 74 B/triangle) |
| Peak while dividing (RSS) | 1282 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 80k (27) | 1.23 | 1.14 | 0.64 | 0.00 | 1.70 | **5.02** | 0.08 | 6.6 | 6.2 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 561k (62) | 4.91 | 0.83 | 2.71 | 0.00 | 9.21 | **18.23** | 14.02 | 21.0 | 21.6 | 18.5 MB |
| Clay | 28k | 49k (19) | 0.97 | 0.73 | 0.56 | 0.00 | 1.23 | **3.75** | 0.08 | 4.4 | 4.4 | 3.5 MB |
| Clay Block | 33k | 56k (22) | 1.12 | 0.93 | 0.59 | 0.00 | 4.52 | **7.42** | 0.08 | 5.3 | 5.3 | 3.8 MB |
| Smooth | 19k | 37k (17) | 1.96 | 0.74 | 0.65 | 0.00 | 1.08 | **4.65** | 0.07 | 3.7 | 3.7 | 2.7 MB |
| Smooth (large, r 0.4) | 233k | 293k (46) | 5.68 | 2.95 | 1.53 | 0.01 | 4.66 | **15.19** | 0.12 | 12.9 | 12.8 | 11.0 MB |
| Inflate | 18k | 36k (17) | 0.79 | 0.68 | 0.66 | 0.00 | 1.00 | **3.33** | 0.07 | 3.5 | 3.6 | 2.6 MB |
| Sharp Crease | 18k | 35k (17) | 0.86 | 0.84 | 0.64 | 0.00 | 1.23 | **3.78** | 0.07 | 3.3 | 4.1 | 2.5 MB |
| Layer | 19k | 36k (18) | 1.20 | 0.76 | 0.64 | 0.00 | 1.01 | **3.82** | 0.09 | 3.7 | 5.0 | 2.6 MB |
| Move | 16k | 32k (13) | 0.13 | 0.56 | 0.21 | 0.00 | 1.71 | **2.78** | 0.02 | 0.8 | 1.4 | 0.3 MB |
| Move (large, r 0.4) | 168k | 217k (37) | 0.34 | 1.87 | 0.97 | 0.01 | 3.74 | **7.27** | 0.05 | 3.7 | 3.4 | 2.7 MB |
| Recipe (pattern) | 19k | 36k (18) | 1.57 | 0.77 | 0.56 | 0.00 | 1.07 | **4.19** | 0.07 | 4.1 | 3.8 | 2.7 MB |
| Mask | 19k | 33k (17) | 0.68 | 0.00 | 0.00 | 0.00 | 2.17 | **3.04** | 0.10 | 0.8 | 0.6 | 1.3 MB |
| Paint | 19k | 33k (17) | 0.64 | 0.00 | 0.00 | 0.00 | 2.49 | **3.31** | 0.07 | 0.9 | 0.9 | 2.7 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 4.0M, after edits) | 445 |
| Higher Level (back up) | 5 |
| Lower Level again (no edits) | 15 |
| Higher Level again (no edits) | 5 |
| Motion proxy build (first move after an upload) | 73 |
| Orbit: frame time, 4x antialiasing | 0.9 (1137 fps) |
| Orbit: frame time, no antialiasing | 0.4 (2555 fps) |
| Orbit: triangles drawn | 600k (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.4 (2553 fps) |
| Orbit: triangles drawn (motion proxy) | 600k (4%) |
| GPU memory with the motion proxy | 304 MB = 19.1 B/triangle |

### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1651 |
| BVH build | 2179 |
| First GPU upload | 3312 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 1217 MB = 37 B/triangle |
| BVH | 567 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 980 MB = 29 B/triangle |
| Process memory (RSS) | 3626 MB (+2116 MB = 64 B/triangle) |
| Peak while dividing (RSS) | 2539 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 150k (39) | 1.75 | 1.77 | 0.93 | 0.01 | 2.75 | **7.50** | 0.11 | 13.3 | 14.7 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (92) | 8.83 | 1.04 | 5.04 | 0.01 | 17.64 | **33.63** | 31.22 | 43.9 | 45.3 | 38.7 MB |
| Clay | 58k | 90k (28) | 1.29 | 1.09 | 0.67 | 0.00 | 2.04 | **5.48** | 0.09 | 8.7 | 8.7 | 7.2 MB |
| Clay Block | 66k | 102k (30) | 1.53 | 1.27 | 0.76 | 0.01 | 8.69 | **12.63** | 0.09 | 10.4 | 9.4 | 8.0 MB |
| Smooth | 39k | 65k (25) | 2.12 | 1.01 | 0.55 | 0.00 | 1.62 | **5.65** | 0.08 | 7.0 | 7.3 | 5.7 MB |
| Smooth (large, r 0.4) | 468k | 563k (65) | 11.04 | 5.59 | 2.61 | 0.01 | 8.62 | **28.53** | 0.16 | 27.0 | 24.7 | 20.5 MB |
| Inflate | 38k | 63k (24) | 1.08 | 1.02 | 0.61 | 0.00 | 1.56 | **4.62** | 0.08 | 6.8 | 6.4 | 5.6 MB |
| Sharp Crease | 37k | 62k (24) | 0.94 | 1.04 | 0.50 | 0.00 | 1.58 | **4.42** | 0.08 | 6.4 | 6.6 | 5.4 MB |
| Layer | 37k | 63k (24) | 1.77 | 1.00 | 0.57 | 0.00 | 1.52 | **5.21** | 0.08 | 6.5 | 6.7 | 5.6 MB |
| Move | 33k | 55k (23) | 0.16 | 0.87 | 0.40 | 0.00 | 3.42 | **5.22** | 0.03 | 1.2 | 1.6 | 0.5 MB |
| Move (large, r 0.4) | 351k | 440k (49) | 0.58 | 3.45 | 1.89 | 0.01 | 7.54 | **14.12** | 0.07 | 6.3 | 6.4 | 5.8 MB |
| Recipe (pattern) | 38k | 63k (24) | 1.89 | 1.08 | 0.51 | 0.00 | 1.68 | **5.53** | 0.08 | 6.6 | 6.7 | 5.6 MB |
| Mask | 38k | 60k (24) | 1.14 | 0.00 | 0.00 | 0.01 | 4.57 | **6.06** | 0.09 | 2.9 | 1.3 | 2.8 MB |
| Paint | 38k | 60k (24) | 1.04 | 0.00 | 0.00 | 0.00 | 5.37 | **6.75** | 0.09 | 1.9 | 1.8 | 5.7 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 8.3M, after edits) | 1015 |
| Higher Level (back up) | 25 |
| Lower Level again (no edits) | 73 |
| Higher Level again (no edits) | 35 |
| Motion proxy build (first move after an upload) | 149 |
| Orbit: frame time, 4x antialiasing | 1.4 (722 fps) |
| Orbit: frame time, no antialiasing | 0.8 (1325 fps) |
| Orbit: triangles drawn | 1.5M (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.4 (2238 fps) |
| Orbit: triangles drawn (motion proxy) | 396k (1%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |

