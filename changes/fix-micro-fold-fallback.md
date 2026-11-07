- Big brushes on micro levels are fast again on modest hardware: a 240 px Clay stroke on a 200M-triangle micro model
  ran at about 24 fps on the base tier in 0.11.10-0.11.12 (186 fps before), because a stroke near any rough spot
  sculpted the whole brush through the fine grid. Now the base takes as much of each dab as keeps its triangles from
  folding over, so stroke ends on rough surfaces stay free of shards without the slowdown.
