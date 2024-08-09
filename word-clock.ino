#include <Adafruit_NeoPixel.h>
#include <RTClib.h>
#include <Wire.h>

#define BUTTON_HOUR_PIN 2
#define BUTTON_MINUTE_PIN 3

enum ButtonState {
  kIdle = HIGH,
  kAdd = LOW,
};

#define O_CLOCK 201326592
#define FIVE_MINUTES 392
#define TEN_MINUTES 448
#define QUARTER 6
#define TWENTY_MINUTES 432
#define TWENTY_FIVE_MINUTES 440
#define HALF 1
#define PAST 1024
#define TO 512

const auto k5Minutes = TimeSpan(0, 0, 5, 0);
const auto k1Hour = TimeSpan(0, 1, 0, 0);

#define LED_PIN 6
#define LED_NUM 30

const auto LED_ON = Adafruit_NeoPixel::Color(192, 192, 255);
const auto LED_DIM = Adafruit_NeoPixel::Color(12, 12, 12);
const auto LED_OFF = Adafruit_NeoPixel::Color(0, 0, 0);

RTC_DS3231 rtc;
Adafruit_NeoPixel pixels(LED_NUM, LED_PIN);

volatile ButtonState add_hour;
volatile ButtonState add_minute;

uint32_t previous_led = 0;

void addHour() { add_hour = kAdd; }
void addMinute() { add_minute = kAdd; }

void setupRtc() {
  pinMode(A2, OUTPUT);
  pinMode(A3, OUTPUT);
  digitalWrite(A2, LOW);
  digitalWrite(A3, HIGH);
}

void setupButtons() {
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  digitalWrite(4, LOW);
  digitalWrite(5, LOW);

  pinMode(BUTTON_HOUR_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MINUTE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_HOUR_PIN), addHour, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_MINUTE_PIN), addMinute, FALLING);
}

void setup() {
  setupRtc();
  setupButtons();

  add_hour = kIdle;
  add_minute = kIdle;

  pixels.begin();

  Serial.begin(9600);

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (true)
      continue;
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

int hourFormat12(int h24) {
  const auto mod = h24 % 12;
  return mod == 0 ? 12 : mod;
}

uint32_t getMinute(int m) {
  if (m < 5) {
    return O_CLOCK;
  } else if (m < 10 || m >= 55) {
    return FIVE_MINUTES;
  } else if (m < 15 || m >= 50) {
    return TEN_MINUTES;
  } else if (m < 20 || m >= 45) {
    return QUARTER;
  } else if (m < 25 || m >= 40) {
    return TWENTY_MINUTES;
  } else if (m < 30 || m >= 35) {
    return TWENTY_FIVE_MINUTES;
  } else {
    return HALF;
  }
}
uint32_t getAffix(int m) { return m < 5 ? 0 : m < 35 ? PAST : TO; }
uint32_t getHour(int h) {
  // clang-format off
  switch (hourFormat12(h)) {
    case 2: return 1L << (11 + 0);
    case 4: return 1L << (11 + 1);
    case 1: return 1L << (11 + 2);

    case 3: return 3L << (11 + 3);
    case 6: return 1L << (11 + 5);
    case 5: return 1L << (11 + 6);

    case 11: return 3L << (11 + 7);
    case 12: return 3L << (11 + 9);

    case 9: return 1L << (11 + 11);
    case 10: return 1L << (11 + 12);
    case 8: return 3L << (11 + 13);

    case 7: return 3L << (11 + 17);

    default: return 0;
  };
  // clang-format on
}
uint32_t getLED(int h, int m) {
  const auto affix = getAffix(m);
  h += affix == TO;
  return getMinute(m) | affix | getHour(h);
}

void loop() {
  if (add_minute == kAdd && digitalRead(BUTTON_MINUTE_PIN) == kAdd) {
    add_minute = kIdle;
    const auto now = rtc.now().unixtime();
    const auto current_time = DateTime(now - (now % k5Minutes.totalseconds()));
    rtc.adjust(current_time + k5Minutes);
  }
  if (add_hour == kAdd && digitalRead(BUTTON_HOUR_PIN) == kAdd) {
    add_hour = kIdle;
    rtc.adjust(rtc.now() + k1Hour);
  }

  const auto now = rtc.now();
  // hour (0-23) minute (0-59)
  auto led = getLED(now.hour(), now.minute());

  if (led == previous_led) {
    return;
  }
  previous_led = led;

  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.print(now.second(), DEC);
  Serial.println();

  // Night mode, 23-08
  // const auto night_mode = now.hour() % 23 < 8;
  for (auto i = 0; i < LED_NUM; ++i) {
    pixels.setPixelColor(i, led & 1 ? LED_ON : LED_OFF);
    led >>= 1;
  }
  pixels.show();
}
