#include <hardware/i2c.h>
#include <hardware/pio.h>
#include <pico/stdlib.h>
#include <time.h>

#include <chrono>
#include <cstdio>
#include <vector>

#include "TimeDefs.h"
#include "ws2812.pio.h"

extern "C" {
#include "ds3231.h"
}

// ===

struct RTC_DS3231 {
  struct DateTime {
    DateTime(const ds3231_data_t *data) : _data{data} {}
    auto hour() const { return _data->hours; }
    auto minute() const { return _data->minutes; }
    auto sec() const { return _data->seconds; }

   private:
    const ds3231_data_t *_data;
  };

  RTC_DS3231() {
    uint8_t sda_pin = 24;
    uint8_t scl_pin = 25;

    ds3231_init(&_rtc, i2c_default, DS3231_DEVICE_ADRESS, AT24C32_EEPROM_ADRESS_0);

    gpio_init(sda_pin);
    gpio_init(scl_pin);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    i2c_init(_rtc.i2c, 400 * 1000);
  }

  void setToBuildTime() {
    constexpr uint8_t month_codes[] = {0, 3, 3, 6, 1, 4, 6, 2, 5, 0, 3, 5};
    _data = {
        .seconds = BUILD_TIME_SECONDS_INT + 5,
        .minutes = BUILD_TIME_MINUTES_INT,
        .hours = BUILD_TIME_HOURS_INT,
        .am_pm = false,
        .day = uint8_t(((BUILD_DATE_YEAR_INT % 100 + (BUILD_DATE_YEAR_INT % 100) % 4) % 7 +
                        month_codes[BUILD_DATE_MONTH_INT - 1] + 6 + BUILD_DATE_DAY_INT -
                        (!(BUILD_DATE_YEAR_INT % 400) ||
                                 !(BUILD_DATE_YEAR_INT % 4) && BUILD_DATE_YEAR_INT % 100
                             ? 1
                             : 0)) %
                       7),
        .date = BUILD_DATE_DAY_INT,
        .month = BUILD_DATE_MONTH_INT,
        .century = (BUILD_DATE_YEAR_INT / 100L),
        .year = BUILD_DATE_YEAR_INT % 100,
    };
    ds3231_configure_time(&_rtc, &_data);
  }
  void adjust(std::chrono::minutes m) {
    assert(std::abs(m.count()) < 60);
    _data.minutes += m.count();
    ds3231_configure_time(&_rtc, &_data);
  }
  bool begin() {
    auto err = ds3231_read_current_time(&_rtc, &_data);
    return !err;
  }
  bool lostPower() {
    auto val = ds3231_check_oscillator_stop_flag(&_rtc);
    return val == 1;
  }
  DateTime now() const {
    ds3231_read_current_time(&_rtc, &_data);
    return DateTime(&_data);
  }

 private:
  mutable ds3231_t _rtc;
  mutable ds3231_data_t _data;
};

struct TimeSpan {
  TimeSpan(int, int, int, int){};
};

class Adafruit_NeoPixel {
 public:
  class Color {
   public:
    Color() = default;
    Color(uint8_t r, uint8_t g, uint8_t b)
        : _val{((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b)} {}

    uint32_t operator*() const { return _val; }

   private:
    uint32_t _val = 0;
  };

  Adafruit_NeoPixel(int num_leds, int led_pin, int sm = 0) : _sm{sm} {
    _data.resize(num_leds);
    ws2812_program_init(pio0, _sm, s_ws2812_program_offset, led_pin, 800000, false);
  }

  void setPixelColor(int i, const Color &c) { _data[i] = c; }

  void begin() {}
  void show() {
    for (auto c : _data) {
      pio_sm_put_blocking(pio0, _sm, *c << 8u);
    }
  }

 private:
  std::vector<Color> _data;
  int _sm = 0;

  static uint s_ws2812_program_offset;
};

uint Adafruit_NeoPixel::s_ws2812_program_offset = pio_add_program(pio0, &ws2812_program);

// ===

#define BUTTON_PIN_1 27
#define BUTTON_PIN_2 28
#define STATUS_PWR_PIN 11
#define STATUS_PIN 12

#define O_CLOCK 201326592
#define FIVE_MINUTES 392
#define TEN_MINUTES 448
#define QUARTER 6
#define TWENTY_MINUTES 432
#define TWENTY_FIVE_MINUTES 440
#define HALF 1
#define PAST 1024
#define TO 512

#define LED_PIN 26
#define LED_NUM 30

const auto LED_ON = Adafruit_NeoPixel::Color(192, 192, 255);
const auto LED_DIM = Adafruit_NeoPixel::Color(12, 12, 12);
const auto LED_OFF = Adafruit_NeoPixel::Color(0, 0, 0);

auto rtc = RTC_DS3231();
auto pixels = Adafruit_NeoPixel(LED_NUM, LED_PIN, 0);
auto status = Adafruit_NeoPixel(1, STATUS_PIN, 1);

uint32_t previous_led = 0;

struct Button {
  volatile int pressed_at = 0;
  volatile int release = 0;
} button;

void gpio_callback(uint gpio, uint32_t events) {
  auto now = to_ms_since_boot(get_absolute_time());

  if (events & GPIO_IRQ_EDGE_FALL && button.pressed_at == 0) {
    button.pressed_at = now;
  } else if (events & GPIO_IRQ_EDGE_RISE && now - button.pressed_at > 50) {
    button.release = gpio;
  }
}

void setupButtons() {
  gpio_init(BUTTON_PIN_1);
  gpio_set_dir(BUTTON_PIN_1, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_1);

  gpio_init(BUTTON_PIN_2);
  gpio_set_dir(BUTTON_PIN_2, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_2);

  auto mask = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
  gpio_set_irq_enabled_with_callback(BUTTON_PIN_1, mask, true, &gpio_callback);
  gpio_set_irq_enabled(BUTTON_PIN_2, mask, true);
}

bool setup() {
  gpio_init(STATUS_PWR_PIN);
  gpio_set_dir(STATUS_PWR_PIN, GPIO_OUT);
  gpio_put(STATUS_PWR_PIN, 1);

  setupButtons();

  pixels.begin();
  pixels.show();  // clear

  if (!rtc.begin()) {
    printf("Couldn't find RTC");
    return false;
  }

  if (rtc.lostPower()) {
    printf("RTC lost power, lets set the time!");
    rtc.setToBuildTime();

    status.setPixelColor(0, Adafruit_NeoPixel::Color(255, 255, 0));
    status.show();
    sleep_ms(250);
    status.setPixelColor(0, {});
    status.show();
  }
  return true;
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
  if (button.release) {
    using namespace std::chrono_literals;

    if (button.release == BUTTON_PIN_1) {
      rtc.adjust(5min);
    } else if (button.release == BUTTON_PIN_2) {
      rtc.adjust(-5min);
    }
    button.pressed_at = 0;
    button.release = 0;
  }

  const auto now = rtc.now();
  // hour (0-23) minute (0-59)
  auto led = getLED(now.hour(), now.minute());

  if (led == previous_led) {
    sleep_ms(100);
    return;
  }
  previous_led = led;

  printf("%d:%d:%d\n", now.hour(), now.minute(), now.sec());

  // Night mode, 23-08
  // const auto night_mode = now.hour() % 23 < 8;
  for (auto i = 0; i < LED_NUM; ++i) {
    pixels.setPixelColor(i, led & 1 ? LED_ON : LED_OFF);
    led >>= 1;
  }
  pixels.show();
}

int main() {
  stdio_init_all();

  if (!setup()) {
    return 1;
  }

  for (;;) {
    loop();
  }
}
