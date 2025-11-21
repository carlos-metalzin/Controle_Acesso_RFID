# Controle_Acesso_RFID

# Controle de Acesso RFID com ESP32 e Verificação Online via CSV

Sistema de controle de acesso baseado em **ESP32**, utilizando: -
**Sensor RFID MFRC522** - **Sensor de presença HC-SR04** - **Buzzer de
alarme** - **Botão para ativar/desativar o sistema** - **Validação de
usuários via arquivo CSV hospedado no GitHub**

Este projeto permite que usuários autorizados, cadastrados em um arquivo
**CSV online**, tenham acesso liberado ao aproximar seus cartões RFID.
Caso o usuário não esteja autorizado, o alarme sonoro continua ativo.

## Funcionalidades

-   Detecta presença utilizando o **HC-SR04**
-   Aciona **buzzer de alarme**
-   Lê cartões RFID (**UID HEX**) com o **MFRC522**
-   Consulta **arquivo CSV no GitHub (HTTPS)** para validar:
    -   UID do cartão
    -   Área do leitor
    -   Status de treinamento
-   Se autorizado → **alarme é silenciado**
-   Botão físico ativa/desativa o sistema
-   LED indica o estado do sistema
-   Logs detalhados no Serial

## Formato do CSV

``` csv
uid,area,treinamento,nome
B499F804,AREA1,OK,Paulo
B122C8A3,AREA1,OK,Carlos
```

## Ligações (Pinout)

### ESP32 → MFRC522 (RFID)

SDA → GPIO5\
SCK → GPIO18\
MOSI → GPIO23\
MISO → GPIO19\
RST → GPIO22\
3.3V → 3V3\
GND → GND

### ESP32 → HC-SR04

VCC → 5V\
GND → GND\
TRIG → GPIO25\
ECHO → GPIO26 *(via divisor de tensão)*

### ESP32 → LED

Anodo → resistor → GPIO12\
Catodo → GND

### ESP32 → Botão

GPIO14 → Botão → GND\
*(pull-up interno)*

### ESP32 → Buzzer

GPIO27 → +\
GND → -

## Bibliotecas Necessárias

-   MFRC522 (Miguel Balboa)
-   WiFi.h
-   WiFiClientSecure.h
-   HTTPClient.h

## Fluxo do Sistema

\[Botão ligado\] → Sistema ativo\
↓\
\[Presença detectada\] → Buzzer ON\
↓\
\[Cartão aproximado\] → UID lido\
↓\
\[Consulta CSV no GitHub\]\
↓\
UID cadastrado e treinamento OK?\
↓\
Sim → Desliga buzzer\
Não → Continua o alarme
