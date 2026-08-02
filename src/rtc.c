#define _POSIX_C_SOURCE 200809L

#include "rtc.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cpu_6502.h"
#include "function_return_codes.h"
#include "system.h"

#define RTC_REGISTER_COUNT 16
#define RTC_TENTHS         1
#define RTC_SECONDS_UNITS  2
#define RTC_SECONDS_TENS   3
#define RTC_MINUTES_UNITS  4
#define RTC_MINUTES_TENS   5
#define RTC_HOURS_UNITS    6
#define RTC_HOURS_TENS     7
#define RTC_DAY_UNITS      8
#define RTC_DAY_TENS       9
#define RTC_WEEKDAY        10
#define RTC_MONTH_UNITS    11
#define RTC_MONTH_TENS     12
#define RTC_YEAR_STATUS    13
#define RTC_START_STOP     14
#define RTC_INTERRUPT      15

#define RTC_INTERRUPT_REPEAT 0x08
#define RTC_INTERRUPT_60S    0x04
#define RTC_INTERRUPT_5S     0x02
#define RTC_INTERRUPT_HALF_S 0x01

#define NANOSECONDS_PER_SECOND 1000000000LL
#define RTC_INTERRUPT_DELAY_NS  16600000LL

static uint16_t rtc_base_address;
static int64_t rtc_offset_seconds;
static int64_t acknowledged_decisecond;
static bool rtc_running;
static struct tm stopped_time;
static int stopped_tenth;
static int weekday_offset;
static uint8_t year_status;
static uint8_t interrupt_mode;
static uint8_t interrupt_status;
static int interrupt_read_count;
static bool interrupt_pending;
static int64_t interrupt_deadline_ns;

static void rtc_host_time(struct timespec* value) {
  clock_gettime(CLOCK_REALTIME, value);
}

static int64_t rtc_monotonic_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * NANOSECONDS_PER_SECOND + now.tv_nsec;
}

static int64_t rtc_interrupt_period_ns(uint8_t mode) {
  switch (mode & 0x07) {
    case RTC_INTERRUPT_HALF_S:
      return NANOSECONDS_PER_SECOND / 2 + RTC_INTERRUPT_DELAY_NS;
    case RTC_INTERRUPT_5S:
      return 5 * NANOSECONDS_PER_SECOND + RTC_INTERRUPT_DELAY_NS;
    case RTC_INTERRUPT_60S:
      return 60 * NANOSECONDS_PER_SECOND + RTC_INTERRUPT_DELAY_NS;
    default:
      return 0;
  }
}

static void rtc_schedule_interrupt(void) {
  int64_t period = rtc_interrupt_period_ns(interrupt_mode);
  interrupt_deadline_ns = period == 0 ? 0 : rtc_monotonic_ns() + period;
}

static void rtc_clear_interrupt(void) {
  interrupt_pending = false;
  interrupt_status = 0;
  interrupt_read_count = 0;
}

static int64_t rtc_current_decisecond(void) {
  struct timespec now;
  rtc_host_time(&now);
  return ((int64_t)now.tv_sec + rtc_offset_seconds) * 10 +
         now.tv_nsec / 100000000L;
}

static void rtc_current_time(struct tm* value, int* tenth) {
  if (!rtc_running) {
    *value = stopped_time;
    *tenth = stopped_tenth;
    return;
  }

  struct timespec now;
  rtc_host_time(&now);
  time_t adjusted = (time_t)((int64_t)now.tv_sec + rtc_offset_seconds);
  localtime_r(&adjusted, value);
  *tenth = (int)(now.tv_nsec / 100000000L);
}

static int rtc_hardware_weekday(const struct tm* value) {
  int weekday = ((value->tm_wday + 6) % 7) + 1;
  return ((weekday - 1 + weekday_offset) % 7) + 1;
}

