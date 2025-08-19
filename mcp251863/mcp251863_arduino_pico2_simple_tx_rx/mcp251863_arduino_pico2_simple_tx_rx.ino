#include <ACAN2517FD.h>
#include <SPI.h>

// 中断+SPI 引脚配置
static const byte MCP_INT = 0; // INT output of MCP251863
static const byte MCP_CS = 1;  // CS input of MCP251863
static const byte MCP_SCK = 2; // SCK input of MCP251863
static const byte MCP_SDI = 3; // SDI input of MCP251863
static const byte MCP_SDO = 4; // SDO output of MCP251863

ACAN2517FD can(MCP_CS, SPI, MCP_INT);

void setup() {
  delay(10000); // 方便查看串口打印
  Serial.begin(921600);

  // https://arduino-pico.readthedocs.io/en/latest/spi.html
  SPI.setSCK(MCP_SCK); // GP2 for SPI0_SCK
  SPI.setTX(MCP_SDI);  // GP3 for SPI0_TX
  SPI.setRX(MCP_SDO);  // GP4 for SPI0_RX
  SPI.setCS(MCP_CS);   // GP5 for SPI0_CS
  SPI.begin();

  // 40MHz晶振, 1Mbps仲裁段, 5Mbps数据段
  ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_40MHz, 1000 * 1000,
                              DataBitRateFactor::x5);
  // Controller是MCP2518的2KB MessageRAM划分, 可理解为硬件RAM
  // Driver是MCU的RAM, 可理解为软件FIFO
  // 驱动中 mDriverReceiveFIFOSize 软件接收FIFO默认32
  settings.mControllerReceiveFIFOSize = 2; // 硬件接收FIFO大小
  // FIFO模式先来先发, QUEUE模式按优先级发, 二者可结合使用称混合模式
  settings.mDriverTransmitFIFOSize = 0;      // 不使用软件FIFO
  settings.mControllerTransmitFIFOSize = 16; // 使用16个TX FIFO
  settings.mControllerTransmitFIFORetransmissionAttempts =
      ACAN2517FDSettings::ThreeAttempts; // 重传3次, 也可选不重传或不限次
  settings.mControllerTXQSize = 1;       // 使用1个TXQ
  settings.mControllerTXQBufferRetransmissionAttempts =
      ACAN2517FDSettings::ThreeAttempts; // 重传3次, 也可选不重传或不限次
  const uint32_t errorCode = can.begin(settings, [] { can.isr(); });
  if (errorCode != 0) {
    Serial.print("CAN init failed with error code: ");
    Serial.println(errorCode, HEX);
    while (true) {
      delay(1000); // 停止执行
    }
  }
  // MessageRAM使用情况, 不超过 2KB
  uint32_t ram_usage = settings.ramUsage();
  Serial.print("RAM usage: ");
  Serial.println(ram_usage);
  Serial.println("CAN initialized successfully, Params:");
  auto print4 = [](const char *label, uint32_t a, uint32_t b, uint32_t c,
                   uint32_t d) {
    char buf[80];
    snprintf(buf, sizeof(buf), "%s %lu-%lu-%lu-%lu", label, (unsigned long)a,
             (unsigned long)b, (unsigned long)c, (unsigned long)d);
    Serial.println(buf);
  };
  print4("  nominal pre-tseg1-tseg2-sjw:", settings.mBitRatePrescaler,
         settings.mArbitrationPhaseSegment1, settings.mArbitrationPhaseSegment2,
         settings.mArbitrationSJW);
  print4("  data pre-tseg1-tseg2-sjw:", settings.mBitRatePrescaler,
         settings.mDataPhaseSegment1, settings.mDataPhaseSegment2,
         settings.mDataSJW);
}

void loop() {
  static uint32_t tx_delay = millis();
  CANFDMessage frame;
  if (tx_delay < millis()) {
    tx_delay += 2000;
    frame.id = 0x12345678; // 发送的ID
    frame.ext = true;      // 发送扩展帧
    frame.type =
        CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH; // 发送带速率切换的CAN FD帧
    // frame.idx = 255 ;  // Uncomment this for sending throught TXQ
    frame.len = 64;
    for (uint8_t i = 0; i < frame.len; i++) {
      frame.data[i] = i;
    }
    const bool ok = can.tryToSend(frame);
    if (!ok) {
      Serial.println("Send failure");
    }
  }

  if (can.available()) {
    can.receive(frame);
    auto printFrame = [](const CANFDMessage &f) {
      char buf[256];
      char type =
          (f.type == CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH)
              ? 'B'
              : ((f.type == CANFDMessage::CANFD_NO_BIT_RATE_SWITCH)
                     ? 'F'
                     : ((f.type == CANFDMessage::CAN_DATA) ? 'D' : 'R'));
      char idstr[16];
      if (f.ext) {
        snprintf(idstr, sizeof(idstr), "%08X", f.id);
      } else {
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%03X", f.id & 0x7FF);
        snprintf(idstr, sizeof(idstr), "%8s", tmp);
      }
      snprintf(buf, sizeof(buf), "(%lu) %s %c %c [%d] ",
               (unsigned long)millis(), idstr, f.ext ? 'E' : 'S', type, f.len);
      Serial.print(buf);
      if (f.type == CANFDMessage::CAN_REMOTE) {
        Serial.print(" [Remote Frame]");
      } else {
        for (uint8_t i = 0; i < f.len; i++) {
          if (i > 0) {
            Serial.print(' ');
          }
          Serial.printf("%02X", f.data[i]);
        }
      }
      Serial.println();
    };
    printFrame(frame);
  }
}