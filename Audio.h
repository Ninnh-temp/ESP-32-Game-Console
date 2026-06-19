#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include <driver/i2s.h>

// --- I2S PIN DEFINITIONS ---
#define I2S_DOUT      25
#define I2S_BCLK      26
#define I2S_LRC       27

enum SoundEffect { SFX_NONE, SFX_SHOOT, SFX_HIT, SFX_DASH, SFX_HEAL };

// Volatile because they are accessed by both Core 0 (Audio) and Core 1 (Game)
volatile SoundEffect currentSFX = SFX_NONE;
volatile int sfxFrame = 0; 

// --- THE AUDIO SYNTHESIZER (Runs on Core 0) ---
void audioTask(void *pvParameters) {
  int16_t sampleBuffer[256];
  size_t bytesWritten;
  
  while (true) {
    if (currentSFX == SFX_NONE) {
      memset(sampleBuffer, 0, sizeof(sampleBuffer));
      i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
      continue;
    }

    for (int i = 0; i < 256; i++) {
      int16_t sample = 0;
      sfxFrame++;

      switch (currentSFX) {
        case SFX_SHOOT:
          if (sfxFrame > 4000) currentSFX = SFX_NONE;
          else sample = ((sfxFrame % (20 + (sfxFrame/100))) < 10) ? 8000 : -8000;
          break;

        case SFX_HIT:
          if (sfxFrame > 3000) currentSFX = SFX_NONE;
          else sample = random(-10000, 10000) * (1.0 - (sfxFrame / 3000.0));
          break;

        case SFX_DASH:
          if (sfxFrame > 2000) currentSFX = SFX_NONE;
          else sample = ((sfxFrame % (60 - (sfxFrame/50))) < 30) ? 6000 : -6000;
          break;
          
        case SFX_HEAL:
          if (sfxFrame > 5000) currentSFX = SFX_NONE;
          else sample = ((sfxFrame % 15) < 7) ? 4000 : -4000;
          break;
          
        default:
          currentSFX = SFX_NONE;
          break;
      }
      sampleBuffer[i] = sample;
    }
    i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
  }
}

// --- HELPER FUNCTIONS ---
void playSound(SoundEffect sfx) {
  currentSFX = sfx;
  sfxFrame = 0;
}

void initAudio() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = 16000, 
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true
  };
  
  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK,
      .ws_io_num = I2S_LRC,
      .data_out_num = I2S_DOUT,
      .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  // Pin the synthesizer to Core 0
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 2048, NULL, 1, NULL, 0); 
}

#endif