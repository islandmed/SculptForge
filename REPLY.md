# Draft replies for 0.10.9 (post at release, after reading)

## #33 Make Sure performances are up to industry standard (bobbe3636)
Another step for dense meshes in 0.10.9: a big brush (240 px) on a 33 million triangle model no longer freezes the view while each dab is worked out. In our standard benchmark scene (first measurements) the slowest frames went from about 300 ms to about 35 ms, and lifting the pen no longer waits. Move keeps up on the same mesh with a fast pen, and the first orbit after a long stroke no longer hitches. Help > Benchmark now also checks free memory first and skips a scene your computer can't hold rather than running it out of memory. If you try it on your heaviest scenes, the benchmark's numbers (Help > Benchmark) would help us a lot.
-- the SculptForge developer
