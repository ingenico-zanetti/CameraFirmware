#include "stm32f1xx_hal.h"

// ---------------- CONFIG ----------------

// Handles
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_tx;

// ---------------- DMA INIT ----------------

void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  // -------- TX (SPI2_TX) → Channel 5 --------
  hdma_tx.Instance = DMA1_Channel5;
  hdma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_tx.Init.Mode = DMA_NORMAL; // ou DMA_CIRCULAR
  hdma_tx.Init.Priority = DMA_PRIORITY_HIGH;

  HAL_DMA_Init(&hdma_tx);

  // IRQ DMA

  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
}

// ---------------- SPI2 INIT ----------------

void MX_SPI2_Init(void)
{
  __HAL_RCC_SPI2_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // --- SPI CONFIG ---
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_1LINE;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_LSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;

  HAL_SPI_Init(&hspi2);

  // ✅ link DMA
  __HAL_LINKDMA(&hspi2, hdmatx, hdma_tx);
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // --- SCK + MOSI + NSS ---
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

// ---------------- IRQ HANDLER ----------------

extern "C" void DMA1_Channel5_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_tx);
}

// ---------------- CALLBACK ----------------

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI2){
  }
}

HardwareSerial piUart(PA10, PA9);

#define LED_COUNT_MAX (16)

#define DATA_BYTES_PER_LED (9)
#define PADDING_BYTES_PER_LED (1)
#define BYTES_PER_LED (DATA_BYTES_PER_LED + PADDING_BYTES_PER_LED)

// ---------------- SETUP ----------------
unsigned char ledTx[1 + (LED_COUNT_MAX) * BYTES_PER_LED + 1]; // Add one extra 0 as first and last byte to get a clean MOSI whe not transmitting

static uint32_t ledByteToBits(unsigned char octet){ /* only 24 bits used */
  uint32_t result = 0;
  for(int i = 0 ; i < 8 ; i++){
    result <<= 3;
    if(octet & 0x1){
      result |= 3; // 0b011
    }else{
      result |= 1; // 0b001
    }
    octet >>= 1;
  }
  return(result);
}

typedef struct {
  unsigned char r;
  unsigned char g;
  unsigned char b;
} Color_s;

Color_s ledColors[LED_COUNT_MAX];
static int redComponent = 0;
static int greenComponent = 0;
static int blueComponent = 0;
static int fanPWM = 77; // start just above 30%

static void rotateLeds(void){
  #if 0
  Color_s led = ledColors[0];
  memmove(ledColors, ledColors + 1, (LED_COUNT_MAX - 1) * sizeof(Color_s));
  ledColors[LED_COUNT_MAX - 1] = led;
  #else
  Color_s led = ledColors[LED_COUNT_MAX - 1];
  memmove(ledColors + 1, ledColors, (LED_COUNT_MAX - 1) * sizeof(Color_s));
  ledColors[0] = led;
  #endif
}

static void rotateComponents(void){
  #if 0
  unsigned char *components = (unsigned char *)ledColors;
  unsigned char component = *components;
  memmove(components, components + 1, sizeof(ledColors) - 1);
  components[sizeof(ledColors) - 1] = component;
  #else
  unsigned char *components = (unsigned char *)ledColors;
  unsigned char component = components[sizeof(ledColors) - 1];
  memmove(components + 1, components, sizeof(ledColors) - 1);
  *components = component;
  #endif
}

