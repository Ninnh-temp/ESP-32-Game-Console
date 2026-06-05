#ifndef STAGES_H
#define STAGES_H

enum EnemyType { ENEMY_BAT, ENEMY_SHOOTER, ENEMY_BRUTE };

struct SpawnInfo {
  EnemyType type;
  int count;
};

const int MAX_SPAWNS_PER_WAVE = 3;
struct Wave {
  SpawnInfo spawns[MAX_SPAWNS_PER_WAVE];
  int numSpawns;
};

const int MAX_WAVES_PER_STAGE = 3;
struct TerrainInfo {
  bool hasWall;
  int xCenterOffset;
  int yCenterOffset;
  int w, h;
};

struct Stage {
  Wave waves[MAX_WAVES_PER_STAGE];
  int numWaves;
  TerrainInfo terrain;
};

const Stage stages[] = {
  // Stage 0 (Blank room to prepare)
  {
    {
      { { {ENEMY_BAT, 0} }, 0 } // No enemies
    },
    0, // 0 waves (will immediately clear and spawn gates)
    { false, 0, 0, 0, 0 }
  },
  // Stage 1
  {
    {
      // Wave 0
      { { {ENEMY_BAT, 2} }, 1 }, // 2 bats
      // Wave 1
      { { {ENEMY_BAT, 2}, {ENEMY_SHOOTER, 1}, {ENEMY_BRUTE, 1} }, 3 } 
    },
    2, // 2 waves
    { true, 10, -15, 5, 30 } // xCenterOffset=10, yCenterOffset=-15 (which centers a 30px high wall)
  },
  // Stage 2
  {
    {
      // Wave 0
      { { {ENEMY_BAT, 3}, {ENEMY_SHOOTER, 2} }, 2 }
    },
    1, // 1 wave
    { false, 0, 0, 0, 0 } // No wall
  }
};

const int MAX_STAGES = 3;

#endif
