#include "calendar_overlay.h"

#include "gdem042f86.h"

#include <stddef.h>

#define COLOR_BLACK  GDEM042F86_BLACK
#define COLOR_WHITE  GDEM042F86_WHITE
#define COLOR_RED    GDEM042F86_RED
#define GRID_TOP     72U
#define GRID_CELL_W  40U
#define GRID_CELL_H  38U

static const uint8_t *Glyph(char character)
{
  static const uint8_t blank[7] = {0U, 0U, 0U, 0U, 0U, 0U, 0U};
  static const uint8_t digits[10][7] = {
    {0x0EU,0x11U,0x13U,0x15U,0x19U,0x11U,0x0EU},
    {0x04U,0x0CU,0x04U,0x04U,0x04U,0x04U,0x0EU},
    {0x0EU,0x11U,0x01U,0x02U,0x04U,0x08U,0x1FU},
    {0x1EU,0x01U,0x01U,0x0EU,0x01U,0x01U,0x1EU},
    {0x02U,0x06U,0x0AU,0x12U,0x1FU,0x02U,0x02U},
    {0x1FU,0x10U,0x10U,0x1EU,0x01U,0x01U,0x1EU},
    {0x0EU,0x10U,0x10U,0x1EU,0x11U,0x11U,0x0EU},
    {0x1FU,0x01U,0x02U,0x04U,0x08U,0x08U,0x08U},
    {0x0EU,0x11U,0x11U,0x0EU,0x11U,0x11U,0x0EU},
    {0x0EU,0x11U,0x11U,0x0FU,0x01U,0x01U,0x0EU}
  };
  static const uint8_t minus[7] = {0U,0U,0U,0x1FU,0U,0U,0U};
  static const uint8_t dot[7] = {0U,0U,0U,0U,0U,0x06U,0x06U};
  static const uint8_t c[7] = {0x0EU,0x11U,0x10U,0x10U,0x10U,0x11U,0x0EU};
  static const uint8_t s[7] = {0x0FU,0x10U,0x10U,0x0EU,0x01U,0x01U,0x1EU};
  static const uint8_t y[7] = {0x11U,0x11U,0x0AU,0x04U,0x04U,0x04U,0x04U};
  static const uint8_t n[7] = {0x11U,0x19U,0x15U,0x13U,0x11U,0x11U,0x11U};
  static const uint8_t percent[7] = {0x19U,0x1AU,0x04U,0x08U,0x16U,0x06U,0U};

  if ((character >= '0') && (character <= '9'))
  {
    return digits[(unsigned int)(character - '0')];
  }
  switch (character)
  {
    case '-': return minus;
    case '.': return dot;
    case 'C': return c;
    case 'S': return s;
    case 'Y': return y;
    case 'N': return n;
    case '%': return percent;
    default: return blank;
  }
}

static void SetPixel(uint8_t line[CALENDAR_OVERLAY_LINE_BYTES], uint16_t x,
                     uint8_t color)
{
  uint16_t byte_index;
  uint8_t shift;

  if (x >= GDEM042F86_WIDTH)
  {
    return;
  }
  byte_index = x / 4U;
  shift = (uint8_t)((3U - (x % 4U)) * 2U);
  line[byte_index] = (uint8_t)((line[byte_index] & ~(0x03U << shift))
                               | ((color & 0x03U) << shift));
}

static void FillSpan(uint8_t line[CALENDAR_OVERLAY_LINE_BYTES],
                     uint16_t x0, uint16_t x1, uint8_t color)
{
  uint16_t x;
  for (x = x0; (x <= x1) && (x < GDEM042F86_WIDTH); ++x)
  {
    SetPixel(line, x, color);
  }
}

static void DrawTextLine(uint8_t line[CALENDAR_OVERLAY_LINE_BYTES],
                         uint16_t y, uint16_t x, uint16_t top,
                         const char *text, uint8_t scale, uint8_t color)
{
  uint16_t glyph_row;

  if ((text == NULL) || (scale == 0U) || (y < top))
  {
    return;
  }
  glyph_row = (uint16_t)((y - top) / scale);
  if (glyph_row >= 7U)
  {
    return;
  }
  while (*text != '\0')
  {
    const uint8_t *glyph = Glyph(*text++);
    uint8_t column;
    for (column = 0U; column < 5U; ++column)
    {
      if ((glyph[glyph_row] & (uint8_t)(1U << (4U - column))) != 0U)
      {
        uint8_t dx;
        for (dx = 0U; dx < scale; ++dx)
        {
          SetPixel(line, (uint16_t)(x + column * scale + dx), color);
        }
      }
    }
    x = (uint16_t)(x + 6U * scale);
  }
}

