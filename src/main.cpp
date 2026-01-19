#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <bluefruit.h>
#include <TimeLib.h>
#include <Wire.h>

// --- 引脚定义 ---
#define PIN_SDA_HACK 28
#define PIN_SCL_HACK 29

// 使用硬件 I2C，性能更好
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// --- BLE 定义保持不变 ---
struct cts_time_t {
  uint16_t year; uint8_t month; uint8_t day; uint8_t hour; uint8_t minute; uint8_t second;
  uint8_t day_of_week; uint8_t fractions256; uint8_t adjust_reason;
} __attribute__((packed));

BLEClientService        ctsSvc(UUID16_SVC_CURRENT_TIME);
BLEClientCharacteristic ctcChar(UUID16_CHR_CURRENT_TIME);

void setupBLE();
void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);
void notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
void updateTimeFromData(uint8_t* data);
void drawCyberUI(); // 新的绘图函数

// --- 延迟配对定时器 ---
SoftwareTimer bondTimer;
void bond_timer_callback(TimerHandle_t xTimerID) {
  (void) xTimerID;
  if ( Bluefruit.connected() ) {
    uint16_t conn_handle = Bluefruit.connHandle();
    BLEConnection* conn = Bluefruit.Connection(conn_handle);
    if (conn) conn->requestPairing();
  }
}
void pair_callback(uint16_t conn_handle, uint8_t auth_status) {
  if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS) Serial.println("Paired");
}

// --- 视觉效果变量 ---
#define WAVE_LEN 32
int waveform[WAVE_LEN]; // 存储波形数据
int wave_idx = 0;

// --- 动画变量 ---
int last_s = -1;
int glitch_frames = 0; // 剩余故障帧数

void setup() {
  Serial.begin(115200);
  Wire.setPins(PIN_SDA_HACK, PIN_SCL_HACK);

  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(255); // 最大亮度

  // 初始化波形数组
  for(int i=0; i<WAVE_LEN; i++) waveform[i] = 10;

  bondTimer.begin(2000, bond_timer_callback, NULL, false); 
  setupBLE();
}

void loop() {
  // 保持较高的刷新率以实现“噪点”和波形的流畅感
  drawCyberUI();
}

void drawSawtooth(int x, int y, int width, int height, int val) {
  // 1. 更新波形数据 (保持不变)
  waveform[wave_idx] = map(val, 0, 59, 2, height - 2); //稍微留点边距
  if (random(0, 10) > 6) waveform[wave_idx] += random(-2, 3);
  
  // 索引环形滚动
  wave_idx = (wave_idx + 1) % WAVE_LEN;

  // 2. 绘制波形线 (核心修改部分)
  // 我们不再计算固定的 step，而是计算每个点的绝对坐标
  
  for (int i = 0; i < WAVE_LEN - 1; i++) {
    // 获取环形缓冲区的数据索引
    int idx = (wave_idx + i) % WAVE_LEN;
    int next_idx = (wave_idx + i + 1) % WAVE_LEN;
    
    // --- 关键修改开始 ---
    // 使用 map 函数将数组索引 (0 ~ WAVE_LEN-1) 映射到 屏幕宽度 (0 ~ width)
    // 这样无论数组多长，宽度多宽，都会自动拉伸填满
    int x1 = x + map(i, 0, WAVE_LEN - 1, 0, width);
    int x2 = x + map(i + 1, 0, WAVE_LEN - 1, 0, width);
    // --- 关键修改结束 ---

    int y1 = y + height - waveform[idx];
    int y2 = y + height - waveform[next_idx];
    
    // 绘制折线
    u8g2.drawLine(x1, y1, x2, y2);
    
    // 绘制竖线 (保持你的机械感设计)
    if (i % 4 == 0) {
      u8g2.drawVLine(x1, y1, waveform[idx]);
    }
  }

  // 画外框
  u8g2.drawFrame(x, y, width, height + 1);
}

// --- 辅助：随机生成的十六进制装饰字符串 ---
void drawRandomHex(int x, int y) {
  char buf[5];
  sprintf(buf, "0x%02X", random(0, 255));
  u8g2.setFont(u8g2_font_micro_tr); // 极小的字体
  u8g2.drawStr(x, y, buf);
}

