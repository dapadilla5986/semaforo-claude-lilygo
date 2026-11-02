// Autor: Mr Dave · https://github.com/dapadilla5986
// Semaforo de Claude Code para LilyGo T-Display-S3 (ST7789 170x320, bus paralelo 8 bits)
// Recibe por USB-serie una linea por evento:
//   THINKING  amarillo respirando     WAITING  rojo fijo (5 min -> IDLE)
//   ERROR     rojo parpadeando        DONE     verde fijo (60 s -> IDLE)
//   IDLE      verde suave respirando
// Ajustes (se guardan en flash):  ROT  gira 180 grados | CAL <px> mueve las luces | MODE L / MODE V
//   MODE L = carcasa con 3 ventanas redondas   MODE V = carcasa con ventana rectangular (luces + texto)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Preferences.h>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel; lgfx::Bus_Parallel8 _bus; lgfx::Light_PWM _light;
public:
  LGFX() {
    { auto c = _bus.config(); c.freq_write = 20000000; c.pin_wr = 8; c.pin_rd = 9; c.pin_rs = 7;
      c.pin_d0 = 39; c.pin_d1 = 40; c.pin_d2 = 41; c.pin_d3 = 42; c.pin_d4 = 45; c.pin_d5 = 46; c.pin_d6 = 47; c.pin_d7 = 48;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config(); c.pin_cs = 6; c.pin_rst = 5; c.pin_busy = -1;
      c.memory_width = 240; c.memory_height = 320; c.panel_width = 170; c.panel_height = 320;
      c.offset_x = 35; c.offset_y = 0; c.readable = false; c.invert = true; c.rgb_order = false;
      _panel.config(c); }
    { auto c = _light.config(); c.pin_bl = 38; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

static LGFX tft;
static LGFX_Sprite spr(&tft);
static Preferences prefs;

enum State { IDLE, THINKING, WAITING, ERR, DONE };
static State state = IDLE;
static uint32_t stateSince = 0;
static uint8_t rot = 0; static int16_t cal = 0; static char mode = 'L';

// geometria: 7.49 px/mm. Ventanas de 11 mm separadas 13.5 mm -> 101 px
static const int W = 170, H = 320, CX = 85;

struct RGB { uint8_t r, g, b; };
static const RGB RED = {255, 20, 10}, YEL = {255, 215, 0}, GRN = {20, 255, 70};

static uint16_t mix(RGB c, float k) {   // k 0..1 brillo
  k = constrain(k, 0.f, 1.f);
  return spr.color565(c.r * k, c.g * k, c.b * k);
}

static uint16_t mixw(RGB c, float k, float w) {   // brillo k y mezcla con blanco w
  float r = c.r * k, g = c.g * k, b = c.b * k;
  return spr.color565(r + (255 - r) * w, g + (255 - g) * w, b + (255 - b) * w);
}

static void lamp(int cy, int R, RGB c, float level) {
  level = constrain(level, 0.f, 1.f);
  if (level < 0.03f) {                              // apagada: lente muy oscura
    spr.fillCircle(CX, cy, R, mix(c, 0.07f));
    spr.drawCircle(CX, cy, R, mix(c, 0.14f));
    return;
  }
  // encendida: color lleno y brillante, borde apenas mas oscuro, centro casi blanco
  for (int r = R; r > 0; r -= 2) {
    float t = 1.f - (float)r / R;                  // 0 borde -> 1 centro
    float k = level * (t < 0.12f ? 0.80f + 1.6f * t : 1.f);
    float w = t > 0.45f ? (t - 0.45f) / 0.55f * 0.75f * level : 0.f;
    spr.fillCircle(CX, cy, r, mixw(c, k, w));
  }
  spr.fillCircle(CX - R / 3, cy - R / 3, R / 6, spr.color565(255, 255, 255));   // reflejo
}

static const char* label() {
  switch (state) { case THINKING: return "PENSANDO"; case WAITING: return "TE ESPERA";
    case ERR: return "ERROR"; case DONE: return "LISTO"; default: return "LIBRE"; }
}

static void draw() {
  uint32_t t = millis(), age = t - stateSince;
  float breathe = 0.5f - 0.5f * cosf(t / 1000.f * 2 * PI / 1.6f);     // 1.6 s
  float slow = 0.5f - 0.5f * cosf(t / 1000.f * 2 * PI / 3.5f);
  float r = 0, y = 0, g = 0;
  switch (state) {
    case THINKING: y = 0.45f + 0.55f * breathe; break;
    case WAITING:  r = 1; break;
    case ERR:      r = ((t / 250) % 2) ? 1 : 0.f; break;
    case DONE:     g = 1; break;
    case IDLE:     g = 0.35f + 0.35f * slow; break;
  }
  spr.fillScreen(TFT_BLACK);
  int sp, R, c0;
  if (mode == 'V') { sp = 84; R = 38; c0 = 128 + cal; } else { sp = 101; R = 52; c0 = 160 + cal; }
  lamp(c0 - sp, R, RED, r);
  lamp(c0, R, YEL, y);
  lamp(c0 + sp, R, GRN, g);
  if (mode == 'V') {
    spr.setTextDatum(textdatum_t::middle_center);
    spr.setFont(&fonts::Font4);
    RGB c = state == WAITING || state == ERR ? RED : state == THINKING ? YEL : GRN;
    spr.setTextColor(mix(c, 1));
    spr.drawString(label(), CX, 296);
  }
  spr.pushSprite(0, 0);
}

static void setState(State s) { state = s; stateSince = millis(); }

static void command(String l) {
  l.trim(); String u = l; u.toUpperCase();
  if (u == "THINKING") setState(THINKING);
  else if (u == "WAITING" || u == "WAITING_USER") setState(WAITING);
  else if (u == "ERROR" || u == "TOOL_ERROR") setState(ERR);
  else if (u == "DONE" || u == "TASK_COMPLETE") setState(DONE);
  else if (u == "IDLE") setState(IDLE);
  else if (u == "ROT") { rot = rot ? 0 : 2; tft.setRotation(rot); prefs.putUChar("rot", rot); }
  else if (u.startsWith("CAL")) { cal = u.substring(3).toInt(); prefs.putShort("cal", cal); }
  else if (u == "MODE L" || u == "MODE V") { mode = u[5]; prefs.putChar("mode", mode); }
  else if (u == "PING") {}
  else { Serial.println("? " + l); return; }
  Serial.printf("OK %s rot=%d cal=%d mode=%c\n", label(), rot, cal, mode);
}

void setup() {
  pinMode(15, OUTPUT); digitalWrite(15, HIGH);       // alimentacion de la pantalla con bateria
  Serial.begin(115200);
  prefs.begin("semaforo", false);
  rot = prefs.getUChar("rot", 0); cal = prefs.getShort("cal", 0); mode = prefs.getChar("mode", 'L');
  tft.init(); tft.setRotation(rot); tft.setBrightness(255); tft.fillScreen(TFT_BLACK);
  spr.setColorDepth(16); spr.setPsram(true);
  if (!spr.createSprite(W, H)) { spr.setPsram(false); spr.createSprite(W, H); }
  setState(IDLE);
}

void loop() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (buf.length()) command(buf); buf = ""; }
    else if (buf.length() < 40) buf += c;
  }
  uint32_t age = millis() - stateSince;
  if (state == DONE && age > 60000) setState(IDLE);
  if (state == WAITING && age > 300000) setState(IDLE);
  static uint32_t last = 0;
  if (millis() - last >= 33) { last = millis(); draw(); }
}
