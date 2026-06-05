#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h> 
#include "Stages.h"

#include <driver/i2s.h>

// --- I2S AUDIO SYNTHESIZER ---
#define I2S_DOUT      22
#define I2S_BCLK      26
#define I2S_LRC       25  

enum SoundEffect { SFX_NONE, SFX_SHOOT, SFX_HIT, SFX_DASH, SFX_HEAL };
volatile SoundEffect currentSFX = SFX_NONE;
volatile int sfxFrame = 0; // Tracks how long the sound has been playing

// The background audio task that runs on Core 0
void audioTask(void *pvParameters) {
  int16_t sampleBuffer[256];
  size_t bytesWritten;
  
  while (true) {
    if (currentSFX == SFX_NONE) {
      // If no sound, output absolute silence to prevent speaker static
      memset(sampleBuffer, 0, sizeof(sampleBuffer));
      i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
      vTaskDelay(10 / portTICK_PERIOD_MS); 
      continue;
    }

    // Synthesize the sound wave block by block
    for (int i = 0; i < 256; i++) {
      int16_t sample = 0;
      sfxFrame++;

      switch (currentSFX) {
        case SFX_SHOOT:
          // Fast descending pitch (Square wave)
          if (sfxFrame > 4000) currentSFX = SFX_NONE;
          else sample = ((sfxFrame % (20 + (sfxFrame/100))) < 10) ? 8000 : -8000;
          break;

        case SFX_HIT:
          // White noise burst (Explosion/Hit)
          if (sfxFrame > 3000) currentSFX = SFX_NONE;
          else sample = random(-10000, 10000) * (1.0 - (sfxFrame / 3000.0)); // Fade out
          break;

        case SFX_DASH:
          // Fast ascending sweep
          if (sfxFrame > 2000) currentSFX = SFX_NONE;
          else sample = ((sfxFrame % (60 - (sfxFrame/50))) < 30) ? 6000 : -6000;
          break;

        case SFX_HEAL:
          // Happy high-pitched chime
          if (sfxFrame > 5000) currentSFX = SFX_NONE;
          else sample = ((sfxFrame % 15) < 7) ? 4000 : -4000;
          break;
          
        default:
          currentSFX = SFX_NONE;
          break;
      }
      sampleBuffer[i] = sample;
    }
    // Push the 256 generated audio samples to the MAX98357A amplifier
    i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
  }
}

// Helper function to trigger sounds easily from the game loop
void playSound(SoundEffect sfx) {
  currentSFX = sfx;
  sfxFrame = 0; // Reset the sound timeline
}

// Create the screen object
TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite canvas = TFT_eSprite(&tft);

// --- CONTROLLER PINS ---
const int btnUp = 13; const int btnDown = 14;
const int btnLeft = 32; const int btnRight = 33;  
const int btnA = 16; const int btnB = 17;

unsigned long lastFrameTime = 0;
const int frameDelay = 33; // 30 FPS

//==== MENU VARIABLES ====
enum GameState { STATE_TITLE, STATE_TUTORIAL, STATE_ARENA, STATE_GAME_OVER_TRANSITION, STATE_GAME_OVER };
GameState gameState = STATE_TITLE;
int menuSelection = 0;
int gameOverSelection = 0;
int transitionY = 0;

bool prevMenuUp = true;
bool prevMenuDown = true;
bool prevMenuA = true;
bool prevMenuB = true;

void resetMenuInput() {
  prevMenuUp = true;
  prevMenuDown = true;
  prevMenuA = true;
  prevMenuB = true;
}

// --- STRUCTURES & ENTITIES ---
struct Player {
  int x, y;
  int prevX, prevY;
  int speed;
  int facingX, facingY;
  int health;
  bool isDashing;
  unsigned long dashTimer;
  int dashDirX, dashDirY;
  bool isInvulnerable;
  unsigned long invulnerableTimer;
};

  enum EnemyState { STATE_IDLE, STATE_PATROL, STATE_AGGRO, STATE_SPAWNING, STATE_PREP_SHOOT, STATE_FROZEN_SHOOT, STATE_PREP_DASH, STATE_DASHING, STATE_PREP_SLAM, STATE_SLAMMING };

struct Enemy {
  bool active;
  EnemyType type;
  EnemyState state;
  float x, y;
  float prevX, prevY;
  float speed;
  float dirX, dirY;
  float lockDirX, lockDirY;
  float targetX, targetY; // For slam indicator
  unsigned long timer;
  int damage;
  int health;
  int w, h;
};

struct Bullet {
  bool active;
  float x, y, prevX, prevY;
  float dirX, dirY;
  float speed;
};

const int MAX_BULLETS = 3;
Bullet bullets[MAX_BULLETS];

struct EnemyBullet {
  bool active;
  float x, y;
  float dirX, dirY;
  float speed;
  int damage;
};

const int MAX_ENEMY_BULLETS = 5;
EnemyBullet enemyBullets[MAX_ENEMY_BULLETS];

Player player;
const int MAX_ENEMIES = 10;
Enemy enemies[MAX_ENEMIES];

int currentStage = 0;
int currentWave = 0;
bool gatesActive = false;
bool checkTerrainCollision(int x, int y, int w, int h); // forward declare

// NEW EFFECTS GLOBALS
int hitFlashTimer = 0;
int playerHistoryX[5];
int playerHistoryY[5];

struct Orb {
  bool active;
  int x, y;
  int healAmount;
  int radius;
};
Orb healingOrb = {false, 0, 0, 30, 4};


