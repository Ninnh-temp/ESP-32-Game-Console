#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h> 
#include "Stages.h"
#include "Audio.h"

// Create the screen object
TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite canvas = TFT_eSprite(&tft);

// --- CONTROLLER PINS ---
const int btnUp = 13; const int btnDown = 14;
const int btnLeft = 33; const int btnRight = 32;  
const int btnA = 16; const int btnB = 17;

unsigned long lastFrameTime = 0;
const int frameDelay = 33; // 30 FPS

// --- MASTER STATE MACHINE ---
enum GameState { 
  STATE_SYSTEM_MENU,
  // Action RPG States
  STATE_ACT_TITLE, STATE_ACT_TUTORIAL, STATE_ACT_ARENA, STATE_ACT_GAMEOVER_TRANS, STATE_ACT_GAMEOVER,
  // Dino Run States
  STATE_DINO_TITLE, STATE_DINO_PLAYING, STATE_DINO_GAMEOVER 
};
GameState gameState = STATE_SYSTEM_MENU;

// --- COMMON HELPERS ---
bool checkCollision(float x1, float y1, float w1, float h1, float x2, float y2, float w2, float h2) {
  return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

void drawMenuButton(int cx, int cy, int w, int h, const char* text, bool isSelected) {
  int x = cx - w/2;
  int y = cy - h/2;
  int offset = isSelected ? 2 : 4; 
  canvas.fillRect(x + offset, y + offset, w, h, tft.color565(80, 80, 80)); 
  canvas.fillRect(x, y, w, h, TFT_BLACK);
  uint16_t outColor = isSelected ? TFT_YELLOW : TFT_WHITE;
  
  for (int i = 0; i < w; i++) {
    if (i % 6 < 3 || i % 6 == 4) { canvas.drawPixel(x + i, y, outColor); canvas.drawPixel(x + i, y + h - 1, outColor); } 
  }
  for (int i = 0; i < h; i++) {
    if (i % 6 < 3 || i % 6 == 4) { canvas.drawPixel(x, y + i, outColor); canvas.drawPixel(x + w - 1, y + i, outColor); } 
  }
  canvas.setTextColor(outColor);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString(text, cx, cy, 1);
}

// ==========================================
//           SYSTEM BOOT MENU
// ==========================================
int systemMenuSelection = 0;
bool prevSysUp = true, prevSysDown = true, prevSysA = true;

void updateSystemMenu() {
  bool currentUp = digitalRead(btnUp) == LOW;
  bool currentDown = digitalRead(btnDown) == LOW;
  bool currentA = digitalRead(btnA) == LOW;

  if (currentUp && !prevSysUp) { 
    systemMenuSelection--; 
    if (systemMenuSelection < 0) systemMenuSelection = 1; 
    playSound(SFX_DASH); 
  }
  if (currentDown && !prevSysDown) { 
    systemMenuSelection++; 
    if (systemMenuSelection > 1) systemMenuSelection = 0; 
    playSound(SFX_DASH); 
  }
  if (currentA && !prevSysA) {
    playSound(SFX_SHOOT);
    if (systemMenuSelection == 0) {
      gameState = STATE_ACT_TITLE;
    } else {
      gameState = STATE_DINO_TITLE;
    }
  }
  prevSysUp = currentUp; prevSysDown = currentDown; prevSysA = currentA;
}

void renderSystemMenu() {
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString("ESP32 OS v1.0", tft.width() / 2, 20, 2);

  int cx = tft.width() / 2;
  drawMenuButton(cx, tft.height() / 2 - 10, 110, 24, "ACTION RPG", systemMenuSelection == 0);
  drawMenuButton(cx, tft.height() / 2 + 25, 110, 24, "DINO RUN", systemMenuSelection == 1);
  canvas.pushSprite(0, 0);
}

// ==========================================
//             DINO RUN CLONE
// ==========================================
struct Dino { float x, y; float yVelocity; int w, h; bool isJumping; };
Dino dinoPlayer;

struct Obstacle { float x, y; int w, h; bool active; };
const int MAX_OBSTACLES = 3;
Obstacle cacti[MAX_OBSTACLES];

const float GRAVITY = 1.2;
const float JUMP_FORCE = -10.0;
const int GROUND_Y = 100;
float dinoSpeed = 3.0;
int dinoScore = 0;
int dinoHighScore = 0;

bool dinoPrevBtnA = true, dinoPrevBtnB = true;

const uint16_t dinoSprite[144] PROGMEM = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x0000, 0x03E0, 0x03E0, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x0000,
  0x03E0, 0x0000, 0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x0000, 0x0000, 0x0000, 0x0000,
  0x03E0, 0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x0000, 0x0000, 0x0000, 0x0000,
  0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x03E0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x03E0, 0x03E0, 0x03E0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x03E0, 0x0000, 0x03E0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x03E0, 0x0000, 0x03E0, 0x03E0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

void dino_initGame() {
  dinoPlayer.w = 12; dinoPlayer.h = 12; dinoPlayer.x = 20;
  dinoPlayer.y = GROUND_Y - dinoPlayer.h;
  dinoPlayer.yVelocity = 0; dinoPlayer.isJumping = false;
  
  dinoSpeed = 3.0; 
  dinoScore = 0;
  
  cacti[0].active = true; cacti[0].w = 8; cacti[0].h = 16; 
  cacti[0].x = 160; cacti[0].y = GROUND_Y - cacti[0].h;
  cacti[1].active = false;
  cacti[2].active = false;
}

void dino_updateTitle() {
  bool currentA = digitalRead(btnA) == LOW;
  bool currentB = digitalRead(btnB) == LOW;
  
  if (currentA && !dinoPrevBtnA) { 
    playSound(SFX_HEAL); 
    dino_initGame(); 
    gameState = STATE_DINO_PLAYING; 
  }
  if (currentB && !dinoPrevBtnB) { 
    playSound(SFX_DASH); 
    gameState = STATE_SYSTEM_MENU; // EXIT TO MENU
  } 
  
  dinoPrevBtnA = currentA; dinoPrevBtnB = currentB;
  
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_GREEN); canvas.setTextDatum(MC_DATUM);
  canvas.drawString("T-REX RUNNER", tft.width()/2, 40, 2);
  
  if ((millis() / 500) % 2 == 0) {
    canvas.setTextColor(TFT_WHITE); canvas.drawString("Press A to Jump", tft.width()/2, 80, 1);
  }
  canvas.setTextColor(TFT_YELLOW); canvas.setTextDatum(BC_DATUM);
  canvas.drawString("Press B to Exit", tft.width()/2, tft.height() - 5, 1);
  
  canvas.pushImage(tft.width()/2 - 6, 100, 12, 12, dinoSprite);
  canvas.pushSprite(0, 0);
}

