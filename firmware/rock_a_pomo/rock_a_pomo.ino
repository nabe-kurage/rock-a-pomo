// Rock-a-Pomo firmware
// Set TEST_MODE to false for 25-minute focus sessions and 5-minute breaks.

#include <M5StickCPlus2.h>
#include <math.h>

// ポモドーロの状態。
// SLEEPING: 作業中 / CRYING: 作業終了通知 / SOOTHING: あやし中 / LAUGHING: 休憩中
enum PomodoroState {
  SLEEPING,
  CRYING,
  SOOTHING,
  LAUGHING,
};

// ===== タイマーとあやし判定の設定 =====
const bool TEST_MODE = true;
const unsigned long FOCUS_MS = TEST_MODE ? 10UL * 1000UL : 25UL * 60UL * 1000UL;
const unsigned long REST_MS = TEST_MODE ? 15UL * 1000UL : 5UL * 60UL * 1000UL;
const unsigned long CRY_BEEP_INTERVAL_MS = 850;

// 加速度の変化量がこの範囲に入ったときだけ「優しく揺らした」とみなす。
const float GENTLE_SWING_DEV_MIN = 0.10f;
const float GENTLE_SWING_DEV_MAX = 0.35f;

// 揺れの間隔が短すぎても長すぎても、あやしとしてはカウントしない。
const unsigned long GENTLE_SWING_MIN_INTERVAL_MS = 300;
const unsigned long GENTLE_SWING_MAX_INTERVAL_MS = 1100;

// あやし量。SOOTHING_GOALまでたまると休憩に入る。
const int SOOTHING_GOAL = 45;
const int SOOTHING_DECAY_INTERVAL_MS = 220;

// 画面描画で使う色。
const uint16_t LIGHT_GREY = 0xC618;
const uint16_t DARK_GREY = 0x7BEF;
const uint16_t CHEEK_PINK = 0xF8B2;

// ===== 現在の状態を覚えておく変数 =====
PomodoroState state = SLEEPING;
unsigned long stateStartedAt = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastSoothingDecay = 0;
unsigned long lastGentleSwingAt = 0;
unsigned long lastCryBeep = 0;
int soothingAmount = 0;
bool needsRedraw = true;
bool paused = false;
unsigned long pausedStartedAt = 0;

// ===== 小さな描画部品 =====
void drawProgressBarOnWhite(int value, int goal) {
  auto &d = StickCP2.Display;
  int x = 44;
  int y = 120;
  int w = 152;
  int h = 8;
  int fillW = constrain((value * w) / goal, 0, w);

  d.drawRect(x, y, w, h, DARK_GREY);
  d.fillRect(x + 1, y + 1, w - 2, h - 2, WHITE);
  d.fillRect(x + 1, y + 1, fillW - 2 > 0 ? fillW - 2 : 0, h - 2, GREEN);
}

// ミリ秒を画面表示用の残り時間テキストに変換する。
String formatRemainingTime(unsigned long remainingMs) {
  unsigned long totalSec = (remainingMs + 999) / 1000;

  if (totalSec < 60) {
    return String(totalSec) + "s";
  }

  unsigned long minutes = totalSec / 60;
  unsigned long seconds = totalSec % 60;
  String secondsText = seconds < 10 ? "0" + String(seconds) : String(seconds);
  return String(minutes) + "m " + secondsText + "s";
}

// ===== 表情の描画 =====
void drawSleepingFace(unsigned long remainingMs) {
  auto &d = StickCP2.Display;
  d.fillScreen(WHITE);

  d.setTextDatum(top_center);
  d.setTextColor(LIGHT_GREY);
  d.setTextSize(1);
  d.drawString("SLEEPING", 120, 6);

  // 画像の寝顔に寄せた、太めのU字まぶた。
  d.drawArc(61, 52, 29, 29, 0, 180, BLACK);
  d.drawArc(62, 52, 29, 29, 0, 180, BLACK);
  d.drawArc(63, 52, 29, 29, 0, 180, BLACK);
  d.drawArc(177, 52, 29, 29, 0, 180, BLACK);
  d.drawArc(178, 52, 29, 29, 0, 180, BLACK);
  d.drawArc(179, 52, 29, 29, 0, 180, BLACK);

  d.fillEllipse(120, 79, 16, 8, BLACK);
  d.fillCircle(20, 94, 15, CHEEK_PINK);
  d.fillCircle(220, 94, 15, CHEEK_PINK);

  d.setTextDatum(bottom_center);
  d.setTextColor(DARK_GREY);
  d.setTextSize(2);
  d.drawString(formatRemainingTime(remainingMs), 120, 132);
}