static uint8_t rtc_digit_value(uint8_t reg) {
  struct tm value;
  int tenth;
  rtc_current_time(&value, &tenth);

  switch (reg) {
    case RTC_TENTHS:
      return (uint8_t)tenth;
    case RTC_SECONDS_UNITS:
      return (uint8_t)(value.tm_sec % 10);
    case RTC_SECONDS_TENS:
      return (uint8_t)(value.tm_sec / 10);
    case RTC_MINUTES_UNITS:
      return (uint8_t)(value.tm_min % 10);
    case RTC_MINUTES_TENS:
      return (uint8_t)(value.tm_min / 10);
    case RTC_HOURS_UNITS:
      return (uint8_t)(value.tm_hour % 10);
    case RTC_HOURS_TENS:
      return (uint8_t)(value.tm_hour / 10);
    case RTC_DAY_UNITS:
      return (uint8_t)(value.tm_mday % 10);
    case RTC_DAY_TENS:
      return (uint8_t)(value.tm_mday / 10);
    case RTC_WEEKDAY:
      return (uint8_t)rtc_hardware_weekday(&value);
    case RTC_MONTH_UNITS:
      return (uint8_t)((value.tm_mon + 1) % 10);
    case RTC_MONTH_TENS:
      return (uint8_t)((value.tm_mon + 1) / 10);
    default:
      return 0;
  }
}

static uint8_t rtc_read(uint16_t address) {
  uint8_t reg = (uint8_t)(address - rtc_base_address);
  bool data_changed = false;
  uint8_t value = 0;

  if (rtc_running) {
    int64_t current_decisecond = rtc_current_decisecond();
    if (current_decisecond != acknowledged_decisecond) {
      acknowledged_decisecond = current_decisecond;
      data_changed = true;
    }
  }

  if ((reg >= RTC_TENTHS) && (reg <= RTC_MONTH_TENS)) {
    value = rtc_digit_value(reg);
  } else if (reg == RTC_INTERRUPT) {
    value = interrupt_status;
    interrupt_read_count++;
    if (interrupt_read_count == 3) {
      rtc_clear_interrupt();
      if ((interrupt_mode & RTC_INTERRUPT_REPEAT) != 0) {
        rtc_schedule_interrupt();
      } else {
        interrupt_deadline_ns = 0;
      }
    }
  }

  // Data-changed drives every data line high, regardless of the address read.
  return data_changed ? 0x0F : value;
}

static bool rtc_valid_staged_time(const struct tm* desired, time_t timestamp) {
  struct tm normalised;
  localtime_r(&timestamp, &normalised);
  return (normalised.tm_year == desired->tm_year) &&
         (normalised.tm_mon == desired->tm_mon) &&
         (normalised.tm_mday == desired->tm_mday) &&
         (normalised.tm_hour == desired->tm_hour) &&
         (normalised.tm_min == desired->tm_min) &&
         (normalised.tm_sec == desired->tm_sec);
}

static bool rtc_commit_time(struct tm desired, bool reset_seconds) {
  if (reset_seconds) {
    desired.tm_sec = 0;
  }
  desired.tm_isdst = -1;
  time_t timestamp = mktime(&desired);
  if ((timestamp == (time_t)-1) ||
      !rtc_valid_staged_time(&desired, timestamp)) {
    return false;
  }

  struct timespec now;
  rtc_host_time(&now);
  rtc_offset_seconds = (int64_t)timestamp - (int64_t)now.tv_sec;
  stopped_time = desired;
  stopped_tenth = 0;
  acknowledged_decisecond = rtc_current_decisecond();
  return true;
}

static bool rtc_set_digit(struct tm* value, uint8_t reg, uint8_t digit) {
  if (digit > 9) {
    return false;
  }

  switch (reg) {
    case RTC_MINUTES_UNITS:
      value->tm_min = (value->tm_min / 10) * 10 + digit;
      return true;
    case RTC_MINUTES_TENS:
      if (digit > 5) {
        return false;
      }
      value->tm_min = digit * 10 + value->tm_min % 10;
      return true;
    case RTC_HOURS_UNITS:
      value->tm_hour = (value->tm_hour / 10) * 10 + digit;
      return true;
    case RTC_HOURS_TENS:
      if (digit > 2) {
        return false;
      }
      value->tm_hour = digit * 10 + value->tm_hour % 10;
      return true;
    case RTC_DAY_UNITS:
      value->tm_mday = (value->tm_mday / 10) * 10 + digit;
      return true;
    case RTC_DAY_TENS:
      if (digit > 3) {
        return false;
      }
      value->tm_mday = digit * 10 + value->tm_mday % 10;
      return true;
    case RTC_MONTH_UNITS:
      value->tm_mon = ((value->tm_mon + 1) / 10) * 10 + digit - 1;
      return true;
    case RTC_MONTH_TENS:
      if (digit > 1) {
        return false;
      }
      value->tm_mon = digit * 10 + (value->tm_mon + 1) % 10 - 1;
      return true;
    default:
      return false;
  }
}