static bool IsLeapYear(uint16_t year)
{
  return ((year % 4U) == 0U)
         && (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

static uint8_t DaysInMonth(uint16_t year, uint8_t month)
{
  static const uint8_t days[] = {
    31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U
  };
  if ((month == 0U) || (month > 12U)) return 0U;
  if ((month == 2U) && IsLeapYear(year)) return 29U;
  return days[month - 1U];
}

static void BuildDateText(const RTC_CalendarDateTime *date_time, char text[11])
{
  uint16_t year = date_time->year;
  text[0] = (char)('0' + ((year / 1000U) % 10U));
  text[1] = (char)('0' + ((year / 100U) % 10U));
  text[2] = (char)('0' + ((year / 10U) % 10U));
  text[3] = (char)('0' + (year % 10U));
  text[4] = '-';
  text[5] = (char)('0' + (date_time->month / 10U));
  text[6] = (char)('0' + (date_time->month % 10U));
  text[7] = '-';
  text[8] = (char)('0' + (date_time->day / 10U));
  text[9] = (char)('0' + (date_time->day % 10U));
  text[10] = '\0';
}

static void BuildTemperatureText(const CalendarOverlay_Context *context,
                                 char text[8])
{
  int32_t value;
  uint32_t magnitude;
  uint8_t index = 0U;

  if (!context->temperature_valid)
  {
    text[0] = '-'; text[1] = '-'; text[2] = '.';
    text[3] = '-'; text[4] = 'C'; text[5] = '\0';
    return;
  }
  value = context->temperature_centi_c;
  if (value < 0)
  {
    text[index++] = '-';
    magnitude = (uint32_t)(-value);
  }
  else
  {
    magnitude = (uint32_t)value;
  }
  magnitude = (magnitude + 5U) / 10U; /* display tenths of a degree */
  if ((magnitude / 10U) >= 10U)
  {
    text[index++] = (char)('0' + ((magnitude / 100U) % 10U));
  }
  text[index++] = (char)('0' + ((magnitude / 10U) % 10U));
  text[index++] = '.';
  text[index++] = (char)('0' + (magnitude % 10U));
  text[index++] = 'C';
  text[index] = '\0';
}

static void BuildHumidityText(const CalendarOverlay_Context *context,
                              char text[8])
{
  uint32_t value;
  uint8_t index = 0U;

  if (!context->humidity_valid)
  {
    text[0] = '-'; text[1] = '-'; text[2] = '.';
    text[3] = '-'; text[4] = '%'; text[5] = '\0';
    return;
  }
  value = (context->humidity_centi_percent + 5U) / 10U;
  if (value > 1000U)
  {
    value = 1000U;
  }
  if ((value / 10U) >= 100U)
  {
    text[index++] = '1';
    text[index++] = '0';
  }
  else if ((value / 10U) >= 10U)
  {
    text[index++] = (char)('0' + ((value / 100U) % 10U));
  }
  text[index++] = (char)('0' + ((value / 10U) % 10U));
  text[index++] = '.';
  text[index++] = (char)('0' + (value % 10U));
  text[index++] = '%';
  text[index] = '\0';
}

void CalendarOverlay_ApplyLine(uint8_t line[CALENDAR_OVERLAY_LINE_BYTES],
                               uint16_t y,
                               const CalendarOverlay_Context *context)
{
  char date_text[11];
  char temperature_text[8];
  char humidity_text[8];
  uint8_t first_weekday;
  uint8_t day;
  uint8_t month_days;

  if ((line == NULL) || (context == NULL) || (y >= GDEM042F86_HEIGHT))
  {
    return;
  }

  if (y < 48U)
  {
    FillSpan(line, 0U, 278U, COLOR_WHITE);
    BuildDateText(&context->date_time, date_text);
    BuildTemperatureText(context, temperature_text);
    BuildHumidityText(context, humidity_text);
    DrawTextLine(line, y, 18U, 16U, date_text, 2U, COLOR_BLACK);
    DrawTextLine(line, y, 190U, 5U, temperature_text, 2U, COLOR_BLACK);
    DrawTextLine(line, y, 190U, 27U, humidity_text, 2U, COLOR_BLACK);
  }

  month_days = DaysInMonth(context->date_time.year,
                          context->date_time.month);
  first_weekday = (uint8_t)((((int16_t)context->date_time.weekday
                              - (int16_t)((context->date_time.day - 1U) % 7U)
                              - 1 + 7) % 7) + 1);
  for (day = 1U; day <= month_days; ++day)
  {
    uint8_t cell = (uint8_t)(first_weekday - 1U + day - 1U);
    uint8_t column = (uint8_t)(cell % 7U);
    uint8_t row = (uint8_t)(cell / 7U);
    uint16_t x0 = (uint16_t)column * GRID_CELL_W;
    uint16_t y0 = GRID_TOP + (uint16_t)row * GRID_CELL_H;
    char day_text[3];
    uint8_t day_color = column >= 5U ? COLOR_RED : COLOR_BLACK;

    if (day == context->date_time.day)
    {
      uint16_t x1 = (uint16_t)(x0 + GRID_CELL_W - 3U);
      uint16_t y1 = (uint16_t)(y0 + GRID_CELL_H - 3U);
      if ((y == (uint16_t)(y0 + 2U)) || (y == (uint16_t)(y0 + 3U))
          || (y == y1) || (y == (uint16_t)(y1 - 1U)))
      {
        FillSpan(line, (uint16_t)(x0 + 2U), x1, COLOR_RED);
      }
      else if ((y > (uint16_t)(y0 + 3U)) && (y < (uint16_t)(y1 - 1U)))
      {
        FillSpan(line, (uint16_t)(x0 + 2U), (uint16_t)(x0 + 3U),
                 COLOR_RED);
        FillSpan(line, (uint16_t)(x1 - 1U), x1, COLOR_RED);
      }
      day_color = COLOR_RED;
    }
    day_text[0] = day >= 10U ? (char)('0' + day / 10U)
                             : (char)('0' + day);
    day_text[1] = day >= 10U ? (char)('0' + day % 10U) : '\0';
    day_text[2] = '\0';
    DrawTextLine(line, y, (uint16_t)(x0 + 7U), (uint16_t)(y0 + 8U),
                 day_text, 2U, day_color);
  }

  if (context->android_content_stale && (y < GDEM042F86_HEIGHT))
  {
    FillSpan(line, 281U, 399U, COLOR_WHITE);
    DrawTextLine(line, y, 307U, 145U, "SYNC", 3U, COLOR_RED);
  }
}