void dino_updatePlaying() {
  bool currentA = digitalRead(btnA) == LOW;
  
  if (currentA && !dinoPrevBtnA && !dinoPlayer.isJumping) {
    dinoPlayer.yVelocity = JUMP_FORCE; 
    dinoPlayer.isJumping = true; 
    playSound(SFX_DASH); 
  }
  dinoPrevBtnA = currentA;

  dinoPlayer.yVelocity += GRAVITY; 
  dinoPlayer.y += dinoPlayer.yVelocity;
  
  if (dinoPlayer.y >= GROUND_Y - dinoPlayer.h) {
    dinoPlayer.y = GROUND_Y - dinoPlayer.h; 
    dinoPlayer.yVelocity = 0; 
    dinoPlayer.isJumping = false;
  }

  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (cacti[i].active) {
      cacti[i].x -= dinoSpeed;
      if (cacti[i].x < -cacti[i].w) {
        dinoScore++;
        
        
      if (dinoScore > 0 && dinoScore % 10 == 0) { dinoSpeed += 0.4; playSound(SFX_HEAL); }
        
        float maxX = 160;
        for (int j = 0; j < MAX_OBSTACLES; j++) {
          if (j != i && cacti[j].active && cacti[j].x > maxX) {
            maxX = cacti[j].x;
          }
        }
        
        // 30% chance to spawn a cluster of cacti 
        // Otherwise, provide a safe landing gap that scales gently with speed.
        int gap;
        if (dinoScore >= 15 && random(0, 100) < 30) {
          gap = random(12, 24); // Tight cluster (12 to 24 pixels apart)
        } else {
          gap = 60 + (int)((dinoSpeed - 3.0) * 10) + random(0, 50); // Safe landing gap
        }

        cacti[i].x = maxX + gap;
        cacti[i].h = random(12, 22); cacti[i].y = GROUND_Y - cacti[i].h;

        // Unlock 2nd obstacle at score 20
        if (dinoScore >= 20 && !cacti[1].active) {
          int gap2 = (random(0, 100) < 30) ? random(12, 24) : (60 + (int)((dinoSpeed - 3.0) * 10) + random(0, 50));
          cacti[1].active = true; cacti[1].x = cacti[i].x + gap2;
          cacti[1].w = 8; cacti[1].h = random(10, 16); cacti[1].y = GROUND_Y - cacti[1].h;
        }
        // Unlock 3rd obstacle at score 50
        if (dinoScore >= 50 && !cacti[2].active) {
          int gap3 = (random(0, 100) < 30) ? random(12, 24) : (60 + (int)((dinoSpeed - 3.0) * 10) + random(0, 50));
          cacti[2].active = true; cacti[2].x = cacti[i].x + gap3;
          cacti[2].w = 8; cacti[2].h = random(10, 16); cacti[2].y = GROUND_Y - cacti[2].h;
        }
      }
      if (checkCollision(dinoPlayer.x+2, dinoPlayer.y+2, dinoPlayer.w-4, dinoPlayer.h-4, cacti[i].x+1, cacti[i].y+1, cacti[i].w-2, cacti[i].h-2)) {
        playSound(SFX_HIT); 
        if (dinoScore > dinoHighScore) dinoHighScore = dinoScore;
        gameState = STATE_DINO_GAMEOVER;
      }
    }
  }

  canvas.fillSprite(tft.color565(20, 20, 40)); 
  canvas.drawLine(0, GROUND_Y, 160, GROUND_Y, TFT_WHITE);
  
  for(int i = 0; i < 160; i+=20) {
    // FIXED: Multiply then divide to prevent Divide-by-Zero exception at high speeds
    int lineX = (i - (int)((millis() * (int)dinoSpeed / 10) % 20));
    if (lineX > 0) canvas.drawPixel(lineX, GROUND_Y + 2 + (i%3), TFT_LIGHTGREY);
  }
  
  canvas.pushImage((int)dinoPlayer.x, (int)dinoPlayer.y, 12, 12, dinoSprite);
  
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (cacti[i].active) {
      canvas.fillRect((int)cacti[i].x, (int)cacti[i].y, cacti[i].w, cacti[i].h, TFT_GREEN);
      canvas.drawRect((int)cacti[i].x, (int)cacti[i].y, cacti[i].w, cacti[i].h, TFT_DARKGREEN);
    }
  }
  
  canvas.setTextColor(TFT_WHITE); canvas.setTextDatum(TR_DATUM);
  char scoreText[16]; sprintf(scoreText, "HI:%03d  %03d", dinoHighScore, dinoScore);
  canvas.drawString(scoreText, 155, 5, 1);
  
  // HUD UI - Floating Point Speed Tracker
  canvas.setTextDatum(TL_DATUM);
  char speedText[16]; 
  // Using integer isolation trick so floats don't fragment the ESP32 PROGMEM
  sprintf(speedText, "SPD: %d.%d", (int)dinoSpeed, (int)(dinoSpeed * 10) % 10);
  canvas.drawString(speedText, 5, 5, 1);
  
  canvas.pushSprite(0, 0);
}