static void ledColorsToLedBits(/*Color_s *leds*/){
  int len = LED_COUNT_MAX;
  unsigned char *bitPtr = ledTx;
  *bitPtr++ = 0; // Clean state of MOSI before actual LED data
  int i = 0;
  while(i < len){
    // Green
    uint32_t greenBits = ledByteToBits(ledColors[i].g);
    // Little Endian trick
    memcpy(bitPtr, &greenBits, 3);
    bitPtr += 3;
    // Red
    uint32_t redBits = ledByteToBits(ledColors[i].r);
    // Little Endian trick
    memcpy(bitPtr, &redBits, 3);
    bitPtr += 3;
    // Blue
    uint32_t blueBits = ledByteToBits(ledColors[i].b);
    // Little Endian trick
    memcpy(bitPtr, &blueBits, 3);
    bitPtr += 3;

    *bitPtr++ = 0; // Clean state after each LED to avoid crossover

    //
    i++;
  }
  
  *bitPtr++ = 0; // Clean state at end of transmission, so the next transmission has no glitch

}

int ledState = HIGH;

enum PinDescriptorPort {
  PIN_DESCRIPTOR_PORT_A,
  PIN_DESCRIPTOR_PORT_B
};

typedef struct {
  int arduinoPin;
  enum PinDescriptorPort port;
  int bit;
} PinDescriptor;

class Encodeur {
  public:
    Encodeur(char n, PinDescriptor a, PinDescriptor b, PinDescriptor s){
      pinA = a;
      pinB = b;
      pinS = s;
      pinMode(a.arduinoPin, INPUT_PULLUP);
      pinMode(b.arduinoPin, INPUT_PULLUP);
      pinMode(s.arduinoPin, INPUT_PULLUP);
      abHist = 0; sHist = 0;
      name = n;
    };
    bool update(uint16_t portA, uint16_t portB);
  private:
    char name;

    PinDescriptor pinA;
    PinDescriptor pinB;
    PinDescriptor pinS;

    int pinAValue(uint16_t portA, uint16_t portB);
    int pinBValue(uint16_t portA, uint16_t portB);
    int pinSValue(uint16_t portA, uint16_t portB);
    
    uint16_t abHist;
    uint16_t sHist;
};

int Encodeur::pinAValue(uint16_t portA, uint16_t portB){
  if(PIN_DESCRIPTOR_PORT_A == pinA.port){
    return((portA >> pinA.bit) & 1);
  }else{
    return((portB >> pinA.bit) & 1);
  }
}

int Encodeur::pinBValue(uint16_t portA, uint16_t portB){
  if(PIN_DESCRIPTOR_PORT_A == pinB.port){
    return((portA >> pinB.bit) & 1);
  }else{
    return((portB >> pinB.bit) & 1);
  }
}

int Encodeur::pinSValue(uint16_t portA, uint16_t portB){
  if(PIN_DESCRIPTOR_PORT_A == pinS.port){
    return((portA >> pinS.bit) & 1);
  }else{
    return((portB >> pinS.bit) & 1);
  }
}

bool Encodeur::update(uint16_t portA, uint16_t portB){
  bool changed = false;
  abHist <<= 1;
  abHist |= pinAValue(portA, portB);
  abHist <<= 1;
  abHist |= pinBValue(portA, portB);
  abHist &= 0x0F;

  if(/* 0b1011 */ 0xB == abHist){
    changed = true;
    piUart.printf("%c-" "\n", name);
    if('1' == name){
      if(redComponent > 0){
        redComponent--;
      }
    }
    if('2' == name){
      if(greenComponent > 0){
        greenComponent--;
      }
    }
    if('3' == name){
      if(blueComponent > 0){
        blueComponent--;
      }
    }
    if('4' == name){
      if(fanPWM > 0){
        fanPWM--;
      }
    }
  }else if(/* 0b1110 */ 0xE == abHist){
    changed = true;
    piUart.printf("%c+" "\n", name);
    if('1' == name){
      if(redComponent < 0xFF){
        redComponent++;
      }
    }
    if('2' == name){
      if(greenComponent < 0xFF){
        greenComponent++;
      }
    }
    if('3' == name){
      if(blueComponent < 0xFF){
        blueComponent++;
      }
    }
    if('4' == name){
      if(fanPWM < 0xFF){
        fanPWM++;
      }
    }
  }

  sHist <<= 1;
  sHist |= pinSValue(portA, portB);
  sHist &= 0x03;

  if(/* 0b01 */ 0x1 == sHist){
    changed = true;
    piUart.printf("%cD" "\n", name);
  }else if(/* 0b10 */ 0x2 == sHist){
    changed = true;
    piUart.printf("%cU" "\n", name);
  }
  if(changed){
    // piUart.printf("%c r=0x%02X, g=0x%02X, b=0x%02X, PWM=0x%02X" "\n", name, redComponent, greenComponent, blueComponent, fanPWM);
  }
  return(changed);
}

