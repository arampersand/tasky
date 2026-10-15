#pragma once
#include <Arduino.h>

// ============================================
// Animation System for TaskyCircular
// Lightweight, no framebuffer, ESP32-C3 safe
// ============================================

// Global animation toggle
#define ANIMATIONS_ENABLED 1

// Timing constants - more noticeable animations
#define ANIM_ENTER_MS      250   // Screen enter animation duration (longer)
#define ANIM_CURSOR_MS     120   // Cursor smooth move duration  
#define ANIM_SNAP_MS       150   // Timer snap animation
#define ANIM_INK_MS        100   // Streak ink drop
#define ANIM_CLICK_MS       80   // Click compression
#define ANIM_LINE_MS       150   // Divider line draw (slower)
#define ANIM_BREATH_PERIOD 3000  // QR breathing cycle

#define ANIM_FPS_LIMIT     30    // Max frames per second
#define ANIM_FRAME_MS      (1000 / ANIM_FPS_LIMIT)

// ============================================
// Math helpers
// ============================================

// Normalize time to 0..1 range
inline float normTime(uint32_t now, uint32_t start, uint32_t durationMs) {
  if (now <= start) return 0.0f;
  uint32_t elapsed = now - start;
  if (elapsed >= durationMs) return 1.0f;
  return (float)elapsed / (float)durationMs;
}

// Smoothstep easing (ease-in-out)
inline float smoothstep(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// Ease-out (decelerate)
inline float easeOut(float t) {
  return 1.0f - (1.0f - t) * (1.0f - t);
}

// Ease-in (accelerate)  
inline float easeIn(float t) {
  return t * t;
}

// Linear interpolation (renamed to avoid std::lerp conflict)
inline float animLerp(float a, float b, float t) {
  return a + (b - a) * t;
}

// Integer lerp
inline int animLerpInt(int a, int b, float t) {
  return a + (int)((b - a) * t);
}

// Clamp value
inline float clampf(float v, float lo, float hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Sin approximation for breathing (fast, no libm)
inline float fastSin01(float t) {
  // t in 0..1 maps to 0..2π, returns -1..1
  // Using parabola approximation
  t = t - (int)t;  // wrap to 0..1
  float x = t * 2.0f - 1.0f;  // -1..1
  return 1.0f - x * x * 2.0f;  // parabola approx
}

// ============================================
// Animation State Structure
// ============================================

struct AnimState {
  // Screen enter animation
  uint32_t enterAt = 0;
  bool enterActive = false;
  
  // Cursor smoothing (for lists/grids)
  float cursorPos = 0.0f;
  int cursorTarget = 0;
  
  // Timer snap effect
  bool snapActive = false;
  uint32_t snapAt = 0;
  uint8_t snapPhase = 0;  // 0=shrink, 1=expand, 2=done
  
  // Streak ink drop
  bool inkActive = false;
  uint32_t inkAt = 0;
  int inkIndex = -1;
  uint8_t inkStep = 0;
  
  // Click compression
  bool clickActive = false;
  uint32_t clickAt = 0;
  
  // Line draw
  bool lineActive = false;
  uint32_t lineAt = 0;
  
  // QR breathing
  uint32_t breathStartAt = 0;
  
  // FPS limiter
  uint32_t lastFrameAt = 0;
  
  // Methods
  void markEnter() {
    enterAt = millis();
    enterActive = true;
  }
  
  void triggerSnap() {
    snapActive = true;
    snapAt = millis();
    snapPhase = 0;
  }
  
  void triggerInk(int index) {
    inkActive = true;
    inkAt = millis();
    inkIndex = index;
    inkStep = 0;
  }
  
  void triggerClick() {
    clickActive = true;
    clickAt = millis();
  }
  
  void triggerLine() {
    lineActive = true;
    lineAt = millis();
  }
  
  bool shouldRender(uint32_t now) {
    if (now - lastFrameAt >= ANIM_FRAME_MS) {
      lastFrameAt = now;
      return true;
    }
    return false;
  }
  
  // Get enter animation progress (0..1, eased)
  float getEnterEase(uint32_t now) {
    if (!enterActive) return 1.0f;
    float t = normTime(now, enterAt, ANIM_ENTER_MS);
    if (t >= 1.0f) enterActive = false;
    return easeOut(t);
  }
  
  // Get enter Y offset (slides up from offset to 0)
  int getEnterYOffset(uint32_t now, int maxOffset = 6) {
    float ease = getEnterEase(now);
    return animLerpInt(maxOffset, 0, ease);
  }
  
  // Update cursor smoothing (call every frame)
  void updateCursor(float targetPos, float speed = 0.25f) {
    cursorPos += (targetPos - cursorPos) * speed;
    // Snap if very close
    if (abs(cursorPos - targetPos) < 0.5f) {
      cursorPos = targetPos;
    }
  }
  
  // Get snap radius offset
  int getSnapOffset(uint32_t now) {
    if (!snapActive) return 0;
    float t = normTime(now, snapAt, ANIM_SNAP_MS);
    if (t >= 1.0f) {
      snapActive = false;
      return 0;
    }
    // Shrink then expand
    if (t < 0.5f) {
      return -animLerpInt(0, 3, easeOut(t * 2.0f));
    } else {
      return -animLerpInt(3, 0, easeOut((t - 0.5f) * 2.0f));
    }
  }
  
  // Get ink animation step (0-3, 3=done)
  int getInkStep(uint32_t now) {
    if (!inkActive) return 3;
    float t = normTime(now, inkAt, ANIM_INK_MS);
    if (t >= 1.0f) {
      inkActive = false;
      return 3;
    }
    return (int)(t * 3.0f);
  }
  
  // Is click compression active?
  bool isClickActive(uint32_t now) {
    if (!clickActive) return false;
    if (now - clickAt >= ANIM_CLICK_MS) {
      clickActive = false;
      return false;
    }
    return true;
  }
  
  // Get line draw width (0..1)
  float getLineProgress(uint32_t now) {
    if (!lineActive) return 1.0f;
    float t = normTime(now, lineAt, ANIM_LINE_MS);
    if (t >= 1.0f) {
      lineActive = false;
      return 1.0f;
    }
    return easeOut(t);
  }
  
  // Get breathing scale (0.98..1.0)
  float getBreathScale(uint32_t now) {
    float t = (float)((now - breathStartAt) % ANIM_BREATH_PERIOD) / (float)ANIM_BREATH_PERIOD;
    return 0.99f + 0.01f * fastSin01(t);
  }
};