void dino_updateGameOver() {
  bool currentA = digitalRead(btnA) == LOW;
  bool currentB = digitalRead(btnB) == LOW;
  
  if (currentA && !dinoPrevBtnA) { 
    playSound(SFX_SHOOT); 
    dino_initGame(); 
    gameState = STATE_DINO_PLAYING; 
  }
  if (currentB && !dinoPrevBtnB) { 
    playSound(SFX_DASH); 
    gameState = STATE_SYSTEM_MENU; // EXIT TO MENU
  } 
  
  dinoPrevBtnA = currentA; dinoPrevBtnB = currentB;

  canvas.fillRect(30, 40, 100, 52, tft.color565(50, 50, 50));
  canvas.drawRect(30, 40, 100, 52, TFT_WHITE);
  canvas.setTextColor(TFT_RED); canvas.setTextDatum(MC_DATUM);
  canvas.drawString("GAME OVER", tft.width()/2, 55, 2);
  canvas.setTextColor(TFT_WHITE); canvas.drawString("A: Restart", tft.width()/2, 72, 1);
  canvas.setTextColor(TFT_YELLOW); canvas.drawString("B: Main Menu", tft.width()/2, 84, 1);
  canvas.pushSprite(0, 0);
}

// ==========================================
//               ACTION RPG
// ==========================================
int actMenuSelection = 0;
int actGameOverSelection = 0;
int actTransitionY = 0;

bool actPrevMenuUp = true, actPrevMenuDown = true, actPrevMenuA = true, actPrevMenuB = true;

void act_resetMenuInput() {
  actPrevMenuUp = true; actPrevMenuDown = true; actPrevMenuA = true; actPrevMenuB = true;
}

struct Player {
  int x, y, prevX, prevY, speed, facingX, facingY, health;
  bool isDashing; unsigned long dashTimer; int dashDirX, dashDirY;
  bool isInvulnerable; unsigned long invulnerableTimer;
};
Player player;

enum EnemyState { STATE_IDLE, STATE_PATROL, STATE_AGGRO, STATE_SPAWNING, STATE_PREP_SHOOT, STATE_FROZEN_SHOOT, STATE_PREP_DASH, STATE_DASHING, STATE_PREP_SLAM, STATE_SLAMMING };
struct Enemy {
  bool active; EnemyType type; EnemyState state;
  float x, y, prevX, prevY, speed, dirX, dirY, lockDirX, lockDirY, targetX, targetY;
  unsigned long timer; int damage, health, w, h;
};
const int MAX_ENEMIES = 10; Enemy enemies[MAX_ENEMIES];

struct Bullet { bool active; float x, y, prevX, prevY, dirX, dirY, speed; };
const int MAX_BULLETS = 3; Bullet bullets[MAX_BULLETS];

struct EnemyBullet { bool active; float x, y, dirX, dirY, speed; int damage; };
const int MAX_ENEMY_BULLETS = 5; EnemyBullet enemyBullets[MAX_ENEMY_BULLETS];

int currentStage = 0; int currentWave = 0; bool gatesActive = false;
int hitFlashTimer = 0; int playerHistoryX[5]; int playerHistoryY[5];
struct Orb { bool active; int x, y, healAmount, radius; };
Orb healingOrb = {false, 0, 0, 30, 4};

const uint16_t heroSprite[64] PROGMEM = {
  0x0000, 0x0000, 0x03FF, 0x03FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0116, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0xC618, 0xC618, 0xC618, 0xC618, 0x0000, 0x0000, 0x0000, 0x0000, 0xC618, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x6B6D, 0xC618, 0x0000, 0xC618, 0x0000, 0x0000, 0x0000, 0x0116, 0x0000, 0x0000, 0x0000, 0x0000, 0x0116, 0x0000,
  0x0000, 0x0000, 0xC618, 0xC618, 0xC618, 0xC618, 0x0000, 0x0000, 0x0000, 0x0000, 0x6B6D, 0x0000, 0x0000, 0x6B6D, 0x0000, 0x0000
};
const uint16_t batSprite[64] PROGMEM = {
  0x0000, 0x0332, 0x0000, 0x0000, 0x0000, 0x0000, 0x0332, 0x0000, 0x0000, 0x0332, 0x018C, 0x018C, 0x018C, 0x018C, 0x0332, 0x0000,
  0x0332, 0x0332, 0x018C, 0x018C, 0x018C, 0x018C, 0x0332, 0x0332, 0x0332, 0x0000, 0x018C, 0xF800, 0xF800, 0x018C, 0x0000, 0x0332,
  0x0332, 0x0000, 0x018C, 0x018C, 0x018C, 0x018C, 0x0000, 0x0332, 0x018C, 0x0000, 0x018C, 0x0000, 0x0000, 0x018C, 0x0000, 0x018C,
  0x0000, 0x0332, 0x018C, 0x0000, 0x0000, 0x018C, 0x0332, 0x0000, 0x0000, 0x0000, 0x0332, 0x0000, 0x0000, 0x0332, 0x0000, 0x0000
};
const uint16_t shooterSprite[64] PROGMEM = {
  0x0000, 0x0000, 0x1C6A, 0x0000, 0x0000, 0x1C6A, 0x0000, 0x0000, 0x0000, 0x1C6A, 0x1C6A, 0x0000, 0x0000, 0x1C6A, 0x1C6A, 0x0000, 
  0x0000, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x0000, 0x0000, 0x1C6A, 0x0000, 0x1C6A, 0x1C6A, 0x0000, 0x1C6A, 0x0000, 
  0x0000, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x1C6A, 0x0000, 0x1AD8, 0x1AD8, 0xA288, 0xA288, 0xA288, 0xA288, 0x1AD8, 0x1AD8, 
  0x1AD8, 0xA288, 0xA288, 0x0000, 0x0000, 0xA288, 0xA288, 0x1AD8, 0x0000, 0xA288, 0xA288, 0x0000, 0x0000, 0xA288, 0xA288, 0x0000  
};
const uint16_t bruteSprite[144] PROGMEM = {
  0x0000, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xF800, 0xFFE0, 0xF800, 0xF800, 0xFFE0, 0xF800, 0x0000, 0x0000, 0x0000,
  0x0000, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0x0000, 0xF800, 0xF800, 0x8010, 0x8010, 0xF800, 0xF800, 0xF800, 0xF800, 0x8010, 0x8010, 0xF800, 0xF800,
  0xF800, 0xF800, 0x0000, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x0000, 0xF800, 0xF800, 0x0000, 0xF800, 0xF800, 0x0000, 0x8010, 0x8010, 0x8010, 0x8010, 0x0000, 0xF800, 0xF800, 0x0000,
  0x0000, 0x0000, 0x0000, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x8010, 0x8010, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 
};

