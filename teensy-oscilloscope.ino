#include <SPI.h>
#include <Wire.h>
#include <ILI9341_t3.h>
#include "Adafruit_seesaw.h"

#define TFT_CS 10
#define TFT_DC 9
ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC);

#define CH1_PIN A0
#define CH2_PIN A1

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define BORDER 10
#define GRID_TOP BORDER
#define GRID_BOTTOM (SCREEN_HEIGHT - BORDER)
#define GRID_HEIGHT (GRID_BOTTOM - GRID_TOP)
#define MIDLINE (GRID_TOP + GRID_HEIGHT / 2)

#define WAVE_SEPARATION 40
#define WAVE_HEIGHT 40

Adafruit_seesaw encoder1; // 0x36 - cursors or amplitude
Adafruit_seesaw encoder2; // 0x37 - time scale and pause
Adafruit_seesaw encoder3; // 0x38 - trigger level cursor and trigger toggle

// Cursor positions
int cursorY1 = MIDLINE - WAVE_SEPARATION;
int cursorY2 = MIDLINE + WAVE_SEPARATION;
int triggerCursorY = MIDLINE - WAVE_SEPARATION;
int prevCursorY1 = -1, prevCursorY2 = -1, prevTriggerCursorY = -1;

// Sweep positions
int sweepX = 0, sweepCount = 0;
int prevX = 0, prevY1 = 0, prevY2 = 0;

// Encoder states
int32_t lastEnc1 = 0, lastEnc2 = 0, lastEnc3 = 0;
uint32_t lastBtn1 = 1, lastBtn2 = 1, lastBtn3 = 1;

// Modes and flags
bool modeCursor = true;
bool paused = false;
bool triggerArmed = true;
bool triggerEnabled = false; // Set trigger off by default
int prevRawCH1 = 0;

// Scaling factors
float ampScale = 1.0;
const float ampStep = 0.1, ampMin = 0.1, ampMax = 2.5;
float timeScale = 0.2;
const float timeStep = 0.1, timeMin = 0.2, timeMax = 100.0;

// Timing control
unsigned long lastSampleMicros = 0;
unsigned long lastUIUpdate = 0;
const unsigned long uiUpdateInterval = 20;

// Draw voltage label at a specific Y position
void drawVoltageLabel(int y, float voltage, uint16_t color) {
  tft.setTextColor(color, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setCursor(SCREEN_WIDTH - 50, y - 4);
  tft.printf("%.2fV", voltage);
}

// Draw oscilloscope grid and labels
void drawGrid() {
  tft.fillScreen(ILI9341_BLACK);
  for (int i = 0; i <= 8; i++) {
    int y = GRID_TOP + i * (GRID_HEIGHT / 8);
    tft.drawLine(0, y, SCREEN_WIDTH, y, ILI9341_WHITE);
  }
  for (int i = 0; i <= 8; i++) {
    int x = i * (SCREEN_WIDTH / 8);
    tft.drawLine(x, GRID_TOP, x, GRID_BOTTOM, ILI9341_WHITE);
  }
  // Draw labels for each channel
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(4, 2);
  tft.printf("CH1: %.1fx, 0.5V/div", ampScale);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(4, SCREEN_HEIGHT - 10);
  tft.printf("CH2: %.1fx, 0.5V/div", ampScale);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(SCREEN_WIDTH - 90, 2);
  tft.printf("Time: %.1fms/div", timeScale);
}

// Draw cursors and their voltage labels
void drawCursors() {
  if (prevCursorY1 != cursorY1) {
    tft.drawLine(0, prevCursorY1, SCREEN_WIDTH, prevCursorY1, ILI9341_BLACK);
    prevCursorY1 = cursorY1;
  }
  if (prevCursorY2 != cursorY2) {
    tft.drawLine(0, prevCursorY2, SCREEN_WIDTH, prevCursorY2, ILI9341_BLACK);
    prevCursorY2 = cursorY2;
  }
  if (prevTriggerCursorY != triggerCursorY) {
    tft.drawLine(0, prevTriggerCursorY, SCREEN_WIDTH, prevTriggerCursorY, ILI9341_BLACK);
    prevTriggerCursorY = triggerCursorY;
  }
  // Redraw all cursors
  tft.drawLine(0, cursorY1, SCREEN_WIDTH, cursorY1, ILI9341_RED);
  drawVoltageLabel(cursorY1, (MIDLINE - cursorY1) / (float)(WAVE_HEIGHT * ampScale) * 0.5, ILI9341_RED);
  tft.drawLine(0, cursorY2, SCREEN_WIDTH, cursorY2, ILI9341_RED);
  drawVoltageLabel(cursorY2, (MIDLINE - cursorY2) / (float)(WAVE_HEIGHT * ampScale) * 0.5, ILI9341_RED);
  tft.drawLine(0, triggerCursorY, SCREEN_WIDTH, triggerCursorY, ILI9341_GREEN);
  drawVoltageLabel(triggerCursorY, (MIDLINE - triggerCursorY) / (float)(WAVE_HEIGHT * ampScale) * 0.5, ILI9341_GREEN);
}

// Handle data sampling and waveform drawing
void handleSampling() {
  float sampleIntervalUs = (timeScale * 1000.0) / 40.0;
  if (!paused && micros() - lastSampleMicros >= sampleIntervalUs) {
    lastSampleMicros = micros();

    int raw1 = analogRead(CH1_PIN);
    int raw2 = analogRead(CH2_PIN);
    int triggerLevel = map(triggerCursorY, GRID_BOTTOM, GRID_TOP, 0, 1023);

    // Trigger logic
    if (triggerEnabled) {
      if (triggerArmed && prevRawCH1 < triggerLevel && raw1 >= triggerLevel) {
        triggerArmed = false;
        sweepX = prevX = sweepCount = 0;
        tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ILI9341_BLACK);
        drawGrid();
      } else if (!triggerArmed) {
        // Allow drawing to continue
      } else {
        prevRawCH1 = raw1;
        return; // Wait for trigger
      }
    }

    // Convert samples to screen Y positions
    int y1 = MIDLINE - WAVE_SEPARATION - (int)(map(raw1, 0, 1023, -WAVE_HEIGHT, WAVE_HEIGHT) * ampScale);
    int y2 = MIDLINE + WAVE_SEPARATION - (int)(map(raw2, 0, 1023, -WAVE_HEIGHT, WAVE_HEIGHT) * ampScale);

    // Draw waveforms
    if (sweepX < SCREEN_WIDTH) {
      tft.drawLine(prevX, prevY1, sweepX, y1, ILI9341_CYAN);
      tft.drawLine(prevX, prevY2, sweepX, y2, ILI9341_YELLOW);
    }

    prevY1 = y1; prevY2 = y2; prevX = sweepX; sweepX++;
    if (sweepX >= SCREEN_WIDTH) {
      sweepX = prevX = 0;
      if (++sweepCount >= 1) {
        sweepCount = 0;
        tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ILI9341_BLACK);
        drawGrid();
        triggerArmed = true; // Re-arm trigger
      }
    }
  }
}

