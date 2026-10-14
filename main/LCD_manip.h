
#include "common.h"
// #include "esp_efuse.h"
#include "esp_lcd_jd9365.h"
#include <sys/param.h>
#include <sys/lock.h>

//lcd macros and definitions 
#define EXAMPLE_MIPI_DSI_LANE_NUM          2    // 2 data lanes
#define EXAMPLE_MIPI_DSI_LANE_BITRATE_MBPS 1500 // 1Gbps
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN       3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL           1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL          !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT                23
#define EXAMPLE_PIN_NUM_LCD_RST                 27
// #define EXAMPLE_PIN_NUM_REFRESH_MONITOR         20 





void enable_dsi_phy_power(void);
void init_lcd_backlight(void);
void set_lcd_backlight(uint32_t level);
void init_refresh_monitor_io(void);
esp_lcd_panel_handle_t init_lcd(void);