PinDescriptor enc1a = {PA4, PIN_DESCRIPTOR_PORT_A, 4};
PinDescriptor enc1b = {PA5, PIN_DESCRIPTOR_PORT_A, 5};
PinDescriptor enc1s = {PA6, PIN_DESCRIPTOR_PORT_A, 6};

Encodeur encodeur1('1', enc1a, enc1b, enc1s);

PinDescriptor enc2a = {PA7, PIN_DESCRIPTOR_PORT_A, 7};
PinDescriptor enc2b = {PB0, PIN_DESCRIPTOR_PORT_B, 0};
PinDescriptor enc2s = {PB1, PIN_DESCRIPTOR_PORT_B, 1};

Encodeur encodeur2('2', enc2a, enc2b, enc2s);

PinDescriptor enc3a = {PB5, PIN_DESCRIPTOR_PORT_B, 5};
PinDescriptor enc3b = {PB6, PIN_DESCRIPTOR_PORT_B, 6};
PinDescriptor enc3s = {PB7, PIN_DESCRIPTOR_PORT_B, 7};

Encodeur encodeur3('3', enc3a, enc3b, enc3s);

PinDescriptor enc4a = {PA15, PIN_DESCRIPTOR_PORT_A, 15};
PinDescriptor enc4b = {PB3, PIN_DESCRIPTOR_PORT_B, 3};
PinDescriptor enc4s = {PB4, PIN_DESCRIPTOR_PORT_B, 4};

Encodeur encodeur4('4', enc4a, enc4b, enc4s);

uint32_t oldMillis;

void setup()
{
  Serial.begin(115200);
  piUart.begin(115200, SERIAL_8N1);
  HAL_Init();

  MX_DMA_Init();
  MX_SPI2_Init();

  pinMode(LED_BUILTIN, INPUT_PULLUP);
  digitalWrite(LED_BUILTIN, ledState);

  // PWM FAN
  // PWM fan control signal is on PB8
  // Tachometer readout is on PB9
  analogWriteResolution(8);
  analogWriteFrequency(25000);
  analogWrite(PB8, fanPWM);
  pinMode(PB9, INPUT_PULLUP);

  {
    Color_s color = {.r = 0, .g = 0, .b = 0};
    for(int i = 0 ; i < LED_COUNT_MAX ; i++){
      ledColors[i] = color;
    }
    ledColorsToLedBits();
    HAL_SPI_Transmit_DMA(&hspi2, ledTx, sizeof(ledTx));
  }
}

// ---------------- LOOP ----------------
void loop(){
  uint16_t portA = GPIOA->IDR;
  uint16_t portB = GPIOB->IDR;
  bool updated = false;
  updated = updated || encodeur1.update(portA, portB);
  updated = updated || encodeur2.update(portA, portB);
  updated = updated || encodeur3.update(portA, portB);
  updated = updated || encodeur4.update(portA, portB);
  if(updated){
    Color_s color = {.r=redComponent, .g=greenComponent, .b=blueComponent};
    for(int i = 0 ; i < LED_COUNT_MAX ; i++){
      ledColors[i] = color;
    }
    ledColorsToLedBits();
    analogWrite(PB8, fanPWM);
    HAL_SPI_Transmit_DMA(&hspi2, ledTx, sizeof(ledTx));
  }
  if(piUart.available() > 0){
    uint8_t octet = piUart.read();
    piUart.write(octet);
  }
}