// --- 核心：赛博朋克风格绘制 ---
void drawCyberUI() {
  u8g2.clearBuffer();

  int h = hour();
  int m = minute();
  int s = second();

  // 1. 绘制背景网格（增加科技感）
  // u8g2.setDrawColor(1);
  // for(int i=0; i<128; i+=21) u8g2.drawVLine(i, 0, 64);
  // u8g2.drawHLine(0, 42, 128);
  u8g2.drawFrame(0, 0, 128, 64);

  // 2. 时间显示（大号、硬朗字体）
  // 选用 Impact 风格的字体
  u8g2.setFont(u8g2_font_inb30_mn); 
  
  char timeBuf[6];
  sprintf(timeBuf, "%02d", h);
  u8g2.drawStr(4, 39, timeBuf); // 左边小时
  
  sprintf(timeBuf, "%02d", m);
  u8g2.drawStr(66, 39, timeBuf); // 右边分钟

  // 3. 故障效果 (Glitch Effect)
  // 随机触发：每帧有 5% 的概率发生
  if (random(0, 100) < 5) {
    int gType = random(0, 3);
    if (gType == 0) {
      // 效果A：水平错位
      u8g2.drawStr(6, 35, timeBuf); // 重影
    } else if (gType == 1) {
      // 效果B：区域反色 (XOR)
      u8g2.setDrawColor(2); // 2 = XOR 模式
      u8g2.drawBox(random(0, 128), 5, random(10, 50), 30);
      u8g2.setDrawColor(1); // 恢复正常
    } else {
      // 效果C：随机黑块遮挡
      u8g2.setDrawColor(0);
      u8g2.drawBox(random(0, 128), random(0, 40), 20, 2);
      u8g2.setDrawColor(1);
    }
  }

  // 4. 秒数锯齿波 (底部)
  // 在下方区域绘制波形
  drawSawtooth(50, 46, 78, 16, s);

  static bool sec_flag = false;
  // 5. 秒针 (带动画) 
  // 检测秒数变化，触发动画
  if (s != last_s) {
    glitch_frames = 4; // 持续 4 帧的故障效果
    last_s = s;

    sec_flag = !sec_flag;
    
  }
  if(sec_flag)
  {
    int x = 58; 
    
    // 上方点：用两个三角形拼成一个倾斜的矩形
    // 坐标逻辑：(x,y), (x+w,y), (x-skew, y+h)
    u8g2.drawTriangle(x, 15, x+5, 15, x-2, 21); // 上半部
    u8g2.drawTriangle(x+5, 15, x+3, 21, x-2, 21); // 下半部
    
    // 下方点
    u8g2.drawTriangle(x, 27, x+2, 27, x-5, 33);
    u8g2.drawTriangle(x+5, 27, x+0, 33, x-5, 33);
  }
  // 设置粗体大字 (20号数字字体)
  u8g2.setFont(u8g2_font_logisoso20_tn); 
  
  char secBuf[3];
  sprintf(secBuf, "%02d", s);
  
  // 默认位置
  int sec_x = 11; 
  int sec_y = 62;

  // 动画逻辑
  if (glitch_frames > 0) {
    glitch_frames--;
    // 随机抖动位置
    sec_x += random(-2, 3);
    sec_y += random(-2, 3);
    
    // 偶尔反色显示 (闪烁)
    if (random(0, 2) == 0) {
       u8g2.setDrawColor(2); // XOR 模式
       u8g2.drawBox(sec_x - 2, sec_y - 24, 30, 26); // 画个背景框反色
       u8g2.setDrawColor(1); // 恢复
    }
  }

  // 绘制秒数 (去掉 SEC 前缀，只要数字)
  u8g2.drawStr(sec_x, sec_y, secBuf);
  
  // 6. 顶部连接状态 (简易代码风)
  u8g2.setFont(u8g2_font_profont10_tf);
  if (Bluefruit.connected()) {
    u8g2.drawStr(2, 8, "[LINK_OK]");
  } else {
    // 闪烁效果
    if ((millis() / 500) % 2 == 0) u8g2.drawStr(2, 8, "[SCANNING]");
  }

  // 7. 随机数据流装饰 (右上角)
  if (millis() % 200 < 50) { // 限制刷新频率，不用太快
    drawRandomHex(100, 8);
    drawRandomHex(100, 16);

    drawRandomHex(random(0, 127), random(0, 63));
    drawRandomHex(random(0, 127), random(0, 63));
  }

  u8g2.sendBuffer();
}

// --- 以下 BLE 逻辑保持原样 ---
void setupBLE() {
  Bluefruit.begin(1, 0); 
  Bluefruit.setName("Cyber_Clock"); // 改个名字

  Bluefruit.Security.begin(); 
  Bluefruit.Security.setIOCaps(false, false, false); 
  Bluefruit.Security.setPairCompleteCallback(pair_callback); 

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  ctsSvc.begin();
  ctcChar.setNotifyCallback(notify_callback);
  ctcChar.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); 
  Bluefruit.Advertising.start(0); 
}

void connect_callback(uint16_t conn_handle) {
  bondTimer.start();
  if (ctsSvc.discover(conn_handle)) {
    if (ctcChar.discover()) {
      ctcChar.enableNotify(); 
      uint8_t data[10];
      if (ctcChar.read(data, sizeof(data)) >= 7) updateTimeFromData(data);
    }
  }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  bondTimer.stop(); 
}

void notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len >= 7) updateTimeFromData(data);
}

void updateTimeFromData(uint8_t* data) {
  cts_time_t* t = (cts_time_t*)data;
  setTime(t->hour, t->minute, t->second, t->day, t->month, t->year);
}