// 8x8 Blue Knight 
const uint16_t heroSprite[64] PROGMEM = {
  0x0000, 0x0000, 0x03FF, 0x03FF, 0x0000, 0x0000, 0x0000, 0x0000, // Plume Top (Bright Blue)
  0x0000, 0x0116, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // Plume Base (Dark Blue)
  0x0000, 0x0000, 0xC618, 0xC618, 0xC618, 0xC618, 0x0000, 0x0000, // Helmet Top (Silver)
  0x0000, 0x0000, 0xC618, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // Visor Slit
  0x0000, 0x0000, 0x6B6D, 0xC618, 0x0000, 0xC618, 0x0000, 0x0000, // Visor Bottom (Purple-Grey shade)
  0x0000, 0x0116, 0x0000, 0x0000, 0x0000, 0x0000, 0x0116, 0x0000, // Hands (Dark Blue) & Belt
  0x0000, 0x0000, 0xC618, 0xC618, 0xC618, 0xC618, 0x0000, 0x0000, // Lower Armor (Silver)
  0x0000, 0x0000, 0x6B6D, 0x0000, 0x0000, 0x6B6D, 0x0000, 0x0000  // Legs (Purple-Grey)
};

// 8x8 Seaborn Tentacle Enemy
const uint16_t batSprite[64] PROGMEM = {
0x0000, 0x0332, 0x0000, 0x0000, 0x0000, 0x0000, 0x0332, 0x0000,
0x0000, 0x0332, 0x018C, 0x018C, 0x018C, 0x018C, 0x0332, 0x0000,
0x0332, 0x0332, 0x018C, 0x018C, 0x018C, 0x018C, 0x0332, 0x0332,
0x0332, 0x0000, 0x018C, 0xF800, 0xF800, 0x018C, 0x0000, 0x0332,
0x0332, 0x0000, 0x018C, 0x018C, 0x018C, 0x018C, 0x0000, 0x0332,
0x018C, 0x0000, 0x018C, 0x0000, 0x0000, 0x018C, 0x0000, 0x018C,
0x0000, 0x0332, 0x018C, 0x0000, 0x0000, 0x018C, 0x0332, 0x0000,
0x0000, 0x0000, 0x0332, 0x0000, 0x0000, 0x0332, 0x0000, 0x0000
};

// 8x8 Seaborn Eye Shooter
const uint16_t shooterSprite[64] PROGMEM = {
  0x0000, 0x0000, 0x1C6A, 0x0000, 0x0000, 0x1C6A, 0x0000, 0x0000, 
  0x0000, 0x1C6A, 0x1C6A, 0x0000, 0x0000, 0x1C6A, 0x1C6A, 0x0000, 
  0x0000, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x0000, 
  0x0000, 0x1C6A, 0x0000, 0x1C6A, 0x1C6A, 0x0000, 0x1C6A, 0x0000, 
  0x0000, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x0000, 
  0x1AD8, 0x1AD8, 0xA288, 0xA288, 0xA288, 0xA288, 0x1AD8, 0x1AD8, 
  0x1AD8, 0xA288, 0xA288, 0x0000, 0x0000, 0xA288, 0xA288, 0x1AD8, 
  0x0000, 0xA288, 0xA288, 0x0000, 0x0000, 0xA288, 0xA288, 0x0000  
};

// 12x12 Brute Enemy
const uint16_t bruteSprite[144] PROGMEM = {
  0x0000, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0x0000, // Horns top
  0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000, // Horns curve
  0x0000, 0x0000, 0x0000, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0x0000, 0x0000, 0x0000, // Head
  0x0000, 0x0000, 0x0000, 0xF800, 0xFFE0, 0xF800, 0xF800, 0xFFE0, 0xF800, 0x0000, 0x0000, 0x0000, // Gold Eyes
  0x0000, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0x0000, // Massive shoulders
  0xF800, 0xF800, 0x8010, 0x8010, 0xF800, 0xF800, 0xF800, 0xF800, 0x8010, 0x8010, 0xF800, 0xF800, // Straps / Pauldrons
  0xF800, 0xF800, 0x0000, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x0000, 0xF800, 0xF800, // Purple armor & arm gaps
  0x0000, 0xF800, 0xF800, 0x0000, 0x8010, 0x8010, 0x8010, 0x8010, 0x0000, 0xF800, 0xF800, 0x0000, // Arms hanging down
  0x0000, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0x0000, // Huge Gold Belt
  0x0000, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x0000, // Legs
  0x0000, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x0000, // Legs
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000  // Empty bottom buffer
};

// ==== COMBAT VARIABLES ====
int ammo = 6;              // Max ammo to start with 6!
const int MAX_AMMO = 6;

int comboStep = 0;         // 0 = Idle, 1 = Thrust, 2 = Slash
unsigned long comboTimer = 0;
const int comboWindow = 800; // You have 800ms to press A again for the 2nd hit

unsigned long meleeCooldownTimer = 0;
const int meleeCooldown = 175; 

unsigned long gunCooldownTimer = 0;
const int gunCooldown = 300; 

bool prevBtnA = true;
bool prevBtnB = true;

// Melee rendering helper
bool drawMelee = false;
int meleeHitOffsetX, meleeHitOffsetY, meleeHitW, meleeHitH;
unsigned long meleeDrawTimer = 0;

// Dash variables
unsigned long dashDuration = 350; // Dash lasts 350ms
int dashSpeed = 4;                // Faster speed during dash
unsigned long lastTapTime[4] = {0, 0, 0, 0}; // Up, Down, Left, Right
const unsigned long iframeDuration = 700; // ms for i-frames
bool prevDpad[4] = {true, true, true, true};
const int doubleTapWindow = 250; // ms for double tap

// Box Collision Helper
bool checkCollision(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
  return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

// Terrain Collision Helper
bool checkTerrainCollision(int x, int y, int w, int h) {
  if (currentStage >= MAX_STAGES) return false;
  if (!stages[currentStage].terrain.hasWall) return false;
  int screenWidth = tft.width();
  int screenHeight = tft.height();
  int wallX = screenWidth / 2 + stages[currentStage].terrain.xCenterOffset;
  int wallY = screenHeight / 2 + stages[currentStage].terrain.yCenterOffset;
  return checkCollision(x, y, w, h, wallX, wallY, stages[currentStage].terrain.w, stages[currentStage].terrain.h);
}

// Stage Manager logic
void spawnWave() {
  for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
  
  if (currentStage >= MAX_STAGES) return;
  const Wave& wave = stages[currentStage].waves[currentWave];
  
  int enemyIndex = 0;
  for (int s = 0; s < wave.numSpawns; s++) {
    for (int c = 0; c < wave.spawns[s].count; c++) {
      if (enemyIndex >= MAX_ENEMIES) break;
      enemies[enemyIndex].active = true;
      enemies[enemyIndex].type = wave.spawns[s].type;
      enemies[enemyIndex].state = STATE_SPAWNING;
      
      enemies[enemyIndex].w = 8;
      enemies[enemyIndex].h = 8;

      bool validSpawn = false;
      int attempts = 0;
      while (!validSpawn && attempts < 20) {
        enemies[enemyIndex].x = random(10, tft.width() - 18);
        enemies[enemyIndex].y = random(10, tft.height() - 18);
        int dx = enemies[enemyIndex].x - player.x;
        int dy = enemies[enemyIndex].y - player.y;
        if ((dx*dx + dy*dy) > 2500) { // Keep away from player
          if (!checkTerrainCollision(enemies[enemyIndex].x, enemies[enemyIndex].y, 12, 12)) {
            validSpawn = true;
          }
        }
        attempts++;
      }
      
      enemies[enemyIndex].dirX = 0;
      enemies[enemyIndex].dirY = 0;
      enemies[enemyIndex].timer = millis();
      enemies[enemyIndex].prevX = enemies[enemyIndex].x;
      enemies[enemyIndex].prevY = enemies[enemyIndex].y;
      
      switch(enemies[enemyIndex].type) {
        case ENEMY_SHOOTER:
          enemies[enemyIndex].speed = 1;
          enemies[enemyIndex].damage = 10;
          enemies[enemyIndex].health = 15;
          break;
        case ENEMY_BRUTE:
          enemies[enemyIndex].speed = 0.5;
          enemies[enemyIndex].damage = 15;
          enemies[enemyIndex].health = 60;
          enemies[enemyIndex].w = 12;
          enemies[enemyIndex].h = 12;
          break;
        case ENEMY_BAT:
        default:
          enemies[enemyIndex].speed = 1;
          enemies[enemyIndex].damage = 10;
          enemies[enemyIndex].health = 10;
          break;
      }
      enemyIndex++;
    }
  }
}

void initEntities() {
  player.x = 76; player.y = 60; player.speed = 2;
  player.prevX = player.x; player.prevY = player.y;
  player.facingX = 1; player.facingY = 0;
  player.health = 100;
  player.isDashing = false;
  player.dashTimer = 0;
  player.dashDirX = 0;
  player.dashDirY = 0;
  player.isInvulnerable = false;
  player.invulnerableTimer = 0;

  ammo = MAX_AMMO;
  comboStep = 0;
  comboTimer = 0;
  meleeCooldownTimer = 0;
  gunCooldownTimer = 0;
  drawMelee = false;
  meleeDrawTimer = 0;
  prevBtnA = true;
  prevBtnB = true;

  for (int i = 0; i < 4; i++) {
    lastTapTime[i] = 0;
    prevDpad[i] = true;
  }

  for(int i=0; i<MAX_BULLETS; i++) {
    bullets[i].active = false;
  }

  for(int i=0; i<MAX_ENEMY_BULLETS; i++) {
    enemyBullets[i].active = false;
  }

  currentStage = 0;
  currentWave = 0;
  gatesActive = false;
  healingOrb.active = false;
  hitFlashTimer = 0;
  for(int i=0; i<5; i++) { playerHistoryX[i] = player.x; playerHistoryY[i] = player.y; }
  spawnWave();
}

void updateEnemies() {
  bool anyActive = false;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    anyActive = true;
    
    Enemy& e = enemies[i];
    e.prevX = e.x;
    e.prevY = e.y;

    int distToPlayerX = player.x - e.x;
    int distToPlayerY = player.y - e.y;
    int distSq = (distToPlayerX * distToPlayerX) + (distToPlayerY * distToPlayerY);

    switch(e.type) {
      case ENEMY_BAT: {
        if (e.state == STATE_SPAWNING) {
          if (millis() - e.timer > 1000) {
            e.state = STATE_IDLE;
            e.timer = millis();
          }
        } 
        else if (e.state == STATE_IDLE) {
          if (distSq < 4000) e.state = STATE_AGGRO; 
          else if (millis() - e.timer > 2000) { 
            e.state = STATE_PATROL; 
            e.timer = millis(); 
            e.dirX = random(-1, 2); 
            e.dirY = random(-1, 2); 
          }
        } 
        else if (e.state == STATE_PATROL) {
          if (distSq < 2500) e.state = STATE_AGGRO;
          e.x += e.dirX * e.speed;
          e.y += e.dirY * e.speed;
          if (millis() - e.timer > 500) { 
            e.state = STATE_IDLE; 
            e.timer = millis(); 
          }
        } 
        else if (e.state == STATE_AGGRO) {
          if (distSq > 5000) e.state = STATE_IDLE; 
          
          if (!player.isDashing) {
            if (e.x < player.x) e.x += e.speed;
            else if (e.x > player.x) e.x -= e.speed;
            
            if (e.y < player.y) e.y += e.speed;
            else if (e.y > player.y) e.y -= e.speed;
          }
        }
        break;
      }
      case ENEMY_SHOOTER: {
        if (e.state == STATE_SPAWNING) {
          if (millis() - e.timer > 1000) {
            e.state = STATE_IDLE;
            e.timer = millis();
          }
        }
        else if (e.state == STATE_PREP_SHOOT) {
          // Tracking player for 1 sec  
          if (millis() - e.timer < 1000) {
            float dx = (player.x + 4) - (e.x + 4);
            float dy = (player.y + 4) - (e.y + 4);
            float mag = sqrt(dx*dx + dy*dy);
            if (mag > 0) { e.lockDirX = dx/mag; e.lockDirY = dy/mag; }
          } else {
            e.state = STATE_FROZEN_SHOOT;
            e.timer = millis();
          }
        }
        else if (e.state == STATE_FROZEN_SHOOT) {
          // Freeze for 0.5s then fire
          if (millis() - e.timer > 500) {
            for (int b = 0; b < MAX_ENEMY_BULLETS; b++) {
              if (!enemyBullets[b].active) {
                enemyBullets[b].active = true;
                enemyBullets[b].x = e.x + 4;
                enemyBullets[b].y = e.y + 4;
                enemyBullets[b].dirX = e.lockDirX;
                enemyBullets[b].dirY = e.lockDirY;
                enemyBullets[b].speed = 2.5; // moderate
                enemyBullets[b].damage = 20;
                break;
              }
            }
            e.state = STATE_IDLE;
            e.timer = millis();
          }
        }
        else {
          // Movement phase
          if (millis() - e.timer > 3000) { // Shoot every 3 seconds roughly
            e.state = STATE_PREP_SHOOT;
            e.timer = millis();
          } else {
            float dx = player.x - e.x;
            float dy = player.y - e.y;
            float mag = sqrt(dx*dx + dy*dy);
            if (mag > 0) {
              if (mag < 45) { // Too close, back away
                e.x -= (dx/mag) * e.speed;
                e.y -= (dy/mag) * e.speed;
              } else if (mag > 85) { // Too far, close in
                e.x += (dx/mag) * e.speed;
                e.y += (dy/mag) * e.speed;
              } else {
                // Strafe (circle around player)
                // Tangent vector is (-dy, dx). We alternate direction every 2 seconds
                if ((millis() / 2000) % 2 == 0) {
                  e.x += (-dy/mag) * e.speed;
                  e.y += (dx/mag) * e.speed;
                } else {
                  e.x -= (-dy/mag) * e.speed;
                  e.y -= (dx/mag) * e.speed;
                }
              }
            }
          }
        }
        break;
      }
      case ENEMY_BRUTE: {
        if (e.state == STATE_SPAWNING) {
          if (millis() - e.timer > 1000) { e.state = STATE_AGGRO; e.timer = millis(); }
        }
        else if (e.state == STATE_AGGRO) {
          if (millis() - e.timer > 3000 && distSq < 10000) { // 3s cooldown and close enough
            e.state = STATE_PREP_DASH;
            e.timer = millis();
            float dx = player.x - e.x; float dy = player.y - e.y;
            float mag = sqrt(dx*dx + dy*dy);
            if(mag>0) { e.lockDirX = dx/mag; e.lockDirY = dy/mag; }
          } else {
            if (!player.isDashing) {
                e.x += (e.x < player.x ? e.speed : -e.speed);
                e.y += (e.y < player.y ? e.speed : -e.speed);
            }
          }
        }
        else if (e.state == STATE_PREP_DASH) {
           if (millis() - e.timer > 500) {
             e.state = STATE_DASHING;
             e.timer = millis();
             e.damage = 30; // heavy dash damage
           }
        }
        else if (e.state == STATE_DASHING) {
           e.x += e.lockDirX * 4; 
           e.y += e.lockDirY * 4;
           if (millis() - e.timer > 200) { // 0.2s quick short dash
             e.state = STATE_PREP_SLAM;
             e.timer = millis();
             e.damage = 15; // revert normal collision damage
             e.targetX = e.x + e.w/2 + e.lockDirX * 16 - 12;
             e.targetY = e.y + e.h/2 + e.lockDirY * 16 - 12;
           }
        }
        else if (e.state == STATE_PREP_SLAM) {
           if (millis() - e.timer > 500) {
             e.state = STATE_SLAMMING;
             e.timer = millis();
             // immediate slam damage check
             if (!player.isInvulnerable && !player.isDashing) {
                if (checkCollision(player.x, player.y, 8, 8, e.targetX, e.targetY, 24, 24)) {
                   player.health -= 35; // a lot of damage
                   player.isInvulnerable = true;
                   player.invulnerableTimer = millis();
                   hitFlashTimer = 5;
                }
             }
           }
        }
        else if (e.state == STATE_SLAMMING) {
           if (millis() - e.timer > 200) {
             e.state = STATE_AGGRO;
             e.timer = millis();
           }
        }
        break;
      }
      default:
        // Handle other enemy types if needed in the future
        break;
    }

    if (checkTerrainCollision(e.x, e.y, e.w, e.h)) {
      e.x = e.prevX;
      e.y = e.prevY;
    }

    int screenWidth = tft.width();
    int screenHeight = tft.height();
    if (e.x < 0) e.x = 0;
    if (e.y < 0) e.y = 0;
    if (e.x > screenWidth - e.w) e.x = screenWidth - e.w;
    if (e.y > screenHeight - e.h) e.y = screenHeight - e.h;
  }
  
  if (!anyActive && !gatesActive) {
    if (currentStage < MAX_STAGES) {
      if (currentWave + 1 >= stages[currentStage].numWaves && stages[currentStage].numWaves > 0) {
        // Only trigger gates if we've completed all waves
        // However, if numWaves == 0, we can just trigger gates immediately.
        gatesActive = true;
        healingOrb.active = true; healingOrb.x = tft.width() / 2 - 10; healingOrb.y = tft.height() / 2;
      } else if (stages[currentStage].numWaves == 0) {
        gatesActive = true;
        healingOrb.active = true; healingOrb.x = tft.width() / 2 - 10; healingOrb.y = tft.height() / 2;
      } else {
        currentWave++;
        spawnWave();
      }
    } else {
      // You won! Maybe handle game win state?
    }
  }
}

void updatePlayer() {
  if (hitFlashTimer > 0) hitFlashTimer--;

  // Update dash trail history
  for(int i=4; i>0; i--) {
    playerHistoryX[i] = playerHistoryX[i-1];
    playerHistoryY[i] = playerHistoryY[i-1];
  }
  playerHistoryX[0] = player.x;
  playerHistoryY[0] = player.y;

  player.prevX = player.x;
  player.prevY = player.y;

  bool currentUp = digitalRead(btnUp) == LOW;
  bool currentDown = digitalRead(btnDown) == LOW;
  bool currentLeft = digitalRead(btnLeft) == LOW;
  bool currentRight = digitalRead(btnRight) == LOW;

  if (!player.isDashing) {
    if (currentUp && !prevDpad[0]) {
      if (millis() - lastTapTime[0] < doubleTapWindow) { player.isDashing = true; player.dashDirX = 0; player.dashDirY = -1; player.dashTimer = millis(); }
      lastTapTime[0] = millis();
    }
    if (currentDown && !prevDpad[1]) {
      if (millis() - lastTapTime[1] < doubleTapWindow) { player.isDashing = true; player.dashDirX = 0; player.dashDirY = 1; player.dashTimer = millis(); }
      lastTapTime[1] = millis();
    }
    if (currentLeft && !prevDpad[2]) {
      if (millis() - lastTapTime[2] < doubleTapWindow) { player.isDashing = true; player.dashDirX = -1; player.dashDirY = 0; player.dashTimer = millis(); }
      lastTapTime[2] = millis();
    }
    if (currentRight && !prevDpad[3]) {
      if (millis() - lastTapTime[3] < doubleTapWindow) { player.isDashing = true; player.dashDirX = 1; player.dashDirY = 0; player.dashTimer = millis(); }
      lastTapTime[3] = millis();
    }
  }

  prevDpad[0] = currentUp;
  prevDpad[1] = currentDown;
  prevDpad[2] = currentLeft;
  prevDpad[3] = currentRight;

  if (player.isDashing) {
    for (int s = 0; s < dashSpeed; s++) {
      player.x += player.dashDirX;
      if (checkTerrainCollision(player.x, player.prevY, 8, 8)) { player.x -= player.dashDirX; break; }
    }
    for (int s = 0; s < dashSpeed; s++) {
      player.y += player.dashDirY;
      if (checkTerrainCollision(player.x, player.y, 8, 8)) { player.y -= player.dashDirY; break; }
    }

    if (millis() - player.dashTimer > dashDuration) {
      player.isDashing = false;
    }
  } else {
    bool moved = false;
    int moveX = 0, moveY = 0;
    
    if (currentLeft)  { moveX = -1; player.facingX = -1; player.facingY = 0; moved = true; }
    else if (currentRight) { moveX = 1; player.facingX = 1; player.facingY = 0; moved = true; }

    if (currentUp)    { moveY = -1; if (!moved) { player.facingX = 0; player.facingY = -1; } }
    else if (currentDown)  { moveY = 1; if (!moved) { player.facingX = 0; player.facingY = 1; } }

    for (int s = 0; s < player.speed; s++) {
      player.x += moveX;
      if (checkTerrainCollision(player.x, player.prevY, 8, 8)) { player.x -= moveX; break; }
    }
    for (int s = 0; s < player.speed; s++) {
      player.y += moveY;
      if (checkTerrainCollision(player.x, player.y, 8, 8)) { player.y -= moveY; break; }
    }
  }

  // Handle Melee (btnA)
  bool currentBtnA = digitalRead(btnA) == LOW;
  if (currentBtnA && !prevBtnA) {
    if (millis() - meleeCooldownTimer > meleeCooldown) { 
      meleeCooldownTimer = millis();
      
      if (comboStep == 0 || millis() - comboTimer > comboWindow) {
        comboStep = 1; // thrust
        comboTimer = millis();
      } else {
        comboStep = 2; // slash
        comboTimer = millis(); // reset combo timer
      }
      
      if (player.facingX != 0) {  // Horizontal attack
        if (comboStep == 1) {     // Thrust has a longer reach but smaller width
          meleeHitW = 16; meleeHitH = 4; meleeHitOffsetX = (player.facingX == 1 ? 8 : -16); meleeHitOffsetY = 2; 
        } else {                  // Slash is wider but shorter reach
          meleeHitW = 8; meleeHitH = 16; meleeHitOffsetX = (player.facingX == 1 ? 8 : -8); meleeHitOffsetY = -4; 
        }
      } else {                    // Vertical attack
        if (comboStep == 1) { 
          meleeHitW = 4; meleeHitH = 16; meleeHitOffsetX = 2; meleeHitOffsetY = (player.facingY == 1 ? 8 : -16); 
        } else { 
          meleeHitW = 16; meleeHitH = 8; meleeHitOffsetX = -4; meleeHitOffsetY = (player.facingY == 1 ? 8 : -8); 
        }
      }

      drawMelee = true;   
      meleeDrawTimer = millis(); // Start timer to erase melee hitbox

      // Check hit on enemies
      for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active || enemies[i].state == STATE_SPAWNING) continue;
        if (checkCollision(player.x + meleeHitOffsetX, player.y + meleeHitOffsetY, meleeHitW, meleeHitH, enemies[i].x, enemies[i].y, enemies[i].w, enemies[i].h)) {
          enemies[i].health -= 5;
          int kb = (comboStep == 1) ? 2 : 12;
          enemies[i].x += player.facingX * kb;
          enemies[i].y += player.facingY * kb;
          if (comboStep == 2 && ammo < MAX_AMMO) ammo++; //2nd hit in combo gives you ammo back!
          if (enemies[i].health <= 0) enemies[i].active = false;
        }
      }
      
      if (comboStep == 2) comboStep = 0; // End of sequence
    }
  }
  prevBtnA = currentBtnA;

  // Handle Gun (btnB)
  bool currentBtnB = digitalRead(btnB) == LOW;
  if (currentBtnB && !prevBtnB) { 
    if (millis() - gunCooldownTimer > gunCooldown && ammo > 0) {
      gunCooldownTimer = millis(); // reset gun cooldown
      ammo--;

      for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
          bullets[i].active = true;
          bullets[i].x = player.x + 4;
          bullets[i].y = player.y + 4;
          bullets[i].prevX = bullets[i].x;
          bullets[i].prevY = bullets[i].y;
          bullets[i].dirX = player.facingX;
          bullets[i].dirY = player.facingY;
          if (bullets[i].dirX == 0 && bullets[i].dirY == 0) bullets[i].dirX = 1;
          bullets[i].speed = 5;
          break;
        }
      }
    }
  }
  prevBtnB = currentBtnB;

  // Update Bullets
  for(int i=0; i<MAX_BULLETS; i++) {
    if(bullets[i].active) {
      bullets[i].prevX = bullets[i].x;
      bullets[i].prevY = bullets[i].y;
      bullets[i].x += bullets[i].dirX * bullets[i].speed;
      bullets[i].y += bullets[i].dirY * bullets[i].speed;
      
      bool hitEnemy = false;
      for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!enemies[e].active || enemies[e].state == STATE_SPAWNING) continue;
        if (checkCollision(bullets[i].x, bullets[i].y, 2, 2, enemies[e].x, enemies[e].y, enemies[e].w, enemies[e].h)) {
          bullets[i].active = false;
          enemies[e].health -= 5;
          enemies[e].x += bullets[i].dirX * 10;
          enemies[e].y += bullets[i].dirY * 10;
          hitEnemy = true;
          if (enemies[e].health <= 0) enemies[e].active = false;
          break;
        }
      }
      if (!hitEnemy && (bullets[i].x < 0 || bullets[i].x > tft.width() || bullets[i].y < 0 || bullets[i].y > tft.height() || checkTerrainCollision(bullets[i].x, bullets[i].y, 2, 2))) {
        bullets[i].active = false;
      }
    }
  }

  // Update Enemy Bullets
  for(int i=0; i<MAX_ENEMY_BULLETS; i++) {
    if(enemyBullets[i].active) {
      enemyBullets[i].x += enemyBullets[i].dirX * enemyBullets[i].speed;
      enemyBullets[i].y += enemyBullets[i].dirY * enemyBullets[i].speed;
      
      if (!player.isDashing && !player.isInvulnerable) {
        if (checkCollision(enemyBullets[i].x, enemyBullets[i].y, 3, 3, player.x, player.y, 8, 8)) {
          enemyBullets[i].active = false;
          player.health -= enemyBullets[i].damage;
          player.isInvulnerable = true;
          player.invulnerableTimer = millis();
          hitFlashTimer = 5;
        }
      }
      
      if (enemyBullets[i].x < 0 || enemyBullets[i].x > tft.width() || enemyBullets[i].y < 0 || enemyBullets[i].y > tft.height() || checkTerrainCollision(enemyBullets[i].x, enemyBullets[i].y, 3, 3)) {
        enemyBullets[i].active = false;
      }
    }
  }

  // Handle Invulnerability and Damage Check
  if (player.isInvulnerable && millis() - player.invulnerableTimer > iframeDuration) {
    player.isInvulnerable = false;
  }

  if (!player.isDashing && !player.isInvulnerable) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (!enemies[i].active || enemies[i].state == STATE_SPAWNING) continue;
      if (checkCollision(player.x, player.y, 8, 8, enemies[i].x, enemies[i].y, enemies[i].w, enemies[i].h)) {
        player.health -= enemies[i].damage;
        player.isInvulnerable = true;
        player.invulnerableTimer = millis();
        hitFlashTimer = 5;
        break; // Only take damage from one enemy per frame
      }
    }
  }

  int screenWidth = tft.width();
  int screenHeight = tft.height();
  if (player.x < 0) player.x = 0;
  if (player.y < 0) player.y = 0;
  if (player.x > screenWidth - 8) player.x = screenWidth - 8;
  if (player.y > screenHeight - 8) player.y = screenHeight - 8;

  if (healingOrb.active) {
    // Check collision considering its center and radius
    if (checkCollision(player.x, player.y, 8, 8, healingOrb.x - healingOrb.radius, healingOrb.y - healingOrb.radius, healingOrb.radius*2, healingOrb.radius*2)) {
      healingOrb.active = false;
      player.health += healingOrb.healAmount;
      if (player.health > 100) player.health = 100;
    }
  }

  // Gate transition logic
  if (gatesActive) {
    if (checkCollision(player.x, player.y, 8, 8, screenWidth / 2 - 8, 0, 16, 8) || 
        checkCollision(player.x, player.y, 8, 8, screenWidth / 2 - 8, screenHeight - 8, 16, 8) || 
        checkCollision(player.x, player.y, 8, 8, 0, screenHeight / 2 - 8, 8, 16) || 
        checkCollision(player.x, player.y, 8, 8, screenWidth - 8, screenHeight / 2 - 8, 8, 16)) {
      
      currentStage++;
      currentWave = 0;
      gatesActive = false;
      
      // Spawn player towards start or center
      player.x = 76;
      player.y = 60;
      
      if (currentStage < MAX_STAGES) {
        spawnWave();
      }
    }
  }
}


void renderArena(bool pushToScreen = true) {
  canvas.fillSprite(TFT_BLACK);
  
  // Draw Wall
  if (currentStage < MAX_STAGES && stages[currentStage].terrain.hasWall) {
    int screenWidth = tft.width();
    int screenHeight = tft.height();
    int wallX = screenWidth / 2 + stages[currentStage].terrain.xCenterOffset;
    int wallY = screenHeight / 2 + stages[currentStage].terrain.yCenterOffset;
    canvas.fillRect(wallX, wallY, stages[currentStage].terrain.w, stages[currentStage].terrain.h, TFT_RED);
  }

  // Draw Gates (Stage Transitions)
  if (gatesActive) {
    int screenWidth = tft.width();
    int screenHeight = tft.height();
    // Top
    canvas.fillRect(screenWidth / 2 - 8, 0, 16, 8, TFT_WHITE);
    // Bottom
    canvas.fillRect(screenWidth / 2 - 8, screenHeight - 8, 16, 8, TFT_WHITE);
    // Left
    canvas.fillRect(0, screenHeight / 2 - 8, 8, 16, TFT_WHITE);
    // Right
    canvas.fillRect(screenWidth - 8, screenHeight / 2 - 8, 8, 16, TFT_WHITE);
  }
  
  // Draw Bullets
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) {
      canvas.fillRect(bullets[i].x, bullets[i].y, 2, 2, TFT_YELLOW);
    }
  }
  
  // Draw Enemy Bullets
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (enemyBullets[i].active) {
      canvas.fillRect(enemyBullets[i].x, enemyBullets[i].y, 3, 3, TFT_ORANGE);
    }
  }

  // Draw Melee Attack
  if (drawMelee) {
    canvas.fillRect(player.x + meleeHitOffsetX, player.y + meleeHitOffsetY, meleeHitW, meleeHitH, TFT_WHITE);
    if (millis() - meleeDrawTimer > 50) {
      drawMelee = false;
    }
  }
  
  // Draw Entities
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      if (enemies[i].state == STATE_SPAWNING) {
        // Draw spawning animation: expanding/blinking rect
        long elapsed = millis() - enemies[i].timer;
        if ((elapsed / 100) % 2 == 0) {
          int size = (elapsed * 8) / 1000;
          if (size > 8) size = 8;
          canvas.drawRect(enemies[i].x + 4 - size/2, enemies[i].y + 4 - size/2, size, size, TFT_RED);
        }
      } else {
        if (enemies[i].type == ENEMY_BAT) {
          canvas.pushImage(enemies[i].x, enemies[i].y, 8, 8, batSprite);
        } else if (enemies[i].type == ENEMY_SHOOTER) {
          canvas.pushImage(enemies[i].x, enemies[i].y, 8, 8, shooterSprite);
          if (enemies[i].state == STATE_PREP_SHOOT) {
            canvas.drawLine(enemies[i].x + 4, enemies[i].y + 4, enemies[i].x + 4 + enemies[i].lockDirX * 50, enemies[i].y + 4 + enemies[i].lockDirY * 50, TFT_RED);
          } else if (enemies[i].state == STATE_FROZEN_SHOOT) {
            canvas.drawLine(enemies[i].x + 4, enemies[i].y + 4, enemies[i].x + 4 + enemies[i].lockDirX * 50, enemies[i].y + 4 + enemies[i].lockDirY * 50, TFT_RED);
          }
        } else if (enemies[i].type == ENEMY_BRUTE) {
          if (enemies[i].state == STATE_PREP_DASH && (millis() / 50) % 2 == 0) {
            canvas.fillRect(enemies[i].x, enemies[i].y, enemies[i].w, enemies[i].h, TFT_RED);
          } else {
            canvas.pushImage(enemies[i].x, enemies[i].y, 12, 12, bruteSprite);
          }
          if (enemies[i].state == STATE_PREP_SLAM) {
            canvas.drawRect(enemies[i].targetX, enemies[i].targetY, 24, 24, TFT_ORANGE);
          } else if (enemies[i].state == STATE_SLAMMING) {
            canvas.fillRect(enemies[i].targetX, enemies[i].targetY, 24, 24, TFT_RED);
          }
        }
      }
    }
  }
    
  // Draw Player with blink effect when invulnerable
  if (!player.isInvulnerable || (millis() / 100) % 2 == 0) {
    canvas.pushImage(player.x, player.y, 8, 8, heroSprite);
  }

  // Draw Dash Trail
  if (player.isDashing) {
    for (int i = 0; i < 5; i += 2) {
      canvas.drawRect(playerHistoryX[i], playerHistoryY[i], 8, 8, TFT_CYAN);
    }
  }

  // Draw Healing Orb
  if (healingOrb.active) {
    int orbPulse = (millis() / 150) % 3;
    canvas.fillCircle(healingOrb.x, healingOrb.y, healingOrb.radius + orbPulse, TFT_GREEN);
    canvas.fillCircle(healingOrb.x, healingOrb.y, healingOrb.radius, TFT_WHITE);
  }

  // Hit Receive Effect (Flash screen border)
  if (hitFlashTimer > 0) {
    canvas.drawRect(0, 0, tft.width(), tft.height(), TFT_RED);
    canvas.drawRect(1, 1, tft.width()-2, tft.height()-2, TFT_RED);
    canvas.drawRect(2, 2, tft.width()-4, tft.height()-4, TFT_RED);
  }

  // Draw HUD
  // Health bar
  canvas.fillRect(2, 2, 50, 6, TFT_BLUE); // Background of health bar
  int healthW = (player.health * 50) / 100;
  if (healthW < 0) healthW = 0;
  canvas.fillRect(2, 2, healthW, 6, TFT_GREEN);
  canvas.drawRect(2, 2, 50, 6, TFT_WHITE);
  
  // Ammo counter
  for (int i = 0; i < MAX_AMMO; i++) {
    if (i < ammo) {
      canvas.fillRect(tft.width() - 8 - (i * 6), 2, 4, 6, TFT_YELLOW);
    } else {
      canvas.drawRect(tft.width() - 8 - (i * 6), 2, 4, 6, tft.color565(80, 80, 80));
    }
  }
    
  // Push buffer to screen
  if (pushToScreen) {
    canvas.pushSprite(0, 0);
  }
}