void drawCryingFace() {
  auto &d = StickCP2.Display;
  d.fillScreen(WHITE);

  d.setTextDatum(top_center);
  d.setTextColor(LIGHT_GREY);
  d.setTextSize(1);
  d.drawString("CRYING", 120, 6);

  // 画像の泣き顔に寄せた、ぎゅっと閉じた目。
  d.drawLine(43, 40, 95, 70, BLACK);
  d.drawLine(43, 41, 95, 71, BLACK);
  d.drawLine(43, 42, 95, 72, BLACK);
  d.drawLine(41, 69, 95, 69, BLACK);
  d.drawLine(41, 70, 95, 70, BLACK);
  d.drawLine(41, 71, 95, 71, BLACK);
  d.drawLine(43, 100, 95, 70, BLACK);
  d.drawLine(43, 101, 95, 71, BLACK);
  d.drawLine(43, 102, 95, 72, BLACK);

  d.drawLine(197, 40, 145, 70, BLACK);
  d.drawLine(197, 41, 145, 71, BLACK);
  d.drawLine(197, 42, 145, 72, BLACK);
  d.drawLine(199, 69, 145, 69, BLACK);
  d.drawLine(199, 70, 145, 70, BLACK);
  d.drawLine(199, 71, 145, 71, BLACK);
  d.drawLine(197, 100, 145, 70, BLACK);
  d.drawLine(197, 101, 145, 71, BLACK);
  d.drawLine(197, 102, 145, 72, BLACK);

  d.fillEllipse(120, 83, 16, 8, BLACK);
  d.fillCircle(20, 96, 15, CHEEK_PINK);
  d.fillCircle(220, 96, 15, CHEEK_PINK);
}

void drawSoothingFace() {
  auto &d = StickCP2.Display;
  d.fillScreen(WHITE);

  d.setTextDatum(top_center);
  d.setTextColor(LIGHT_GREY);
  d.setTextSize(1);
  d.drawString("SOOTHING", 120, 6);

  // 泣き止んで笑顔になる途中の「間の顔」。
  d.drawLine(28, 54, 86, 40, BLACK);
  d.drawLine(28, 55, 86, 41, BLACK);
  d.drawLine(28, 56, 86, 42, BLACK);
  d.drawLine(154, 40, 212, 54, BLACK);
  d.drawLine(154, 41, 212, 55, BLACK);
  d.drawLine(154, 42, 212, 56, BLACK);

  d.drawArc(120, 78, 18, 14, 200, 340, BLACK);
  d.drawArc(120, 79, 18, 14, 200, 340, BLACK);
  d.drawArc(120, 80, 18, 14, 200, 340, BLACK);
  d.fillCircle(20, 96, 15, CHEEK_PINK);
  d.fillCircle(220, 96, 15, CHEEK_PINK);

  drawProgressBarOnWhite(soothingAmount, SOOTHING_GOAL);
}

void drawLaughingFace(unsigned long remainingMs) {
  auto &d = StickCP2.Display;
  d.fillScreen(WHITE);

  d.setTextDatum(top_center);
  d.setTextColor(LIGHT_GREY);
  d.setTextSize(1);
  d.drawString("LAUGHING", 120, 6);

  // 休憩中の笑顔。目は大きな山型、口は小さな横長の楕円にする。
  d.drawArc(63, 78, 29, 36, 180, 360, BLACK);
  d.drawArc(64, 78, 29, 36, 180, 360, BLACK);
  d.drawArc(65, 78, 29, 36, 180, 360, BLACK);
  d.drawArc(175, 78, 29, 36, 180, 360, BLACK);
  d.drawArc(176, 78, 29, 36, 180, 360, BLACK);
  d.drawArc(177, 78, 29, 36, 180, 360, BLACK);

  d.fillEllipse(120, 83, 16, 8, BLACK);
  d.fillCircle(20, 96, 15, CHEEK_PINK);
  d.fillCircle(220, 96, 15, CHEEK_PINK);

  d.setTextDatum(bottom_center);
  d.setTextColor(DARK_GREY);
  d.setTextSize(2);
  d.drawString(formatRemainingTime(remainingMs), 120, 132);
}