int ammo = 6;              
const int MAX_AMMO = 6;
int comboStep = 0;         
unsigned long comboTimer = 0;
const int comboWindow = 800; 
unsigned long meleeCooldownTimer = 0;
const int meleeCooldown = 175; 
unsigned long gunCooldownTimer = 0;
const int gunCooldown = 300; 

bool actPrevBtnA = true;
bool actPrevBtnB = true;

bool drawMelee = false;
int meleeHitOffsetX, meleeHitOffsetY, meleeHitW, meleeHitH;
unsigned long meleeDrawTimer = 0;

unsigned long dashDuration = 350; 
int dashSpeed = 4;                
unsigned long lastTapTime[4] = {0, 0, 0, 0}; 
const unsigned long iframeDuration = 700; 
bool prevDpad[4] = {true, true, true, true};
const int doubleTapWindow = 250; 

bool checkTerrainCollision(int x, int y, int w, int h) {
  if (currentStage >= MAX_STAGES) return false;
  if (!stages[currentStage].terrain.hasWall) return false;
  int screenWidth = tft.width();
  int screenHeight = tft.height();
  int wallX = screenWidth / 2 + stages[currentStage].terrain.xCenterOffset;
  int wallY = screenHeight / 2 + stages[currentStage].terrain.yCenterOffset;
  return checkCollision(x, y, w, h, wallX, wallY, stages[currentStage].terrain.w, stages[currentStage].terrain.h);
}