// GUI Drawing helper
void drawMenuButton(int cx, int cy, int w, int h, const char* text, bool isSelected) {
  int x = cx - w/2;
  int y = cy - h/2;
  int offset = isSelected ? 2 : 4; 
  canvas.fillRect(x + offset, y + offset, w, h, tft.color565(80, 80, 80)); 
  canvas.fillRect(x, y, w, h, TFT_BLACK);
  uint16_t outColor = isSelected ? TFT_YELLOW : TFT_WHITE;
  
  for (int i = 0; i < w; i++) {
    if (i % 6 < 3 || i % 6 == 4) { 
      canvas.drawPixel(x + i, y, outColor); 
      canvas.drawPixel(x + i, y + h, outColor); 
    } 
  }
  for (int i = 0; i < h; i++) {
    if (i % 6 < 3 || i % 6 == 4) { 
      canvas.drawPixel(x, y + i, outColor); 
      canvas.drawPixel(x + w, y + i, outColor); 
    } 
  }
  canvas.setTextColor(outColor);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString(text, cx, cy, 1);
}

void updateTitle() {
  bool currentUp = digitalRead(btnUp) == LOW;
  bool currentDown = digitalRead(btnDown) == LOW;
  bool currentA = digitalRead(btnA) == LOW;

  if (currentUp && !prevMenuUp) { menuSelection--; if (menuSelection < 0) menuSelection = 1; }
  if (currentDown && !prevMenuDown) { menuSelection++; if (menuSelection > 1) menuSelection = 0; }
  if (currentA && !prevMenuA) {
    if (menuSelection == 0) {
      initEntities();
      resetMenuInput();
      gameState = STATE_ARENA;
    } else {
      resetMenuInput();
      gameState = STATE_TUTORIAL;
    }
  }
  prevMenuUp = currentUp; prevMenuDown = currentDown; prevMenuA = currentA;
}

