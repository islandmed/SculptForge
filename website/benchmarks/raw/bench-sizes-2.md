## With the GPU (sculptforge --bench)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 164 |
| BVH build | 279 |
| First GPU upload | 370 |
| GPU memory (buffers) | 59 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 146 MB = 37 B/triangle |
| BVH | 68 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 117 MB = 29 B/triangle |
| Process memory (RSS) | 491 MB (+406 MB = 102 B/triangle) |
| Peak while dividing (RSS) | 406 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 26k (15) | 0.58 | 0.75 | 0.51 | 0.00 | 0.96 | **2.92** | 0.03 | 2.5 | 2.3 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 159k (33) | 1.74 | 1.89 | 1.05 | 0.00 | 3.00 | **7.98** | 0.09 | 6.0 | 6.6 | 4.6 MB |
| Clay | 7k | 17k (10) | 0.39 | 0.56 | 0.29 | 0.00 | 0.72 | **2.03** | 0.03 | 2.0 | 2.4 | 0.9 MB |
| Clay Block | 8k | 20k (11) | 0.56 | 0.58 | 0.36 | 0.00 | 1.65 | **3.23** | 0.03 | 1.4 | 1.8 | 0.8 MB |
| Smooth | 5k | 14k (9) | 0.65 | 0.43 | 0.20 | 0.00 | 0.64 | **1.98** | 0.03 | 1.7 | 1.7 | 0.6 MB |
| Smooth (large, r 0.4) | 58k | 85k (23) | 3.12 | 1.26 | 0.81 | 0.00 | 2.01 | **7.55** | 0.07 | 4.0 | 4.1 | 2.5 MB |
| Inflate | 5k | 13k (9) | 0.27 | 0.43 | 0.20 | 0.00 | 0.64 | **1.58** | 0.03 | 1.5 | 1.5 | 0.6 MB |
| Sharp Crease | 4k | 13k (9) | 0.28 | 0.42 | 0.19 | 0.00 | 0.64 | **1.59** | 0.03 | 1.7 | 2.7 | 0.5 MB |
| Layer | 5k | 13k (9) | 0.43 | 0.52 | 0.20 | 0.00 | 0.75 | **1.97** | 0.03 | 1.7 | 1.5 | 0.6 MB |
| Move | 4k | 10k (8) | 0.03 | 0.23 | 0.14 | 0.00 | 0.77 | **1.21** | 0.01 | 0.5 | 0.5 | 0.1 MB |
| Move (large, r 0.4) | 42k | 63k (20) | 0.23 | 0.83 | 0.37 | 0.00 | 1.60 | **3.31** | 0.04 | 1.5 | 1.7 | 0.7 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.52 | 0.42 | 0.19 | 0.00 | 0.61 | **1.80** | 0.03 | 1.6 | 1.9 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.22 | 0.00 | 0.00 | 0.00 | 0.80 | **1.06** | 0.03 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.25 | 0.00 | 0.00 | 0.00 | 1.00 | **1.30** | 0.03 | 0.2 | 0.2 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 995k, after edits) | 124 |
| Higher Level (back up) | 5 |
| Lower Level again (no edits) | 5 |
| Higher Level again (no edits) | 3 |
| Motion proxy build (first move after an upload) | 21 |
| Orbit: frame time, 4x antialiasing | 0.5 (1887 fps) |
| Orbit: frame time, no antialiasing | 0.2 (4787 fps) |
| Orbit: triangles drawn | 164k (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.2 (5554 fps) |
| Orbit: triangles drawn (motion proxy) | 164k (4%) |
| GPU memory with the motion proxy | 76 MB = 19.1 B/triangle |

### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 710 |
| BVH build | 1178 |
| First GPU upload | 1551 |
| GPU memory (buffers) | 235 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 583 MB = 37 B/triangle |
| BVH | 272 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 470 MB = 29 B/triangle |
| Process memory (RSS) | 1779 MB (+1209 MB = 76 B/triangle) |
| Peak while dividing (RSS) | 1285 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 80k (27) | 1.21 | 1.13 | 0.65 | 0.00 | 1.87 | **5.16** | 0.09 | 7.1 | 7.0 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 561k (62) | 5.23 | 0.80 | 2.93 | 0.00 | 10.01 | **19.59** | 15.48 | 23.4 | 24.8 | 18.5 MB |
| Clay | 28k | 49k (19) | 1.37 | 0.90 | 0.63 | 0.00 | 1.47 | **4.65** | 0.09 | 5.1 | 7.1 | 3.5 MB |
| Clay Block | 33k | 56k (22) | 1.22 | 1.00 | 0.61 | 0.00 | 4.80 | **7.89** | 0.07 | 6.8 | 5.3 | 3.8 MB |
| Smooth | 19k | 37k (17) | 1.95 | 0.71 | 0.65 | 0.00 | 1.15 | **4.66** | 0.08 | 4.2 | 3.9 | 2.7 MB |
| Smooth (large, r 0.4) | 233k | 293k (46) | 10.31 | 4.42 | 2.32 | 0.01 | 6.58 | **24.09** | 0.16 | 19.3 | 17.8 | 11.0 MB |
| Inflate | 18k | 36k (17) | 0.75 | 0.86 | 0.66 | 0.00 | 1.29 | **3.75** | 0.08 | 4.9 | 4.7 | 2.6 MB |
| Sharp Crease | 18k | 35k (17) | 0.77 | 0.92 | 0.64 | 0.00 | 1.42 | **3.94** | 0.29 | 5.0 | 5.5 | 2.5 MB |
| Layer | 19k | 36k (18) | 1.17 | 0.97 | 0.64 | 0.00 | 1.37 | **4.35** | 0.07 | 4.6 | 4.3 | 2.6 MB |
| Move | 16k | 32k (13) | 0.12 | 0.67 | 0.28 | 0.00 | 2.07 | **3.29** | 0.02 | 1.3 | 1.3 | 0.3 MB |
| Move (large, r 0.4) | 168k | 217k (37) | 0.48 | 2.18 | 1.24 | 0.01 | 4.60 | **8.84** | 0.06 | 4.0 | 4.9 | 2.7 MB |
| Recipe (pattern) | 19k | 36k (18) | 1.68 | 0.82 | 0.58 | 0.00 | 1.25 | **4.52** | 0.08 | 5.1 | 5.1 | 2.7 MB |
| Mask | 19k | 33k (17) | 0.64 | 0.00 | 0.00 | 0.00 | 2.34 | **3.17** | 0.07 | 0.7 | 0.6 | 1.3 MB |
| Paint | 19k | 33k (17) | 0.62 | 0.00 | 0.00 | 0.00 | 2.86 | **3.65** | 0.08 | 0.9 | 0.9 | 2.7 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 4.0M, after edits) | 471 |
| Higher Level (back up) | 6 |
| Lower Level again (no edits) | 30 |
| Higher Level again (no edits) | 5 |
| Motion proxy build (first move after an upload) | 93 |
| Orbit: frame time, 4x antialiasing | 0.9 (1143 fps) |
| Orbit: frame time, no antialiasing | 0.4 (2666 fps) |
| Orbit: triangles drawn | 600k (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.4 (2691 fps) |
| Orbit: triangles drawn (motion proxy) | 600k (4%) |
| GPU memory with the motion proxy | 304 MB = 19.1 B/triangle |

### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1647 |
| BVH build | 2290 |
| First GPU upload | 3746 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 1217 MB = 37 B/triangle |
| BVH | 567 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 980 MB = 29 B/triangle |
| Process memory (RSS) | 3555 MB (+2202 MB = 66 B/triangle) |
| Peak while dividing (RSS) | 2597 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 150k (39) | 2.54 | 3.12 | 1.56 | 0.01 | 5.41 | **13.14** | 0.21 | 24.6 | 22.6 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (92) | 10.70 | 0.99 | 6.28 | 0.01 | 22.37 | **41.51** | 33.55 | 50.2 | 48.5 | 38.7 MB |
| Clay | 58k | 90k (28) | 1.45 | 1.22 | 0.74 | 0.00 | 2.30 | **6.12** | 0.09 | 9.1 | 9.6 | 7.2 MB |
| Clay Block | 66k | 102k (30) | 1.62 | 1.38 | 0.76 | 0.01 | 10.00 | **14.17** | 0.08 | 10.8 | 9.9 | 8.0 MB |
| Smooth | 39k | 65k (25) | 2.26 | 1.10 | 0.54 | 0.00 | 1.72 | **5.99** | 0.09 | 7.5 | 7.3 | 5.7 MB |
| Smooth (large, r 0.4) | 468k | 563k (65) | 12.57 | 6.34 | 2.86 | 0.01 | 9.81 | **32.28** | 0.17 | 27.2 | 26.2 | 20.5 MB |
| Inflate | 38k | 63k (24) | 1.00 | 1.12 | 0.59 | 0.00 | 1.76 | **4.82** | 0.26 | 7.4 | 7.8 | 5.6 MB |
| Sharp Crease | 37k | 62k (24) | 1.07 | 1.17 | 0.59 | 0.00 | 1.82 | **5.02** | 0.08 | 7.7 | 7.6 | 5.4 MB |
| Layer | 37k | 63k (24) | 1.67 | 1.16 | 0.53 | 0.00 | 1.69 | **5.39** | 0.08 | 8.0 | 8.3 | 5.6 MB |
| Move | 33k | 55k (23) | 0.19 | 0.95 | 0.39 | 0.01 | 3.72 | **5.60** | 0.03 | 1.5 | 1.4 | 0.5 MB |
| Move (large, r 0.4) | 351k | 440k (49) | 0.87 | 3.70 | 2.00 | 0.01 | 8.13 | **15.32** | 0.06 | 7.1 | 7.1 | 5.8 MB |
| Recipe (pattern) | 38k | 63k (24) | 2.48 | 1.20 | 0.57 | 0.00 | 1.84 | **6.51** | 0.09 | 7.2 | 7.6 | 5.6 MB |
| Mask | 38k | 60k (24) | 1.00 | 0.00 | 0.00 | 0.00 | 4.77 | **6.12** | 0.09 | 1.4 | 1.3 | 2.8 MB |
| Paint | 38k | 60k (24) | 1.10 | 0.00 | 0.00 | 0.00 | 5.75 | **7.20** | 0.09 | 2.0 | 2.1 | 5.7 MB |

| Operation | ms |
|---|---:|
| Lower Level (to 8.3M, after edits) | 1099 |
| Higher Level (back up) | 25 |
| Lower Level again (no edits) | 48 |
| Higher Level again (no edits) | 13 |
| Motion proxy build (first move after an upload) | 175 |
| Orbit: frame time, 4x antialiasing | 1.4 (724 fps) |
| Orbit: frame time, no antialiasing | 0.7 (1342 fps) |
| Orbit: triangles drawn | 1.5M (4%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 0.4 (2345 fps) |
| Orbit: triangles drawn (motion proxy) | 396k (1%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |

