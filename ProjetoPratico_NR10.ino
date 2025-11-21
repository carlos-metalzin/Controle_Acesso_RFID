/**
 *
 * Bibliotecas necessárias:
 *   - WiFi (ESP32)
 *   - WiFiClientSecure (ESP32)
 *   - HTTPClient (ESP32)
 *   - SPI
 *   - MFRC522 by Miguel Balboa
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>   // 
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>

// --------- CONFIG Wi-Fi ---------
const char* WIFI_SSID = "INSIRA_SSID";
const char* WIFI_PASS = "INSIRA_SENHA";

// --------- CONFIG CSV / ÁREA ---------
const char* CSV_URL = "https://raw.githubusercontent.com/carlos-metalzin/Controle_Acesso_RFID/main/usuarios.csv";

// Identificador da área desse leitor
const char* AREA_ID = "AREA1";

// --------- Pinos ESP32 (AJUSTE CONFORME SEU HARDWARE) ---------
const uint8_t PIN_BTN    = 14;  // GPIO14 - botão (com pull-up interno)
const uint8_t PIN_LED    = 12;  // GPIO12 - LED de status
const uint8_t PIN_BUZZER = 27;  // GPIO27 - buzzer

// HC-SR04
const uint8_t PIN_TRIG   = 25;  // GPIO25 - TRIG
const uint8_t PIN_ECHO   = 26;  // GPIO26 - ECHO

// MFRC522 - SPI padrão ESP32
// SCK  -> GPIO18
// MISO -> GPIO19
// MOSI -> GPIO23
// SDA(SS) -> GPIO5
// RST -> GPIO22
const uint8_t PIN_SS     = 5;   // SDA/SS do MFRC522
const uint8_t PIN_RST    = 22;  // RST do MFRC522

MFRC522 rfid(PIN_SS, PIN_RST);

// ---------- Parâmetros ----------
const unsigned long BTN_DEBOUNCE_MS = 40;
const unsigned long SILENCIO_MS     = 10000; // após cartão válido, tempo de silêncio
const unsigned long PULSE_TRIG_US   = 10;
const unsigned long TIMEOUT_ECHO_US = 25000; // ~4m máx

const unsigned int  DIST_LIMIT_CM   = 10; // distância para disparar alarme

// ---------- Estado ----------
bool modoAtivo          = false;
bool alarmeSoando       = false;
unsigned long tUltBtn   = 0;
bool btnEstadoAnterior  = HIGH; // com pull-up, HIGH = solto
unsigned long silencioAte = 0;

// ---------- Funções auxiliares de hardware ----------
unsigned int medeDistanciaCm() {
  // Pulso de trigger
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(PULSE_TRIG_US);
  digitalWrite(PIN_TRIG, LOW);

  // mede o tempo do echo HIGH
  unsigned long dur = pulseIn(PIN_ECHO, HIGH, TIMEOUT_ECHO_US);
  if (dur == 0) return 9999; // timeout -> muito longe/sem eco

  // Conversão: velocidade do som ~343 m/s => 29.1 us por cm ida+volta; divide por 58
  unsigned int cm = (unsigned int)(dur / 58);
  return cm;
}

void buzzerOn()  { digitalWrite(PIN_BUZZER, HIGH); alarmeSoando = true; }
void buzzerOff() { digitalWrite(PIN_BUZZER, LOW);  alarmeSoando = false; }

String uidToHex(const MFRC522::Uid& uid) {
  String s;
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// ---------- Helpers de String ----------
String trimEspacos(const String& str) {
  String s = str;
  s.trim();
  return s;
}

/// Divide linha CSV e pega APENAS as 3 primeiras colunas: uid, area, treinamento
void splitLinhaCSV(const String& linha, String& col1, String& col2, String& col3) {
  int p1 = linha.indexOf(',');          // fim da 1ª coluna
  if (p1 < 0) {
    col1 = trimEspacos(linha);
    col2 = "";
    col3 = "";
    return;
  }

  int p2 = linha.indexOf(',', p1 + 1);  // fim da 2ª coluna

  col1 = trimEspacos(linha.substring(0, p1));  // uid

  if (p2 < 0) {
    // Só tem 2 colunas
    col2 = trimEspacos(linha.substring(p1 + 1));
    col3 = "";
    return;
  }

  col2 = trimEspacos(linha.substring(p1 + 1, p2));  // area

  // Agora vamos procurar a 3ª coluna (treinamento)
  int p3 = linha.indexOf(',', p2 + 1);  // pode existir 4ª coluna (nome) ou não

  if (p3 < 0) {
    // Não tem mais vírgula, então o resto da linha é só treinamento
    col3 = trimEspacos(linha.substring(p2 + 1));
  } else {
    // Tem mais coisa depois (ex.: nome), pegamos só até a próxima vírgula
    col3 = trimEspacos(linha.substring(p2 + 1, p3));
  }
}


// ---------- Verificação remota no CSV ----------
// Retorna true se o UID tiver treinamento OK para AREA_ID
bool verificaTreinamentoRemoto(const String& uidHex) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[NET] WiFi desconectado, não foi possível consultar CSV."));
    return false;
  }

  // ✅ Usar WiFiClientSecure para HTTPS (GitHub)
  WiFiClientSecure client;
  client.setInsecure();  // aceita qualquer certificado (OK pra teste / projeto interno)

  HTTPClient http;

  Serial.print(F("[NET] Baixando CSV de: "));
  Serial.println(CSV_URL);

  if (!http.begin(client, CSV_URL)) {
    Serial.println(F("[NET] Falha em http.begin()"));
    return false;
  }

  int httpCode = http.GET();
  Serial.print(F("[NET] httpCode = "));
  Serial.println(httpCode);

  if (httpCode <= 0) {
    Serial.print(F("[NET] Erro HTTPClient: "));
    Serial.println(http.errorToString(httpCode));  //  mostra texto do erro (ex: connection lost)
    http.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK) {
    Serial.print(F("[NET] HTTP code inesperado: "));
    Serial.println(httpCode);
    http.end();
    return false;
  }

  String csv = http.getString();
  http.end();

  // Percorre o CSV linha por linha
  int start = 0;
  bool primeiraLinha = true;

  while (true) {
    int end = csv.indexOf('\n', start);
    if (end == -1) end = csv.length();

    String linha = csv.substring(start, end);
    linha.trim();
    start = end + 1;

    if (linha.length() == 0) {
      if (end >= (int)csv.length()) break;
      continue;
    }

    // pula header
    if (primeiraLinha) {
      primeiraLinha = false;
      continue;
    }

    String colUID, colArea, colTreino;
    splitLinhaCSV(linha, colUID, colArea, colTreino);

    // Normaliza para maiúsculas
    colUID.toUpperCase();
    colArea.toUpperCase();
    colTreino.toUpperCase();

    String areaLocal = String(AREA_ID);
    areaLocal.toUpperCase();

    if (colUID == uidHex && colArea == areaLocal) {
      Serial.print(F("[CSV] Encontrado UID na area: "));
      Serial.print(colUID); Serial.print(" / "); Serial.println(colArea);

      if (colTreino == "OK" || colTreino == "1") {
        Serial.println(F("[CSV] Treinamento OK. Acesso liberado."));
        return true;
      } else {
        Serial.println(F("[CSV] Treinamento NAO OK. Acesso negado."));
        return false;
      }
    }

    if (end >= (int)csv.length()) break;
  }

  Serial.println(F("[CSV] UID nao encontrado para esta area."));
  return false;
}

// ---------- RFID ----------
void tentaLerRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  String uidHex = uidToHex(rfid.uid);

  // UID em HEX no terminal
  Serial.print(F("[RFID] UID HEX: "));
  Serial.println(uidHex);

  Serial.print(F("[RFID] Bytes: "));
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print("0x");
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Consulta remota no CSV
  bool autorizado = verificaTreinamentoRemoto(uidHex);

  if (autorizado) {
    Serial.println(F("[RFID] Cartao autorizado e com treinamento. Silenciando alarme."));
    buzzerOff();
    silencioAte = millis() + SILENCIO_MS;
  } else {
    Serial.println(F("[RFID] Cartao NAO autorizado (ou sem treinamento)."));
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------- Botão ----------
void atualizaBotaoToggle() {
  bool leitura = digitalRead(PIN_BTN); // HIGH=solto (pull-up), LOW=pressionado
  unsigned long agora = millis();

  // detecção de borda com debounce
  if (leitura != btnEstadoAnterior && (agora - tUltBtn) > BTN_DEBOUNCE_MS) {
    tUltBtn = agora;
    btnEstadoAnterior = leitura;

    if (leitura == LOW) { // borda de pressão
      modoAtivo = !modoAtivo;
      digitalWrite(PIN_LED, modoAtivo ? HIGH : LOW);
      Serial.print(F("[BOTAO] Modo ativo: "));
      Serial.println(modoAtivo ? F("SIM") : F("NAO"));
      if (!modoAtivo) {
        buzzerOff();
      }
    }
  }
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(100);

  // Pinos
  pinMode(PIN_BTN,    INPUT_PULLUP);
  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_TRIG,   OUTPUT);
  pinMode(PIN_ECHO,   INPUT);

  digitalWrite(PIN_LED, LOW);
  buzzerOff();

  // Wi-Fi
  Serial.println();
  Serial.print(F("Conectando ao Wi-Fi: "));
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print(F("Wi-Fi conectado. IP: "));
  Serial.println(WiFi.localIP());

  // SPI + RFID (pinos SPI padrão do ESP32: 18,19,23)
  SPI.begin(18, 19, 23);
  rfid.PCD_Init();
  delay(50);

  Serial.println(F("Sistema iniciado. Pressione o botao para ativar/desativar."));
  Serial.println(F("Aproxime um cartao para verificacao no CSV remoto."));
}

void loop() {
  // 1) Leitura do botão (toggle do modo)
  atualizaBotaoToggle();

  // 2) Se estiver ativo, processa sensores/alarme
  if (modoAtivo) {
    // Se está no período de silêncio pós-autorização, apenas verifica RFID (pode renovar silêncio)
    if (millis() < silencioAte) {
      tentaLerRFID();
      delay(20);
      return;
    }

    // Mede distância
    unsigned int dist = medeDistanciaCm();
    // Serial.println(dist); // debug, se quiser

    // Buzzer SÓ depende do sensor de presença:
    if (dist <= DIST_LIMIT_CM) {
      if (!alarmeSoando) buzzerOn();
    } else {
      if (alarmeSoando) buzzerOff();
    }

    // Verifica RFID (pode silenciar o alarme)
    if (alarmeSoando) {
      tentaLerRFID();
    } else {
      tentaLerRFID();
    }
  }

  delay(20);
}
