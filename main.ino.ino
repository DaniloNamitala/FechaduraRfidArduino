#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <Keypad.h> // Biblioteca do codigo

typedef enum { UNLOCK_LOOP, REGISTER, MEMORY_CLEAR, ID_INPUT_REGISTER, ID_INPUT_DELETE, INPUT_PASSWORD } PState;

#define SS_PIN 10
#define RST_PIN 2
#define BTN 3
#define REGISTER_BLINK_DELAY 500
#define TEXT_DELAY 2000
#define LCD_ADDR 0x27
#define HOLD_TIME 3000

#define ADDR_TAGS 0x0
#define ADDR_N_TAGS 0x200
#define ADDR_N_GAPS 0x201
#define ADDR_GAPS 0x202

struct Registry {
  uint32_t matricula;
  char rfid[4];
};

const byte LINHAS = 4; // Linhas do teclado
const byte COLUNAS = 4; // Colunas do teclado

const char TECLAS_MATRIZ[LINHAS][COLUNAS] = { // Matriz de caracteres (mapeamento do teclado)
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

const char master_pass[4] = "1121";

const byte PINOS_LINHAS[LINHAS] = {7, 6, 5, 4}; // Pinos de conexao com as linhas do teclado
const byte PINOS_COLUNAS[COLUNAS] = {A0, A1, A3, A2}; // Pinos de conexao com as colunas do teclado

Keypad keyboard = Keypad(makeKeymap(TECLAS_MATRIZ), PINOS_LINHAS, PINOS_COLUNAS, LINHAS, COLUNAS); // Inicia teclado

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance.
byte rfid_read[4];
byte zeros[4];
char btn_read = 0;
KeyState key_state = IDLE;
PState state = UNLOCK_LOOP;
PState next_state = UNLOCK_LOOP;
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);  // set the LCD address to 0x27 for a 16 chars and 2 line display
char lcd_1[16] = "Aguardando...";
char lcd_2[16] = "";
char text_input[16] = "";
long btn_time = 0L;
byte cursor_position = 0;

void ReadRfid();
void HandleRfid();
void ReadKeyboard();
void HandleKeyboard();
byte NumberOfTags();
void RefreshDisplay();
void DisplayPrint(uint8_t line, const char text[16], bool hide = false);
void MemoryClear();
void WriteTagEEPROM(Registry &r);
Registry ReadTagEEPROM(int addr);
void DeleteTagEEPROM();

void setup() {
  memset(zeros, 0, 4);
  
  Wire.begin();
  Serial.begin(9600);  // Inicia a serial
  SPI.begin();         // Inicia  SPI bus
  mfrc522.PCD_Init();  // Inicia MFRC522
  lcd.init();
  lcd.backlight();
  pinMode(BTN, INPUT_PULLUP);
  state = UNLOCK_LOOP;
  DisplayPrint(1, "Inicializando...");
  delay(TEXT_DELAY);
}

void loop() {
  if (state == UNLOCK_LOOP) {
    memset(lcd_2, 0, 16);
    DisplayPrint(1, "Aguarando...");
  } else if (state == INPUT_PASSWORD) {
    memset(lcd_2, 0, 16);
    DisplayPrint(1, "Senha Meste:");
  } else if (state == ID_INPUT_REGISTER) {
    memset(lcd_2, 0, 16);
    DisplayPrint(1, "Matricula (C):");
  } else if (state == ID_INPUT_DELETE) {
    memset(lcd_2, 0, 16);
    DisplayPrint(1, "Matricula (D):");
  } else if (state == REGISTER) {
    memset(lcd_2, 0, 16);
    DisplayPrint(1, "Passe o Cartao:");
  }
  memset(rfid_read, 0, 4);

  ReadKeyboard();
  HandleKeyboard();
  if (state == UNLOCK_LOOP || state == REGISTER) {
    ReadRfid();
    HandleRfid();
  }
  if (state == MEMORY_CLEAR) {
    MemoryClear();
  }
}

void ReadKeyboard() {
  char leitura_teclas = keyboard.getKey(); // Atribui a variavel a leitura do teclado
  key_state = keyboard.getState();

  if (leitura_teclas) { // Se alguma tecla foi pressionada
    btn_read = leitura_teclas;
  }
}

