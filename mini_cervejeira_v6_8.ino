/*
 * MINI CERVEJEIRA - v6.8  (receitas multi-etapas via serial)
 * Loop nao-bloqueante | sem delay() | debounce por borda
 *
 * Estende a v6.7 (ISOLADO - se der problema, regrave o v6_7):
 *  - Recebe receitas pela serial:  R<temp>,<min>;<temp>,<min>;...   (ex.: R52,15;63,40;78,5)
 *  - Executa mostura em MULTIPLAS etapas (rampa -> atinge -> conta o tempo -> proxima -> fim).
 *  - SEM receita carregada: usa o padrao 65 C / 2 min (retrocompativel com a v6_7).
 *  - Mantem da v6.7: a bomba pode ligar DURANTE o processo; o tempo so conta APOS atingir a temp.
 *
 * PINAGEM:
 *   BTN_BOMBA      -> 2   BTN_PROCESSO -> 3
 *   RELE_AQUECEDOR -> 5   RELE_BOMBA   -> 6
 *   DS18B20        -> 12
 *
 * RELE_BOMBA: LOW = ligado, HIGH = desligado
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// -- Pinos -----------------------------------------------------
#define BTN_BOMBA       2
#define BTN_PROCESSO    3
#define RELE_AQUECEDOR  5
#define RELE_BOMBA      6
#define PINO_SENS_TEMP  12

// -- Configuracoes ---------------------------------------------
const float         TEMP_HISTERESE   =  0.5;
const unsigned long DEBOUNCE_MS      =  200;
const unsigned long INTERVALO_SENSOR = 5000;
const unsigned long INTERVALO_LCD    =  200;

// Receita padrao (usada ate chegar uma receita pela serial)
const float         TEMP_PADRAO      = 65.0;
const unsigned long DUR_PADRAO_MS    = 120000UL;   // 2 min

// -- Receita (etapas) ------------------------------------------
#define MAX_ETAPAS 8
struct Etapa { float temp; unsigned long durMs; };
Etapa    receita[MAX_ETAPAS];
uint8_t  numEtapas  = 1;
uint8_t  etapaAtual = 0;

// -- Objetos ---------------------------------------------------
LiquidCrystal_I2C lcd(0x27, 20, 4);
OneWire           oneWire(PINO_SENS_TEMP);
DallasTemperature sensors(&oneWire);
DeviceAddress     enderecoTemp;

// -- Estado ----------------------------------------------------
bool          processoRodando = false;
bool          bombaLigada     = false;
bool          aquecedorLigado = false;
bool          sensorOK        = false;
bool          tempAtingida    = false;   // a temperatura da etapa ja foi atingida?
bool          concluido       = false;   // a receita terminou?
float         temperatura     = 0.0;
unsigned long tempoDecorrido  = 0;
unsigned long tInicio         = 0;

// -- Timestamps ------------------------------------------------
unsigned long tSensor = 0, tLCD = 0;
unsigned long tDebBomba = 0, tDebProc = 0;
bool          antBomba = HIGH, antProc = HIGH;

// -- Simbolo grau ----------------------------------------------
byte grau[8] = { 0b00110,0b01001,0b01001,0b00110,0,0,0,0 };

// -- Auxiliares LCD --------------------------------------------
void lcdFixo(const char* str, uint8_t n) {
  uint8_t i = 0;
  while (str[i] && i < n) lcd.print(str[i++]);
  while (i++ < n)         lcd.print(' ');
}

// Imprime MM:SS (tempo decorrido)
void lcdTimer(unsigned long ms) {
  uint8_t m = (ms / 60000) % 60, s = (ms / 1000) % 60;
  if (m < 10) lcd.print('0'); lcd.print(m);
  lcd.print(':');
  if (s < 10) lcd.print('0'); lcd.print(s);
}

// -- Receita padrao --------------------------------------------
void receitaPadrao() {
  receita[0].temp  = TEMP_PADRAO;
  receita[0].durMs = DUR_PADRAO_MS;
  numEtapas = 1;
}

// -- Reinicia o processo (etapa 0) -----------------------------
void reiniciarProcesso() {
  etapaAtual     = 0;
  tempAtingida   = false;
  tempoDecorrido = 0;
  concluido      = false;
}

// -- Supervisorio: envia estado em JSON ------------------------
void enviarJSON(unsigned long agora) {
  static unsigned long tSerial = 0;
  if (agora - tSerial < 1000) return;
  tSerial = agora;

  unsigned long dec = tempAtingida ? (tempoDecorrido + (processoRodando ? agora - tInicio : 0)) : 0;

  Serial.print(F("{\"temp\":"));        Serial.print(temperatura, 1);
  Serial.print(F(",\"proc\":"));        Serial.print(etapaAtual + 1);
  Serial.print(F(",\"rodando\":"));     Serial.print(processoRodando ? 1 : 0);
  Serial.print(F(",\"bomba\":"));       Serial.print(bombaLigada ? 1 : 0);
  Serial.print(F(",\"aquec\":"));       Serial.print(aquecedorLigado ? 1 : 0);
  Serial.print(F(",\"dec\":"));         Serial.print(dec);
  Serial.print(F(",\"setpoint\":"));    Serial.print(receita[etapaAtual].temp, 1);
  Serial.print(F(",\"etapaTotal\":"));  Serial.print(numEtapas);
  Serial.print(F(",\"etapaDurSeg\":")); Serial.print(receita[etapaAtual].durMs / 1000);
  Serial.print(F(",\"concluido\":"));   Serial.print(concluido ? 1 : 0);
  Serial.println(F("}"));
}

// -- Carrega receita:  R52,15;63,40;78,5 -----------------------
void carregarReceita() {
  char buf[96];
  uint8_t n = Serial.readBytesUntil('\n', buf, sizeof(buf) - 1);
  buf[n] = '\0';

  uint8_t et = 0;
  char* etapaStr = strtok(buf, ";");
  while (etapaStr != NULL && et < MAX_ETAPAS) {
    char* virg = strchr(etapaStr, ',');
    if (virg != NULL) {
      *virg = '\0';
      float t = atof(etapaStr);
      long  m = atol(virg + 1);
      if (t > 0 && m > 0) {
        receita[et].temp  = t;
        receita[et].durMs = (unsigned long) m * 60000UL;
        et++;
      }
    }
    etapaStr = strtok(NULL, ";");
  }
  if (et > 0) {                  // recebeu pelo menos 1 etapa valida
    numEtapas = et;
    processoRodando = false;     // seguranca: nova receita nao inicia sozinha
    reiniciarProcesso();
  }
}

void receberComando(unsigned long agora) {
  if (!Serial.available()) return;
  char cmd = Serial.read();

  if (cmd == 'P') {                          // inicia / pausa
    if (!processoRodando) {
      if (concluido) reiniciarProcesso();    // recomeca do zero apos terminar
      bombaLigada = false; processoRodando = true; tInicio = agora;
    } else {
      if (tempAtingida) tempoDecorrido += agora - tInicio;
      processoRodando = false;
    }
  }
  else if (cmd == 'B') {                      // bomba (pode durante o processo)
    bombaLigada = !bombaLigada;
  }
  else if (cmd == 'R') {                      // carrega receita multi-etapas
    carregarReceita();
  }
}

// -------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  Serial.setTimeout(250);                    // p/ readBytesUntil nao travar muito
  pinMode(BTN_BOMBA,      INPUT_PULLUP);
  pinMode(BTN_PROCESSO,   INPUT_PULLUP);
  pinMode(RELE_AQUECEDOR, OUTPUT);
  pinMode(RELE_BOMBA,     OUTPUT);
  digitalWrite(RELE_AQUECEDOR, LOW);
  digitalWrite(RELE_BOMBA,     HIGH);

  receitaPadrao();

  lcd.init(); lcd.backlight(); lcd.createChar(0, grau);
  lcd.setCursor(0, 0); lcd.print(F("--- MINI CERVEJEIRA -"));

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  tSensor = millis();
}

// -------------------------------------------------------------
void loop() {
  unsigned long agora = millis();

  // -- Sensor (5s) -----------------------------------------
  if (agora - tSensor >= INTERVALO_SENSOR) {
    tSensor = agora;
    sensorOK    = sensors.getAddress(enderecoTemp, 0);
    temperatura = sensorOK ? sensors.getTempC(enderecoTemp) : temperatura;
    sensors.requestTemperatures();
  }

  // -- Botao BOMBA (pode durante o processo) ---------------
  bool lB = digitalRead(BTN_BOMBA);
  if (antBomba == HIGH && lB == LOW && agora - tDebBomba >= DEBOUNCE_MS) {
    tDebBomba = agora;
    bombaLigada = !bombaLigada;
  }
  antBomba = lB;

  // -- Botao PROCESSO (inicia / pausa) ---------------------
  bool lP = digitalRead(BTN_PROCESSO);
  if (antProc == HIGH && lP == LOW && agora - tDebProc >= DEBOUNCE_MS) {
    tDebProc = agora;
    if (!processoRodando) {
      if (concluido) reiniciarProcesso();
      bombaLigada = false;
      processoRodando = true;
      tInicio = agora;
    } else {
      if (tempAtingida) tempoDecorrido += agora - tInicio;
      processoRodando = false;
    }
  }
  antProc = lP;

  // -- Setpoint da etapa atual -----------------------------
  float setpoint = receita[etapaAtual].temp;

  // -- Atingiu a temperatura da etapa? Libera a contagem ---
  if (processoRodando && !concluido && !tempAtingida && sensorOK && temperatura >= setpoint) {
    tempAtingida   = true;
    tInicio        = agora;     // o tempo da etapa so comeca AGORA
    tempoDecorrido = 0;
  }

  // -- Avanco de etapa / conclusao (apos contar a duracao) -
  if (processoRodando && !concluido && tempAtingida) {
    unsigned long total = tempoDecorrido + (agora - tInicio);
    if (total >= receita[etapaAtual].durMs) {
      if (etapaAtual + 1 < numEtapas) {
        etapaAtual++;
        tempAtingida   = false;     // a proxima etapa espera atingir a temp dela
        tempoDecorrido = 0;
        tInicio        = agora;
      } else {
        processoRodando = false;
        concluido       = true;
      }
    }
  }

  // -- Aquecedor com histerese (processo rodando) ----------
  if (sensorOK && processoRodando) {
    if (!aquecedorLigado && temperatura < setpoint - TEMP_HISTERESE)
      digitalWrite(RELE_AQUECEDOR, HIGH), aquecedorLigado = true;
    else if (aquecedorLigado && temperatura >= setpoint)
      digitalWrite(RELE_AQUECEDOR, LOW),  aquecedorLigado = false;
  } else if (aquecedorLigado) {
    digitalWrite(RELE_AQUECEDOR, LOW); aquecedorLigado = false;
  }

  // -- Bomba (sem intertravamento) -------------------------
  digitalWrite(RELE_BOMBA, bombaLigada ? LOW : HIGH);

  // -- LCD (200ms) -----------------------------------------
  if (agora - tLCD >= INTERVALO_LCD) {
    tLCD = agora;

    unsigned long dec = tempAtingida ? (tempoDecorrido + (processoRodando ? agora - tInicio : 0)) : 0;

    // Linha 1: "Temp:  65.0 C AQUEC "
    lcd.setCursor(0, 1); lcd.print(F("Temp: "));
    if (sensorOK) {
      char bufT[7]; dtostrf(temperatura, 5, 1, bufT);
      lcd.print(bufT); lcd.write(byte(0)); lcd.print('C');
      lcd.print(aquecedorLigado ? F(" AQUEC ") : F("       "));
    }

    // Linha 2: "Bomba: DESLIGADA    "
    lcd.setCursor(0, 2); lcd.print(F("Bomba: "));
    lcdFixo(bombaLigada ? "LIGADA" : "DESLIGADA", 13);

    // Linha 3: "Et1/3 RUN  12:00    "  (20 cols)
    lcd.setCursor(0, 3);
    lcd.print(F("Et"));
    lcd.print(etapaAtual + 1); lcd.print('/'); lcd.print(numEtapas);
    lcd.print(' ');
    lcd.print(concluido ? F("FIM ") : (processoRodando ? F("RUN ") : F("STOP")));
    lcd.print(' ');
    lcdTimer(dec);
    lcd.print(F("    "));   // padding p/ limpar restos (16 + 4 = 20 cols)
  }

  enviarJSON(agora);
  receberComando(agora);
}