void drawCurrentScreen() {
  // ボタンAで一時停止中。タイマー進行も揺れ判定も止める。
  if (paused) {
    auto &d = StickCP2.Display;
    d.fillScreen(BLACK);
    d.setTextDatum(middle_center);
    d.setTextColor(YELLOW);
    d.setTextSize(3);
    d.drawString("PAUSED", 120, 62);
    d.setTextSize(1);
    d.drawString("BtnA resume", 120, 100);
    needsRedraw = false;
    return;
  }

  unsigned long elapsed = millis() - stateStartedAt;
  unsigned long focusRemainingMs = elapsed >= FOCUS_MS ? 0 : FOCUS_MS - elapsed;
  unsigned long restRemainingMs = elapsed >= REST_MS ? 0 : REST_MS - elapsed;

  // 現在の状態に対応する画面だけを描画する。
  if (state == SLEEPING) drawSleepingFace(focusRemainingMs);
  if (state == CRYING) drawCryingFace();
  if (state == SOOTHING) drawSoothingFace();
  if (state == LAUGHING) drawLaughingFace(restRemainingMs);

  needsRedraw = false;
}

// ===== 音 =====
void playCryBeep() {
  auto &spk = StickCP2.Speaker;
  spk.tone(3200, 70);
  delay(75);
  spk.tone(2600, 70);
}

void playFocusStartBeep() {
  auto &spk = StickCP2.Speaker;
  spk.tone(2400, 90);
}

// 状態を切り替えるときは、タイマーや画面更新フラグもここでまとめて初期化する。
void changeState(PomodoroState nextState) {
  state = nextState;
  stateStartedAt = millis();
  lastDisplayUpdate = 0;
  lastSoothingDecay = millis();
  lastGentleSwingAt = 0;
  lastCryBeep = millis();
  needsRedraw = true;

  if (state == SLEEPING) {
    soothingAmount = 0;
  }

  if (state == LAUGHING) {
    soothingAmount = SOOTHING_GOAL;
  }

  if (state == CRYING) {
    playCryBeep();
  }
}

// ===== IMUによる揺れ検出 =====
float readAccelDeviation() {
  float ax, ay, az;
  StickCP2.Imu.update();
  StickCP2.Imu.getAccelData(&ax, &ay, &az);

  // 静止していると加速度の大きさは約1G。そこからのズレを揺れの強さとして使う。
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  return fabsf(mag - 1.0f);
}

bool isGentleSwing(float dev, unsigned long now) {
  bool isGentleStrength = dev >= GENTLE_SWING_DEV_MIN && dev <= GENTLE_SWING_DEV_MAX;
  bool isTooStrong = dev > GENTLE_SWING_DEV_MAX;
  bool isFirstGentleSwing = lastGentleSwingAt == 0;
  unsigned long swingInterval = isFirstGentleSwing ? 0 : now - lastGentleSwingAt;
  bool isGoodRhythm = isFirstGentleSwing ||
                      (swingInterval >= GENTLE_SWING_MIN_INTERVAL_MS &&
                       swingInterval <= GENTLE_SWING_MAX_INTERVAL_MS);

  // 強すぎる動きは「乱暴に振った」とみなし、リズム判定をリセットする。
  if (isTooStrong) {
    lastGentleSwingAt = 0;
    return false;
  }

  // 強さとリズムの両方がよければ、あやし成功としてカウントする。
  if (isGentleStrength && isGoodRhythm) {
    lastGentleSwingAt = now;
    return true;
  }

  // 前回から時間が空きすぎた場合は、ここから新しい揺れ始めとして扱う。
  if (isGentleStrength && !isGoodRhythm && swingInterval > GENTLE_SWING_MAX_INTERVAL_MS) {
    lastGentleSwingAt = now;
  }

  return false;
}