void HandleKeyboard() {
  if (key_state == PRESSED) {
    btn_time = millis();
  }

  if (key_state == RELEASED && state != REGISTER) {
    if (btn_read == '#' && state == UNLOCK_LOOP) {
      next_state = ID_INPUT_REGISTER;
      state = INPUT_PASSWORD;
      cursor_position = 0;
    } else if (btn_read == 'D' && state == UNLOCK_LOOP) {
      next_state = ID_INPUT_DELETE;
      state = INPUT_PASSWORD;
    }

    if (state == ID_INPUT_REGISTER || state == ID_INPUT_DELETE || state == INPUT_PASSWORD) {
      if (btn_read >= '0' && btn_read <= '9' && cursor_position < 9) {
        text_input[cursor_position++] = btn_read;
        DisplayPrint(2, text_input, state == INPUT_PASSWORD);
      } else if (btn_read == 'A' && cursor_position > 0) {
        text_input[--cursor_position] = 0;
        DisplayPrint(2, text_input);
      } else if (btn_read == 'C' && cursor_position == 9) {
        if (state == ID_INPUT_REGISTER) {
          state = REGISTER;
        } else if (state == ID_INPUT_DELETE) {
          memset(lcd_2, 0, 16);
          RefreshDisplay();
          DeleteTagEEPROM();
          memset(text_input, 0, 16);
          cursor_position = 0;
          state = UNLOCK_LOOP;
        }
        memset(lcd_2, 0, 16);
        RefreshDisplay();
      } else if (btn_read == 'C' && state == INPUT_PASSWORD) {
        if (cursor_position == 4 && memcmp(master_pass, text_input, 4) == 0) {
          state = next_state;
          next_state = UNLOCK_LOOP;
          memset(text_input, 0, 16);
          cursor_position = 0;
          return;
        }

        memset(lcd_2, 0, 16);
        DisplayPrint(1, "Senha Incorreta!");
        memset(text_input, 0, 16);
        cursor_position = 0;
        delay(TEXT_DELAY);
        state = UNLOCK_LOOP;
        next_state = UNLOCK_LOOP;
      }
    }
  } else if (key_state == HOLD && state == UNLOCK_LOOP) {
    if (millis() - btn_time > HOLD_TIME) {
      if (btn_read == '*') {
        state = INPUT_PASSWORD;
        next_state = MEMORY_CLEAR;
      }
    }
  }
}

void DisplayPrint(uint8_t line, const char text[16], bool hide = false) {
  char masked[16];
  memset(masked, 0, 16);
  strncpy(masked, text, sizeof(masked));
  if (hide) {
    size_t len = strlen(masked);
    memset(masked, '*', len);
    masked[len] = '\0';
  }

  if (line == 1) {
    if (memcmp(lcd_1, masked, 16) == 0) return;
    memcpy(lcd_1, masked, 16);
  } else {
    if (memcmp(lcd_2, masked, 16) == 0) return;
    memcpy(lcd_2, masked, 16);
  }
  RefreshDisplay();
}

void RefreshDisplay() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(lcd_1);
  lcd.setCursor(0,1);
  lcd.print(lcd_2);
}

byte NumberOfTags() {
  byte n;
  EEPROM.get(ADDR_N_TAGS, n);
  return n;
}

byte NumberOfGaps() {
  byte n;
  EEPROM.get(ADDR_N_GAPS, n);
  return n;
}

void MemoryClear() {
  DisplayPrint(1, "LIMPANDO MEMORIA..");
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0);
  }
  state = UNLOCK_LOOP;
  delay(TEXT_DELAY);
}

void DeleteTagEEPROM() {
  uint32_t s = strtoul(text_input, nullptr, 10);
  int n = NumberOfTags();

  for (byte i=0; i<n; i++) {
    byte addr = ADDR_TAGS + i * sizeof(Registry);
    uint32_t f;

    EEPROM.get(addr, f);

    if (s == f) {
      Registry r;
      n--;
      if (i < n) { // se for a ultima nao precisa mover, só mudar a quantidade
        EEPROM.get(ADDR_TAGS + (n * sizeof(Registry)), r);
        EEPROM.put(addr, r);
      }
      EEPROM.write(ADDR_N_TAGS, n);
      DisplayPrint(1, "Registro Apagado!");
      delay(TEXT_DELAY);
      return;
    }
  }
  DisplayPrint(1, "Nao Encontrado!");
  delay(TEXT_DELAY);
}

void HandleRfid() {
  if (memcmp(rfid_read, zeros, 4) == 0)
  return;
  for (int i = 0; i < sizeof(rfid_read); i++) {
    Serial.print(rfid_read[i], HEX);
    Serial.print(" "); // Add space for readability
  }
  Serial.println();

  if (state == REGISTER) {
    Registry r;
    r.matricula = strtoul(text_input, nullptr, 10);
    memcpy(r.rfid, rfid_read, 4);
    WriteTagEEPROM(r);
    memset(text_input, 0, 16);
    cursor_position = 0;
    DisplayPrint(1, "Cadastrado...");
    delay(TEXT_DELAY);

    state = UNLOCK_LOOP;
  } else if (state == UNLOCK_LOOP) {
    byte n = NumberOfTags();
    Serial.print("NUMERO TAGS: "); Serial.println(n);
   
    if (n == 0) {
      DisplayPrint(1, "Acesso Negado!");
      delay(TEXT_DELAY);
      return;
    }

    byte limit = ADDR_TAGS + n * sizeof(Registry);

    for (byte addr=ADDR_TAGS; addr < limit; addr+=sizeof(Registry)) {
      Registry r = ReadTagEEPROM(addr);
      
      if (memcmp(r.rfid, rfid_read, 4) == 0) {
        sprintf(lcd_2, "%lu", r.matricula);
        DisplayPrint(1, "Acesso Liberado!");
        delay(TEXT_DELAY);
        return;
      }
    }
    DisplayPrint(1, "Acesso Negado!");
    delay(TEXT_DELAY);
  }
}

Registry ReadTagEEPROM(int addr) {
  Registry r;
  EEPROM.get(addr, r);
  return r;
}

void WriteTagEEPROM(Registry &r) {
  byte n = NumberOfTags();
  int addr = ADDR_TAGS + (n * sizeof(Registry));
  EEPROM.put(addr, r);
  n += 1;
  EEPROM.write(ADDR_N_TAGS, n);
}

void ReadRfid() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  memcpy(rfid_read, mfrc522.uid.uidByte, mfrc522.uid.size);
}