void act_spawnWave() {
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
        if ((dx*dx + dy*dy) > 2500) { 
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
      enemies[enemyIndex].lockDirX = 0;
      enemies[enemyIndex].lockDirY = 0;
      
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

void act_initEntities() {
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
  actPrevBtnA = true;
  actPrevBtnB = true;

  for (int i = 0; i < 4; i++) {
    lastTapTime[i] = 0;
    prevDpad[i] = true;
  }

  for(int i=0; i<MAX_BULLETS; i++) { bullets[i].active = false; }
  for(int i=0; i<MAX_ENEMY_BULLETS; i++) { enemyBullets[i].active = false; }

  currentStage = 0;
  currentWave = 0;
  gatesActive = false;
  healingOrb.active = false;
  hitFlashTimer = 0;
  for(int i=0; i<5; i++) { playerHistoryX[i] = player.x; playerHistoryY[i] = player.y; }
  
  act_spawnWave();
}

void act_updateEnemies() {
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
          if (millis() - e.timer > 1000) { e.state = STATE_IDLE; e.timer = millis(); }
        } 
        else if (e.state == STATE_IDLE) {
          if (distSq < 4000) e.state = STATE_AGGRO; 
          else if (millis() - e.timer > 2000) { 
            e.state = STATE_PATROL; e.timer = millis(); e.dirX = random(-1, 2); e.dirY = random(-1, 2); 
          }
        } 
        else if (e.state == STATE_PATROL) {
          if (distSq < 2500) e.state = STATE_AGGRO;
          e.x += e.dirX * e.speed; e.y += e.dirY * e.speed;
          if (millis() - e.timer > 500) { e.state = STATE_IDLE; e.timer = millis(); }
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
          if (millis() - e.timer > 1000) { e.state = STATE_IDLE; e.timer = millis(); }
        }
        else if (e.state == STATE_PREP_SHOOT) {
          if (millis() - e.timer < 1000) {
            float dx = (player.x + 4) - (e.x + 4);
            float dy = (player.y + 4) - (e.y + 4);
            float mag = sqrt(dx*dx + dy*dy);
            if (mag > 0) { e.lockDirX = dx/mag; e.lockDirY = dy/mag; }
            else { e.lockDirX = 0; e.lockDirY = 0; }
          } else {
            e.state = STATE_FROZEN_SHOOT; e.timer = millis();
          }
        }
        else if (e.state == STATE_FROZEN_SHOOT) {
          if (millis() - e.timer > 500) {
            for (int b = 0; b < MAX_ENEMY_BULLETS; b++) {
              if (!enemyBullets[b].active) {
                enemyBullets[b].active = true;
                enemyBullets[b].x = e.x + 4; enemyBullets[b].y = e.y + 4;
                enemyBullets[b].dirX = e.lockDirX; enemyBullets[b].dirY = e.lockDirY;
                enemyBullets[b].speed = 2.5; enemyBullets[b].damage = 20;
                break;
              }
            }
            e.state = STATE_IDLE; e.timer = millis();
          }
        }
        else {
          if (millis() - e.timer > 3000) { 
            e.state = STATE_PREP_SHOOT; e.timer = millis();
          } else {
            float dx = player.x - e.x; float dy = player.y - e.y;
            float mag = sqrt(dx*dx + dy*dy);
            if (mag > 0) {
              if (mag < 45) { e.x -= (dx/mag) * e.speed; e.y -= (dy/mag) * e.speed; } 
              else if (mag > 85) { e.x += (dx/mag) * e.speed; e.y += (dy/mag) * e.speed; } 
              else {
                if ((millis() / 2000) % 2 == 0) { e.x += (-dy/mag) * e.speed; e.y += (dx/mag) * e.speed; } 
                else { e.x -= (-dy/mag) * e.speed; e.y -= (dx/mag) * e.speed; }
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
          if (millis() - e.timer > 3000 && distSq < 10000) { 
            e.state = STATE_PREP_DASH; e.timer = millis();
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
           if (millis() - e.timer > 500) { e.state = STATE_DASHING; e.timer = millis(); e.damage = 30; }
        }
        else if (e.state == STATE_DASHING) {
           e.x += e.lockDirX * 4; e.y += e.lockDirY * 4;
           if (millis() - e.timer > 200) { 
             e.state = STATE_PREP_SLAM; e.timer = millis(); e.damage = 15; 
             e.targetX = e.x + e.w/2 + e.lockDirX * 16 - 12; e.targetY = e.y + e.h/2 + e.lockDirY * 16 - 12;
           }
        }
        else if (e.state == STATE_PREP_SLAM) {
           if (millis() - e.timer > 500) {
             e.state = STATE_SLAMMING; e.timer = millis();
             if (!player.isInvulnerable && !player.isDashing) {
                if (checkCollision(player.x, player.y, 8, 8, e.targetX, e.targetY, 24, 24)) {
                   player.health -= 35; 
                   player.isInvulnerable = true; 
                   player.invulnerableTimer = millis(); 
                   hitFlashTimer = 5;
                   playSound(SFX_HIT); // ADDED HIT SOUND
                }
             }
           }
        }
        else if (e.state == STATE_SLAMMING) {
           if (millis() - e.timer > 200) { e.state = STATE_AGGRO; e.timer = millis(); }
        }
        break;
      }
    }

    if (checkTerrainCollision(e.x, e.y, e.w, e.h)) {
      e.x = e.prevX; e.y = e.prevY;
    }

    int screenWidth = tft.width(); int screenHeight = tft.height();
    if (e.x < 0) e.x = 0; if (e.y < 0) e.y = 0;
    if (e.x > screenWidth - e.w) e.x = screenWidth - e.w;
    if (e.y > screenHeight - e.h) e.y = screenHeight - e.h;
  }
  
  if (!anyActive && !gatesActive) {
    if (currentStage < MAX_STAGES) {
      if (currentWave + 1 >= stages[currentStage].numWaves && stages[currentStage].numWaves > 0) {
        gatesActive = true; healingOrb.active = true; healingOrb.x = tft.width() / 2 - 10; healingOrb.y = tft.height() / 2;
      } else if (stages[currentStage].numWaves == 0) {
        gatesActive = true; healingOrb.active = true; healingOrb.x = tft.width() / 2 - 10; healingOrb.y = tft.height() / 2;
      } else {
        currentWave++; act_spawnWave();
      }
    }
  }
}

void act_updatePlayer() {
  if (hitFlashTimer > 0) hitFlashTimer--;

  for(int i=4; i>0; i--) {
    playerHistoryX[i] = playerHistoryX[i-1]; playerHistoryY[i] = playerHistoryY[i-1];
  }
  playerHistoryX[0] = player.x; playerHistoryY[0] = player.y;
  player.prevX = player.x; player.prevY = player.y;

  bool currentUp = digitalRead(btnUp) == LOW;
  bool currentDown = digitalRead(btnDown) == LOW;
  bool currentLeft = digitalRead(btnLeft) == LOW;
  bool currentRight = digitalRead(btnRight) == LOW;

  if (!player.isDashing) {
    if (currentUp && !prevDpad[0]) {
      if (millis() - lastTapTime[0] < doubleTapWindow) { 
        player.isDashing = true; player.dashDirX = 0; player.dashDirY = -1; player.dashTimer = millis(); 
        playSound(SFX_DASH); // ADDED DASH SOUND
      }
      lastTapTime[0] = millis();
    }
    if (currentDown && !prevDpad[1]) {
      if (millis() - lastTapTime[1] < doubleTapWindow) { 
        player.isDashing = true; player.dashDirX = 0; player.dashDirY = 1; player.dashTimer = millis(); 
        playSound(SFX_DASH); // ADDED DASH SOUND
      }
      lastTapTime[1] = millis();
    }
    if (currentLeft && !prevDpad[2]) {
      if (millis() - lastTapTime[2] < doubleTapWindow) { 
        player.isDashing = true; player.dashDirX = -1; player.dashDirY = 0; player.dashTimer = millis(); 
        playSound(SFX_DASH); // ADDED DASH SOUND
      }
      lastTapTime[2] = millis();
    }
    if (currentRight && !prevDpad[3]) {
      if (millis() - lastTapTime[3] < doubleTapWindow) { 
        player.isDashing = true; player.dashDirX = 1; player.dashDirY = 0; player.dashTimer = millis(); 
        playSound(SFX_DASH); // ADDED DASH SOUND
      }
      lastTapTime[3] = millis();
    }
  }

  prevDpad[0] = currentUp; prevDpad[1] = currentDown;
  prevDpad[2] = currentLeft; prevDpad[3] = currentRight;

  if (player.isDashing) {
    for (int s = 0; s < dashSpeed; s++) {
      player.x += player.dashDirX;
      if (checkTerrainCollision(player.x, player.prevY, 8, 8)) { player.x -= player.dashDirX; break; }
    }
    for (int s = 0; s < dashSpeed; s++) {
      player.y += player.dashDirY;
      if (checkTerrainCollision(player.x, player.y, 8, 8)) { player.y -= player.dashDirY; break; }
    }

    if (millis() - player.dashTimer > dashDuration) { player.isDashing = false; }
  } else {
    bool moved = false; int moveX = 0, moveY = 0;
    
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
  if (currentBtnA && !actPrevBtnA) {
    if (millis() - meleeCooldownTimer > meleeCooldown) { 
      meleeCooldownTimer = millis();
      
      if (comboStep == 0 || millis() - comboTimer > comboWindow) {
        comboStep = 1; comboTimer = millis();
      } else {
        comboStep = 2; comboTimer = millis(); 
      }
      
      if (player.facingX != 0) {  
        if (comboStep == 1) { meleeHitW = 16; meleeHitH = 4; meleeHitOffsetX = (player.facingX == 1 ? 8 : -16); meleeHitOffsetY = 2; } 
        else { meleeHitW = 8; meleeHitH = 16; meleeHitOffsetX = (player.facingX == 1 ? 8 : -8); meleeHitOffsetY = -4; }
      } else {                    
        if (comboStep == 1) { meleeHitW = 4; meleeHitH = 16; meleeHitOffsetX = 2; meleeHitOffsetY = (player.facingY == 1 ? 8 : -16); } 
        else { meleeHitW = 16; meleeHitH = 8; meleeHitOffsetX = -4; meleeHitOffsetY = (player.facingY == 1 ? 8 : -8); }
      }

      drawMelee = true;   
      meleeDrawTimer = millis(); 

      for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active || enemies[i].state == STATE_SPAWNING) continue;
        if (checkCollision(player.x + meleeHitOffsetX, player.y + meleeHitOffsetY, meleeHitW, meleeHitH, enemies[i].x, enemies[i].y, enemies[i].w, enemies[i].h)) {
          enemies[i].health -= 5;
          playSound(SFX_HIT); // ADDED MELEE HIT SOUND
          
          int kb = (comboStep == 1) ? 2 : 12;
          enemies[i].x += player.facingX * kb;
          enemies[i].y += player.facingY * kb;
          if (comboStep == 2 && ammo < MAX_AMMO) ammo++; 
          if (enemies[i].health <= 0) enemies[i].active = false;
        }
      }
      if (comboStep == 2) comboStep = 0; 
    }
  }
  actPrevBtnA = currentBtnA;

  // Handle Gun (btnB)
  bool currentBtnB = digitalRead(btnB) == LOW;
  if (currentBtnB && !actPrevBtnB) { 
    if (millis() - gunCooldownTimer > gunCooldown && ammo > 0) {
      gunCooldownTimer = millis(); 
      ammo--;
      playSound(SFX_SHOOT); // ADDED SHOOT SOUND

      for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
          bullets[i].active = true;
          bullets[i].x = player.x + 4; bullets[i].y = player.y + 4;
          bullets[i].prevX = bullets[i].x; bullets[i].prevY = bullets[i].y;
          bullets[i].dirX = player.facingX; bullets[i].dirY = player.facingY;
          if (bullets[i].dirX == 0 && bullets[i].dirY == 0) bullets[i].dirX = 1;
          bullets[i].speed = 5;
          break;
        }
      }
    }
  }
  actPrevBtnB = currentBtnB;

  // Update Bullets
  for(int i=0; i<MAX_BULLETS; i++) {
    if(bullets[i].active) {
      bullets[i].prevX = bullets[i].x; bullets[i].prevY = bullets[i].y;
      bullets[i].x += bullets[i].dirX * bullets[i].speed;
      bullets[i].y += bullets[i].dirY * bullets[i].speed;
      
      bool hitEnemy = false;
      for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!enemies[e].active || enemies[e].state == STATE_SPAWNING) continue;
        if (checkCollision(bullets[i].x, bullets[i].y, 2, 2, enemies[e].x, enemies[e].y, enemies[e].w, enemies[e].h)) {
          bullets[i].active = false;
          enemies[e].health -= 5;
          enemies[e].x += bullets[i].dirX * 10; enemies[e].y += bullets[i].dirY * 10;
          hitEnemy = true;
          playSound(SFX_HIT); // ADDED BULLET HIT SOUND
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
          playSound(SFX_HIT); // ADDED TAKING DAMAGE SOUND
        }
      }
      if (enemyBullets[i].x < 0 || enemyBullets[i].x > tft.width() || enemyBullets[i].y < 0 || enemyBullets[i].y > tft.height() || checkTerrainCollision(enemyBullets[i].x, enemyBullets[i].y, 3, 3)) {
        enemyBullets[i].active = false;
      }
    }
  }

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
        playSound(SFX_HIT); // ADDED TAKING DAMAGE SOUND
        break; 
      }
    }
  }

  int screenWidth = tft.width(); int screenHeight = tft.height();
  if (player.x < 0) player.x = 0; if (player.y < 0) player.y = 0;
  if (player.x > screenWidth - 8) player.x = screenWidth - 8; if (player.y > screenHeight - 8) player.y = screenHeight - 8;

  if (healingOrb.active) {
    if (checkCollision(player.x, player.y, 8, 8, healingOrb.x - healingOrb.radius, healingOrb.y - healingOrb.radius, healingOrb.radius*2, healingOrb.radius*2)) {
      healingOrb.active = false;
      player.health += healingOrb.healAmount;
      playSound(SFX_HEAL); // ADDED HEAL SOUND
      if (player.health > 100) player.health = 100;
    }
  }

  if (gatesActive) {
    if (checkCollision(player.x, player.y, 8, 8, screenWidth / 2 - 8, 0, 16, 8) || 
        checkCollision(player.x, player.y, 8, 8, screenWidth / 2 - 8, screenHeight - 8, 16, 8) || 
        checkCollision(player.x, player.y, 8, 8, 0, screenHeight / 2 - 8, 8, 16) || 
        checkCollision(player.x, player.y, 8, 8, screenWidth - 8, screenHeight / 2 - 8, 8, 16)) {
      
      currentStage++; currentWave = 0; gatesActive = false;
      player.x = 76; player.y = 60;
      if (currentStage < MAX_STAGES) act_spawnWave();
    }
  }
}