void updateSoothing(float dev) {
  unsigned long now = millis();
  bool gentleSwing = (state == CRYING || state == SOOTHING) && isGentleSwing(dev, now);
  bool isTooWeak = dev < GENTLE_SWING_DEV_MIN;
  bool isTooStrong = dev > GENTLE_SWING_DEV_MAX;

  if (gentleSwing) {
    if (state == CRYING) {
      // 泣いている状態で優しい揺れを検出したら、あやし中へ移る。
      state = SOOTHING;
      stateStartedAt = now;
      needsRedraw = true;
    }

    soothingAmount = min(soothingAmount + 1, SOOTHING_GOAL);
    needsRedraw = true;
  }

  // あやしが止まったり強すぎたりすると、少しずつあやし量を減らす。
  if (state == SOOTHING && now - lastSoothingDecay >= SOOTHING_DECAY_INTERVAL_MS) {
    lastSoothingDecay = now;
    if ((isTooWeak || isTooStrong) && soothingAmount > 0) {
      soothingAmount--;
      needsRedraw = true;
    }
  }

  // あやし量がなくなったら泣き直し、満タンになったら休憩へ入る。
  if (state == SOOTHING && soothingAmount <= 0) {
    changeState(CRYING);
  }

  if (state == SOOTHING && soothingAmount >= SOOTHING_GOAL) {
    changeState(LAUGHING);
  }
}

void setup() {
  // M5StickC Plus2の画面、スピーカー、状態を初期化する。
  auto cfg = M5.config();
  StickCP2.begin(cfg);
  StickCP2.Display.setRotation(1);
  StickCP2.Speaker.setVolume(70);
  changeState(SLEEPING);
}

void togglePause(unsigned long now) {
  paused = !paused;

  if (paused) {
    pausedStartedAt = now;
    needsRedraw = true;
    return;
  }

  // 一時停止していた時間ぶん、状態開始時刻を後ろへずらしてタイマーを止めた扱いにする。
  unsigned long pausedMs = now - pausedStartedAt;
  stateStartedAt += pausedMs;
  lastDisplayUpdate = 0;
  lastSoothingDecay += pausedMs;
  lastGentleSwingAt = 0;
  lastCryBeep = now;
  needsRedraw = true;
}

void loop() {
  StickCP2.update();
  unsigned long now = millis();

  // ボタンAはデモや調整中に使う一時停止。
  if (StickCP2.BtnA.wasPressed()) {
    togglePause(now);
  }

  if (paused) {
    if (needsRedraw) {
      drawCurrentScreen();
    }
    delay(20);
    return;
  }

  // 作業時間が終わったら泣き始める。作業中は1秒ごとに残り時間を更新する。
  if (state == SLEEPING) {
    unsigned long elapsed = now - stateStartedAt;
    if (elapsed >= FOCUS_MS) {
      changeState(CRYING);
    } else if (now - lastDisplayUpdate >= 1000 || lastDisplayUpdate == 0) {
      lastDisplayUpdate = now;
      needsRedraw = true;
    }
  }

  // 休憩時間が終わったら、短い音を鳴らして次の作業時間へ戻る。
  if (state == LAUGHING) {
    unsigned long elapsed = now - stateStartedAt;
    if (elapsed >= REST_MS) {
      playFocusStartBeep();
      changeState(SLEEPING);
    } else if (now - lastDisplayUpdate >= 1000 || lastDisplayUpdate == 0) {
      lastDisplayUpdate = now;
      needsRedraw = true;
    }
  }

  // 泣いている間は一定間隔で泣き声を鳴らす。
  if (state == CRYING && now - lastCryBeep >= CRY_BEEP_INTERVAL_MS) {
    lastCryBeep = now;
    playCryBeep();
  }

  // 毎ループIMUを読み、必要ならあやし量と状態を更新する。
  float dev = readAccelDeviation();
  updateSoothing(dev);

  if (needsRedraw) {
    drawCurrentScreen();
  }

  delay(20);
}