static void rtc_stop(void) {
  if (!rtc_running) {
    return;
  }

  rtc_current_time(&stopped_time, &stopped_tenth);
  rtc_running = false;
}

static void rtc_start(void) {
  if (rtc_running) {
    rtc_stop();
  }
  if (rtc_commit_time(stopped_time, true)) {
    rtc_running = true;
    if ((interrupt_mode & 0x07) != 0) {
      rtc_schedule_interrupt();
    }
  }
}

static void rtc_write(uint16_t address, uint8_t value) {
  uint8_t reg = (uint8_t)(address - rtc_base_address);
  value &= 0x0F;

  if ((reg >= RTC_MINUTES_UNITS) && (reg <= RTC_MONTH_TENS) &&
      (reg != RTC_WEEKDAY)) {
    struct tm desired;
    int tenth;
    rtc_current_time(&desired, &tenth);
    if (!rtc_set_digit(&desired, reg, value)) {
      return;
    }

    if (rtc_running) {
      rtc_commit_time(desired, false);
    } else {
      stopped_time = desired;
    }
    return;
  }

  switch (reg) {
    case RTC_WEEKDAY: {
      if ((value < 1) || (value > 7)) {
        break;
      }
      struct tm current;
      int tenth;
      rtc_current_time(&current, &tenth);
      int weekday = ((current.tm_wday + 6) % 7) + 1;
      weekday_offset = (value - weekday + 7) % 7;
      break;
    }

    case RTC_YEAR_STATUS:
      if ((value == 1) || (value == 2) ||
          (value == 4) || (value == 8)) {
        year_status = value;
      }
      break;

    case RTC_START_STOP:
      if (value == 0) {
        rtc_stop();
      } else if (value == 1) {
        rtc_start();
      }
      break;

    case RTC_INTERRUPT:
      interrupt_mode = value;
      rtc_clear_interrupt();
      rtc_schedule_interrupt();
      break;

    default:
      break;
  }
}

int rtc_initialise(uint8_t bank, uint16_t address, uint16_t param,
                   char* identifier) {
  (void)bank;
  (void)param;
  (void)identifier;

  rtc_base_address = address;
  rtc_offset_seconds = 0;
  rtc_running = true;
  stopped_tenth = 0;
  weekday_offset = 0;
  year_status = 1;
  interrupt_mode = 0;
  interrupt_status = 0;
  interrupt_read_count = 0;
  interrupt_pending = false;
  interrupt_deadline_ns = 0;
  acknowledged_decisecond = rtc_current_decisecond();

  return system_register_memory_mapped_device(
    address, (uint16_t)(address + RTC_REGISTER_COUNT - 1),
    rtc_read, rtc_write, false);
}

void rtc_reset(uint8_t bank, uint16_t address) {
  (void)bank;
  (void)address;
  // The battery-backed board is deliberately unaffected by system reset.
}

void rtc_update(void) {
  if (!rtc_running || ((interrupt_mode & 0x07) == 0)) {
    return;
  }

  if (!interrupt_pending && (interrupt_deadline_ns != 0) &&
      (rtc_monotonic_ns() >= interrupt_deadline_ns)) {
    interrupt_pending = true;
    interrupt_status = interrupt_mode & 0x07;
  }

  // The physical output remains asserted until register 15 is read 3 times.
  if (interrupt_pending) {
    cpu_6502_assert_irq();
  }
}

int64_t rtc_get_offset_seconds(void) {
  return rtc_offset_seconds;
}

void rtc_set_offset_seconds(int64_t offset) {
  rtc_offset_seconds = offset;
  acknowledged_decisecond = rtc_current_decisecond();
}