void act_renderArena(bool pushToScreen = true) {
  canvas.fillSprite(TFT_BLACK);
  
  if (currentStage < MAX_STAGES && stages[currentStage].terrain.hasWall) {
    int wallX = tft.width() / 2 + stages[currentStage].terrain.xCenterOffset;
    int wallY = tft.height() / 2 + stages[currentStage].terrain.yCenterOffset;
    canvas.fillRect(wallX, wallY, stages[currentStage].terrain.w, stages[currentStage].terrain.h, TFT_RED);
  }

  if (gatesActive) {
    canvas.fillRect(tft.width() / 2 - 8, 0, 16, 8, TFT_WHITE); 
    canvas.fillRect(tft.width() / 2 - 8, tft.height() - 8, 16, 8, TFT_WHITE);
    canvas.fillRect(0, tft.height() / 2 - 8, 8, 16, TFT_WHITE); 
    canvas.fillRect(tft.width() - 8, tft.height() / 2 - 8, 8, 16, TFT_WHITE);
  }
  
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) canvas.fillRect(bullets[i].x, bullets[i].y, 2, 2, TFT_YELLOW);
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (enemyBullets[i].active) canvas.fillRect(enemyBullets[i].x, enemyBullets[i].y, 3, 3, TFT_ORANGE);
  }

  if (drawMelee) {
    canvas.fillRect(player.x + meleeHitOffsetX, player.y + meleeHitOffsetY, meleeHitW, meleeHitH, TFT_WHITE);
    if (millis() - meleeDrawTimer > 50) drawMelee = false;
  }
  
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      if (enemies[i].state == STATE_SPAWNING) {
        long elapsed = millis() - enemies[i].timer;
        if ((elapsed / 100) % 2 == 0) {
          int size = (elapsed * 8) / 1000; if (size > 8) size = 8;
          canvas.drawRect(enemies[i].x + 4 - size/2, enemies[i].y + 4 - size/2, size, size, TFT_RED);
        }
      } else {
        if (enemies[i].type == ENEMY_BAT) {
          canvas.pushImage(enemies[i].x, enemies[i].y, 8, 8, batSprite);
        } else if (enemies[i].type == ENEMY_SHOOTER) {
          canvas.pushImage(enemies[i].x, enemies[i].y, 8, 8, shooterSprite);
          if (enemies[i].state == STATE_PREP_SHOOT || enemies[i].state == STATE_FROZEN_SHOOT) {
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
    
  if (!player.isInvulnerable || (millis() / 100) % 2 == 0) {
    canvas.pushImage(player.x, player.y, 8, 8, heroSprite);
  }

  if (player.isDashing) {
    for (int i = 0; i < 5; i += 2) canvas.drawRect(playerHistoryX[i], playerHistoryY[i], 8, 8, TFT_CYAN);
  }

  if (healingOrb.active) {
    int orbPulse = (millis() / 150) % 3;
    canvas.fillCircle(healingOrb.x, healingOrb.y, healingOrb.radius + orbPulse, TFT_GREEN);
    canvas.fillCircle(healingOrb.x, healingOrb.y, healingOrb.radius, TFT_WHITE);
  }

  if (hitFlashTimer > 0) {
    canvas.drawRect(0, 0, tft.width(), tft.height(), TFT_RED); 
    canvas.drawRect(1, 1, tft.width()-2, tft.height()-2, TFT_RED);
  }

  canvas.fillRect(2, 2, 50, 6, TFT_BLUE); 
  int healthW = (player.health * 50) / 100; if (healthW < 0) healthW = 0;
  canvas.fillRect(2, 2, healthW, 6, TFT_GREEN); canvas.drawRect(2, 2, 50, 6, TFT_WHITE);
  
  for (int i = 0; i < MAX_AMMO; i++) {
    if (i < ammo) canvas.fillRect(tft.width() - 8 - (i * 6), 2, 4, 6, TFT_YELLOW);
    else canvas.drawRect(tft.width() - 8 - (i * 6), 2, 4, 6, tft.color565(80, 80, 80));
  }
    
  if (pushToScreen) canvas.pushSprite(0, 0);
}

void act_updateTitle() {
  bool currentUp = digitalRead(btnUp) == LOW;
  bool currentDown = digitalRead(btnDown) == LOW;
  bool currentA = digitalRead(btnA) == LOW;
  bool currentB = digitalRead(btnB) == LOW;

  if (currentUp && !actPrevMenuUp) { 
    actMenuSelection--; if (actMenuSelection < 0) actMenuSelection = 1; playSound(SFX_DASH); 
  }
  if (currentDown && !actPrevMenuDown) { 
    actMenuSelection++; if (actMenuSelection > 1) actMenuSelection = 0; playSound(SFX_DASH); 
  }
  if (currentA && !actPrevMenuA) {
    playSound(SFX_SHOOT);
    if (actMenuSelection == 0) { act_initEntities(); act_resetMenuInput(); gameState = STATE_ACT_ARENA; } 
    else { act_resetMenuInput(); gameState = STATE_ACT_TUTORIAL; }
  }
  if (currentB && !actPrevMenuB) { 
    playSound(SFX_DASH); gameState = STATE_SYSTEM_MENU; // EXIT TO SYSTEM MENU
  } 
  
  actPrevMenuUp = currentUp; actPrevMenuDown = currentDown; 
  actPrevMenuA = currentA; actPrevMenuB = currentB;
}

void act_renderTitle() {
  canvas.fillSprite(TFT_BLACK); int cx = tft.width() / 2;
  drawMenuButton(cx, tft.height() / 2 - 20, 80, 24, "START", actMenuSelection == 0);
  drawMenuButton(cx, tft.height() / 2 + 20, 80, 24, "TUTORIAL", actMenuSelection == 1);
  
  canvas.setTextColor(TFT_YELLOW); canvas.setTextDatum(BC_DATUM);
  canvas.drawString("B: System Menu", cx, tft.height() - 5, 1);
  canvas.pushSprite(0, 0);
}

void act_updateTutorial() {
  bool currentB = digitalRead(btnB) == LOW;
  if (currentB && !actPrevMenuB) { act_resetMenuInput(); gameState = STATE_ACT_TITLE; }
  actPrevMenuB = currentB;
}

void act_renderTutorial() {
  canvas.fillSprite(TFT_BLACK); canvas.setTextColor(TFT_WHITE); canvas.setTextDatum(TL_DATUM);
  canvas.drawString("TUTORIAL", 10, 10, 1); 
  canvas.drawString("- D-Pad: Move/Dash", 10, 30, 1);
  canvas.drawString("- A: Melee Combo", 10, 50, 1); 
  canvas.drawString("- B: Shoot Gun", 10, 70, 1);
  canvas.setTextColor(TFT_YELLOW); canvas.drawString("Press B to return", 10, 100, 1);
  canvas.pushSprite(0, 0);
}

void act_updateGameOverTransition() {
  actTransitionY -= 10;
  if (actTransitionY < 0) { gameState = STATE_ACT_GAMEOVER; actGameOverSelection = 0; act_resetMenuInput(); }
}

void act_renderGameOverTransition() {
  act_renderArena(false);
  for (int y = max(0, actTransitionY); y < tft.height(); y += 4) {
    for (int x = 0; x < tft.width(); x += 4) {
      canvas.fillRect(x, y, 4, 4, random(2) ? TFT_WHITE : TFT_BLACK);
    }
  }
  canvas.pushSprite(0, 0);
}

void act_updateGameOver() {
  bool currentUp = digitalRead(btnUp) == LOW;
  bool currentDown = digitalRead(btnDown) == LOW;
  bool currentA = digitalRead(btnA) == LOW;
  
  if (currentUp && !actPrevMenuUp) { 
    actGameOverSelection--; if (actGameOverSelection < 0) actGameOverSelection = 1; playSound(SFX_DASH); 
  }
  if (currentDown && !actPrevMenuDown) { 
    actGameOverSelection++; if (actGameOverSelection > 1) actGameOverSelection = 0; playSound(SFX_DASH); 
  }
  if (currentA && !actPrevMenuA) {
    playSound(SFX_SHOOT);
    if (actGameOverSelection == 0) { act_initEntities(); act_resetMenuInput(); gameState = STATE_ACT_ARENA; } 
    else { act_resetMenuInput(); gameState = STATE_ACT_TITLE; actMenuSelection = 0; }
  }
  actPrevMenuUp = currentUp; actPrevMenuDown = currentDown; actPrevMenuA = currentA;
}

void act_renderGameOver() {
  canvas.fillSprite(TFT_BLACK); canvas.setTextColor(TFT_RED); canvas.setTextDatum(MC_DATUM);
  canvas.drawString("GAME OVER", tft.width() / 2, tft.height() / 4, 2);
  
  int cx = tft.width() / 2;
  drawMenuButton(cx, tft.height() / 2 + 10, 90, 24, "START OVER", actGameOverSelection == 0);
  drawMenuButton(cx, tft.height() / 2 + 40, 90, 24, "TITLE SCREEN", actGameOverSelection == 1);
  canvas.pushSprite(0, 0);
}

// ==========================================
//             CORE ARDUINO
// ==========================================
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
  initAudio(); 
}

void loop() {
  if (millis() - lastFrameTime < frameDelay) return; 
  lastFrameTime = millis(); 

  switch (gameState) {
    case STATE_SYSTEM_MENU:
      updateSystemMenu();
      renderSystemMenu();
      break;

    // --- ACTION RPG ---
    case STATE_ACT_TITLE:
      act_updateTitle(); act_renderTitle();
      break;
    case STATE_ACT_TUTORIAL:
      act_updateTutorial(); act_renderTutorial();
      break;
    case STATE_ACT_ARENA:
      act_updateEnemies(); 
      act_updatePlayer();
      if (player.health <= 0) { 
        gameState = STATE_ACT_GAMEOVER_TRANS; 
        actTransitionY = tft.height(); 
      } else {
        act_renderArena(true);
      }
      break;
    case STATE_ACT_GAMEOVER_TRANS:
      act_updateGameOverTransition(); act_renderGameOverTransition();
      break;
    case STATE_ACT_GAMEOVER:
      act_updateGameOver(); act_renderGameOver();
      break;

    // --- DINO RUN ---
    case STATE_DINO_TITLE:
      dino_updateTitle();
      break;
    case STATE_DINO_PLAYING:
      dino_updatePlaying();
      break;
    case STATE_DINO_GAMEOVER:
      dino_updateGameOver();
      break;
  }
}
