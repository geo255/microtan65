#ifndef __RTC_H__
#define __RTC_H__

#include <stdint.h>

extern int rtc_initialise(uint8_t bank, uint16_t address, uint16_t param,
                          char* identifier);
extern void rtc_reset(uint8_t bank, uint16_t address);
extern void rtc_update(void);

extern int64_t rtc_get_offset_seconds(void);
extern void rtc_set_offset_seconds(int64_t offset);

#endif // __RTC_H__
