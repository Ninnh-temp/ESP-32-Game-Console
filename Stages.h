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
    { true, -20, -5, 10, 10 } // Small obstruction block
  },
  // Stage 3
  {
    {
      // Wave 0
      { { {ENEMY_BAT, 4} }, 1 },
      // Wave 1
      { { {ENEMY_SHOOTER, 3}, {ENEMY_BRUTE, 1} }, 2 },
      // Wave 2
      { { {ENEMY_BAT, 2}, {ENEMY_SHOOTER, 2}, {ENEMY_BRUTE, 1} }, 3 }
    },
    3, 
    { true, 0, -20, 60, 10 } // Wide horizontal platform
  },
  // Stage 4
  {
    {
      // Wave 0
      { { {ENEMY_BAT, 4}, {ENEMY_BRUTE, 4} }, 2 }
    },
    1, // 1 wave
    { true, 20, -25, 20, 50 } // High column on the right
  },
  // Stage 5
  {
    {
      // Wave 0
      { { {ENEMY_SHOOTER, 4}, {ENEMY_BRUTE, 2} }, 2 },
      // Wave 1
      { { {ENEMY_BAT, 6}, {ENEMY_SHOOTER, 3}, {ENEMY_BRUTE, 4} }, 3 }
    },
    2, // 2 waves
    { true, -15, -15, 25, 25 } // Medium square block on the left
  }
};

const int MAX_STAGES = 6;

#endif
