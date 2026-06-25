/*
 * MINI CERVEJEIRA - v6.0
 * Loop não-bloqueante | sem delay() | debounce por borda
 *
 * PINAGEM:
 *   BTN_BOMBA      → 2   BTN_PROCESSO → 3
 *   RELE_AQUECEDOR → 5   RELE_BOMBA   → 6
 *   DS18B20        → 12
 *
 * RELE_BOMBA: LOW = ligado, HIGH = desligado
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── Pinos ────────────────────────────────────────────────────
#define BTN_BOMBA       2
#define BTN_PROCESSO    3
#define RELE_AQUECEDOR  5
#define RELE_BOMBA      6
#define PINO_SENS_TEMP  12

// ── Configurações ────────────────────────────────────────────
const float         TEMP_ALVO        = 65.0;
const float         TEMP_HISTERESE   =  0.5;
const unsigned long DEBOUNCE_MS      =  200;
const unsigned long INTERVALO_SENSOR = 5000;
const unsigned long DURACAO_P1       = 120000UL;
const unsigned long INTERVALO_LCD    =  200;

// ── Objetos ──────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 20, 4);
OneWire           oneWire(PINO_SENS_TEMP);
DallasTemperature sensors(&oneWire);
DeviceAddress     enderecoTemp;

// ── Estado ───────────────────────────────────────────────────
uint8_t       processoAtual   = 1;
bool          processoRodando = false;
bool          bombaLigada     = false;
bool          aquecedorLigado = false;
bool          sensorOK        = false;
float         temperatura     = 0.0;
unsigned long tempoDecorrido  = 0;
unsigned long tInicio         = 0;

// ── Timestamps ───────────────────────────────────────────────
unsigned long tSensor = 0, tLCD = 0;
unsigned long tDebBomba = 0, tDebProc = 0;
bool          antBomba = HIGH, antProc = HIGH;

// ── Símbolo ° ────────────────────────────────────────────────
byte grau[8] = { 0b00110,0b01001,0b01001,0b00110,0,0,0,0 };

// ── Auxiliares LCD ───────────────────────────────────────────
// Imprime str preenchendo com espaços até 'n' colunas
void lcdFixo(const char* str, uint8_t n) {
  uint8_t i = 0;
  while (str[i] && i < n) lcd.print(str[i++]);
  while (i++ < n)         lcd.print(' ');
}

// Imprime MM:SS
void lcdTimer(unsigned long ms, bool restante) {
  if (restante) ms = (ms < DURACAO_P1) ? DURACAO_P1 - ms : 0;
  uint8_t m = (ms / 60000) % 60, s = (ms / 1000) % 60;
  if (m < 10) lcd.print('0'); lcd.print(m);
  lcd.print(':');
  if (s < 10) lcd.print('0'); lcd.print(s);
}

// ── SUPERVISÓRIO NODE-RED ─────────────────────────────────────
// Adicione esta variável junto dos outros timestamps:
// unsigned long tSerial = 0;
//
// No loop(), chame: enviarJSON(agora);  receberComando(agora);
// ─────────────────────────────────────────────────────────────

void enviarJSON(unsigned long agora) {
  static unsigned long tSerial = 0;
  if (agora - tSerial < 1000) return;
  tSerial = agora;

  unsigned long dec = tempoDecorrido + (processoRodando ? agora - tInicio : 0);

  Serial.print(F("{\"temp\":"));   Serial.print(temperatura, 1);
  Serial.print(F(",\"proc\":"));   Serial.print(processoAtual);
  Serial.print(F(",\"rodando\":")); Serial.print(processoRodando ? 1 : 0);
  Serial.print(F(",\"bomba\":"));  Serial.print(bombaLigada ? 1 : 0);
  Serial.print(F(",\"aquec\":"));  Serial.print(aquecedorLigado ? 1 : 0);
  Serial.print(F(",\"dec\":"));    Serial.print(dec);
  Serial.println(F("}"));
}

void receberComando(unsigned long agora) {
  if (!Serial.available()) return;
  char cmd = Serial.read();

  if (cmd == 'P') {                          // toggle processo
    if (!processoRodando) {
      bombaLigada = false; processoRodando = true; tInicio = agora;
    } else {
      tempoDecorrido += agora - tInicio; processoRodando = false;
    }
  }
  if (cmd == 'B' && !processoRodando)        // toggle bomba (só parado)
    bombaLigada = !bombaLigada;
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  pinMode(BTN_BOMBA,      INPUT_PULLUP);
  pinMode(BTN_PROCESSO,   INPUT_PULLUP);
  pinMode(RELE_AQUECEDOR, OUTPUT);
  pinMode(RELE_BOMBA,     OUTPUT);
  digitalWrite(RELE_AQUECEDOR, LOW);
  digitalWrite(RELE_BOMBA,     HIGH);

  lcd.init(); lcd.backlight(); lcd.createChar(0, grau);
  lcd.setCursor(0, 0); lcd.print(F("--- MINI CERVEJEIRA -"));

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  tSensor = millis();
}

// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long agora = millis();

  // ── Sensor (1s) ──────────────────────────────────────────
  if (agora - tSensor >= INTERVALO_SENSOR) {
    tSensor = agora;
    sensorOK    = sensors.getAddress(enderecoTemp, 0);
    temperatura = sensorOK ? sensors.getTempC(enderecoTemp) : temperatura;
    sensors.requestTemperatures();
  }

  // ── Botão BOMBA (só com processo parado) ─────────────────
  bool lB = digitalRead(BTN_BOMBA);
  if (antBomba == HIGH && lB == LOW && agora - tDebBomba >= DEBOUNCE_MS) {
    tDebBomba = agora;
    if (!processoRodando) bombaLigada = !bombaLigada;
  }
  antBomba = lB;

  // ── Botão PROCESSO (inicia/pausa, retoma de onde parou) ──
  bool lP = digitalRead(BTN_PROCESSO);
  if (antProc == HIGH && lP == LOW && agora - tDebProc >= DEBOUNCE_MS) {
    tDebProc = agora;
    if (!processoRodando) {
      bombaLigada = false;
      processoRodando = true;
      tInicio = agora;
    } else {
      tempoDecorrido += agora - tInicio;
      processoRodando = false;
    }
  }
  antProc = lP;

  // ── Fim automático do Processo 1 (2 min) ─────────────────
  if (processoRodando) {
    unsigned long total = tempoDecorrido + (agora - tInicio);
    if (processoAtual == 1 && total >= DURACAO_P1) {
      tempoDecorrido  = 0;
      processoRodando = false;
      processoAtual   = 2;
    }
  }

  // ── Aquecedor com histerese (Processo 1 rodando) ─────────
  if (sensorOK && processoAtual == 1 && processoRodando) {
    if (!aquecedorLigado && temperatura < TEMP_ALVO - TEMP_HISTERESE)
      digitalWrite(RELE_AQUECEDOR, HIGH), aquecedorLigado = true;
    else if (aquecedorLigado && temperatura >= TEMP_ALVO)
      digitalWrite(RELE_AQUECEDOR, LOW),  aquecedorLigado = false;
  } else if (aquecedorLigado) {
    digitalWrite(RELE_AQUECEDOR, LOW); aquecedorLigado = false;
  }

  // ── Bomba ────────────────────────────────────────────────
  if (processoRodando) bombaLigada = false;
  digitalWrite(RELE_BOMBA, bombaLigada ? LOW : HIGH);

  // ── LCD (200ms) ──────────────────────────────────────────
  // Linha 0 fixo. Linhas 1-3 redesenhadas completas (sem artefatos).
  // Cada linha ocupa exatamente 20 colunas.
  if (agora - tLCD >= INTERVALO_LCD) {
    tLCD = agora;

    unsigned long dec = tempoDecorrido + (processoRodando ? agora - tInicio : 0);

    // Linha 1: "Temp:  65.0°C AQUEC "  (20 cols)
    lcd.setCursor(0, 1); lcd.print(F("Temp: "));
    if (sensorOK) {
      char buf[7]; dtostrf(temperatura, 5, 1, buf);
      lcd.print(buf); lcd.write(byte(0)); lcd.print('C');
      lcd.print(aquecedorLigado ? F(" AQUEC ") : F("       "));
    }

    // Linha 2: "Bomba: DESLIGADA    "  (20 cols)
    lcd.setCursor(0, 2); lcd.print(F("Bomba: "));
    lcdFixo(bombaLigada ? "LIGADA" : "DESLIGADA", 13);

    // Linha 3: "Proc: P1 RUN  02:00 "  (20 cols)
    // 6 + 2 + 1 + 4 + 1 + 5 + 1 = 20
    lcd.setCursor(0, 3); lcd.print(F("Proc: P"));
    lcd.print(processoAtual); lcd.print(' ');
    lcd.print(processoRodando ? F("RUN ") : F("STOP"));
    lcd.print(' ');
    lcdTimer(dec, processoAtual == 1);
    lcd.print(' ');
  }

  enviarJSON(agora);  
  receberComando(agora);
}