// Handle rotary encoder input
void handleEncoders() {
  if (millis() - lastUIUpdate < uiUpdateInterval) return;
  lastUIUpdate = millis();
  drawCursors();

  // Encoder 1: cursors or amplitude scale
  int32_t encPos1 = encoder1.getEncoderPosition();
  if (encPos1 != lastEnc1) {
    int delta = encPos1 - lastEnc1;
    if (modeCursor) {
      cursorY1 = constrain(cursorY1 - delta, GRID_TOP, GRID_BOTTOM);
      cursorY2 = constrain(cursorY2 - delta, GRID_TOP, GRID_BOTTOM);
    } else {
      ampScale += (delta > 0 ? ampStep : -ampStep);
      ampScale = constrain(ampScale, ampMin, ampMax);
      drawGrid();
    }
    lastEnc1 = encPos1;
  }
  if (encoder1.digitalRead(24) == 0 && lastBtn1 == 1) modeCursor = !modeCursor;
  lastBtn1 = encoder1.digitalRead(24);

  // Encoder 2: time scaling and pause
  int32_t encPos2 = encoder2.getEncoderPosition();
  if (encPos2 != lastEnc2) {
    int delta = encPos2 - lastEnc2;
    timeScale += (delta > 0 ? timeStep : -timeStep);
    timeScale = constrain(timeScale, timeMin, timeMax);
    drawGrid();
    lastEnc2 = encPos2;
  }
  if (encoder2.digitalRead(24) == 0 && lastBtn2 == 1) {
    paused = !paused;
    if (!paused) {
      sweepX = prevX = sweepCount = 0;
      triggerArmed = true;
      drawGrid();
    }
  }
  lastBtn2 = encoder2.digitalRead(24);

  // Encoder 3: trigger level and trigger mode toggle
  int32_t encPos3 = encoder3.getEncoderPosition();
  if (encPos3 != lastEnc3) {
    int delta = encPos3 - lastEnc3;
    triggerCursorY = constrain(triggerCursorY - delta, GRID_TOP, GRID_BOTTOM);
    lastEnc3 = encPos3;
  }
  if (encoder3.digitalRead(24) == 0 && lastBtn3 == 1) {
    triggerEnabled = !triggerEnabled;
    Serial.print("Trigger Mode: ");
    Serial.println(triggerEnabled ? "ON" : "OFF");
  }
  lastBtn3 = encoder3.digitalRead(24);
}

void setup() {
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1);
  drawGrid();

  // Initialize I2C and encoders
  Wire.begin();
  encoder1.begin(0x36);
  encoder2.begin(0x37);
  encoder3.begin(0x38);
  encoder1.enableEncoderInterrupt();
  encoder2.enableEncoderInterrupt();
  encoder3.enableEncoderInterrupt();
  encoder1.setEncoderPosition(0);
  encoder2.setEncoderPosition(0);
  encoder3.setEncoderPosition(0);
}

void loop() {
  handleSampling();
  handleEncoders();
}