void renderTitle() {
  canvas.fillSprite(TFT_BLACK);
  int cx = tft.width() / 2;
  drawMenuButton(cx, tft.height() / 2 - 20, 80, 24, "START", menuSelection == 0);
  drawMenuButton(cx, tft.height() / 2 + 20, 80, 24, "TUTORIAL", menuSelection == 1);
  canvas.pushSprite(0, 0);
}

void updateTutorial() {
  bool currentB = digitalRead(btnB) == LOW;
  if (currentB && !prevMenuB) {
    resetMenuInput();
    gameState = STATE_TITLE;
  }
  prevMenuB = currentB;
}

void renderTutorial() {
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(TL_DATUM);
  canvas.drawString("TUTORIAL", 10, 10, 1);
  canvas.drawString("- D-Pad: Move / Dash", 10, 30, 1);
  canvas.drawString("- A: Melee Combo", 10, 50, 1);
  canvas.drawString("- B: Shoot Gun", 10, 70, 1);
  
  canvas.setTextColor(TFT_YELLOW);
  canvas.drawString("Press B to return", 10, 100, 1);
  canvas.pushSprite(0, 0);
}

void updateGameOverTransition() {
  transitionY -= 10;
  if (transitionY < 0) {
    gameState = STATE_GAME_OVER;
    gameOverSelection = 0;
    resetMenuInput();
  }
}

void renderGameOverTransition() {
  renderArena(false);
  for (int y = max(0, transitionY); y < tft.height(); y += 4) {
    for (int x = 0; x < tft.width(); x += 4) {
      canvas.fillRect(x, y, 4, 4, random(2) ? TFT_WHITE : TFT_BLACK);
    }
  }
  canvas.pushSprite(0, 0);
}

void updateGameOver() {
  bool currentUp = digitalRead(btnUp) == LOW;
  bool currentDown = digitalRead(btnDown) == LOW;
  bool currentA = digitalRead(btnA) == LOW;

  if (currentUp && !prevMenuUp) { gameOverSelection--; if (gameOverSelection < 0) gameOverSelection = 1; }
  if (currentDown && !prevMenuDown) { gameOverSelection++; if (gameOverSelection > 1) gameOverSelection = 0; }
  if (currentA && !prevMenuA) {
    if (gameOverSelection == 0) {
      initEntities();
      resetMenuInput();
      gameState = STATE_ARENA;
    } else {
      resetMenuInput();
      gameState = STATE_TITLE;
      menuSelection = 0;
    }
  }
  prevMenuUp = currentUp; prevMenuDown = currentDown; prevMenuA = currentA;
}

void renderGameOver() {
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_RED);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString("GAME OVER", tft.width() / 2, tft.height() / 4, 2);

  int cx = tft.width() / 2;
  drawMenuButton(cx, tft.height() / 2 + 10, 90, 24, "START OVER", gameOverSelection == 0);
  drawMenuButton(cx, tft.height() / 2 + 40, 90, 24, "TITLE SCREEN", gameOverSelection == 1);
  canvas.pushSprite(0, 0);
}

void setup() {
  Serial.begin(115200);
  
  pinMode(btnUp, INPUT_PULLUP); pinMode(btnDown, INPUT_PULLUP);
  pinMode(btnLeft, INPUT_PULLUP); pinMode(btnRight, INPUT_PULLUP);
  pinMode(btnA, INPUT_PULLUP); pinMode(btnB, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  canvas.createSprite(tft.width(), tft.height());
  canvas.setSwapBytes(true);

  randomSeed(analogRead(34)); 
}

void loop() {
  if (millis() - lastFrameTime < frameDelay) return; 
  lastFrameTime = millis(); 

  switch (gameState) {
    case STATE_TITLE:
      updateTitle();
      renderTitle();
      break;
      
    case STATE_TUTORIAL:
      updateTutorial();
      renderTutorial();
      break;
      
    case STATE_ARENA:
      updateEnemies();
      updatePlayer();
      if (player.health <= 0) {
        gameState = STATE_GAME_OVER_TRANSITION;
        transitionY = tft.height();
      } else {
        renderArena(true);
      }
      break;
      
    case STATE_GAME_OVER_TRANSITION:
      updateGameOverTransition();
      renderGameOverTransition();
      break;
      
    case STATE_GAME_OVER:
      updateGameOver();
      renderGameOver();
      break;
  }
  
  // Initialize I2S Audio
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = 16000, // 16kHz is perfect for retro 8-bit audio
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false
  };
  
  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK,
      .ws_io_num = I2S_LRC,
      .data_out_num = I2S_DOUT,
      .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  // Pin the Audio Task to Core 0!
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 2048, NULL, 1, NULL, 0);
}
