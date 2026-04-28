#include <my_main.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

//======================= 电池参数配置 =======================
#define BAT_FULL_MV         4200
#define BAT_EMPTY_MV        3000
#define BAT_FILTER_COUNT    5
#define BAT_MAX_DELTA_MV    150

//======================= 界面布局 =======================
#define BATTERY_CARD_X              250
#define BATTERY_CARD_SHOW_Y         -13
#define BATTERY_CARD_WIDTH          43
#define BATTERY_CARD_WIDTH_CHARGING (56 + 12)
#define BATTERY_CARD_HEIGHT         33

extern "C" const lv_img_dsc_t bolt;

//======================= 全局UI对象 =======================
static MyCard card_Battery;
static lv_obj_t *img_bolt = NULL;
static lv_obj_t *bat_grid[5];  // 5格电量指示

//======================= 状态变量 =======================
static bool inited = false;
static int16_t filter_buffer[BAT_FILTER_COUNT] = {0};
static uint8_t filter_index = 0;
static int16_t last_stable_voltage = 3500;
static bool last_charging = false;

//======================= 函数声明 =======================
static int16_t battery_get_filtered_voltage(void);
static uint8_t battery_voltage_to_level(int16_t mv);
static lv_color_t battery_get_color(int16_t mv, bool charging);
static void battery_card_construct(lv_obj_t *parent);
static void battery_update_grid(uint8_t level, lv_color_t color);

//================================================================
// 滤波：稳定电压
//================================================================
static int16_t battery_get_filtered_voltage(void)
{
    int16_t raw = PowerManager_getBatteryVoltage();
    if (raw <= 0)
        return last_stable_voltage;

    filter_buffer[filter_index++] = raw;
    if (filter_index >= BAT_FILTER_COUNT)
        filter_index = 0;

    int32_t sum = 0;
    for (uint8_t i = 0; i < BAT_FILTER_COUNT; i++)
    {
        sum += filter_buffer[i];
    }
    int16_t avg = sum / BAT_FILTER_COUNT;

    if (abs(avg - last_stable_voltage) > BAT_MAX_DELTA_MV)
    {
        return last_stable_voltage;
    }

    last_stable_voltage = avg;
    return last_stable_voltage;
}

//================================================================
// 电压 → 5档电量等级 (0~5)
//================================================================
static uint8_t battery_voltage_to_level(int16_t mv)
{
    if (mv >= 4100) return 5;
    if (mv >= 3850) return 4;
    if (mv >= 3600) return 3;
    if (mv >= 3300) return 2;
    if (mv >= 3100) return 1;
    return 0;
}

//================================================================
// 电池颜色
//================================================================
static lv_color_t battery_get_color(int16_t mv, bool charging)
{
    if (charging)
        return lv_color_make(0, 180, 255);   // 充电蓝 
    if (mv <= 3100)
        return lv_color_make(255, 60, 60);   // 低电红
    return lv_color_make(0, 220, 80);       // 默认绿
}

//================================================================
// 更新5格显示
//================================================================
static void battery_update_grid(uint8_t level, lv_color_t color)
{
    for (int i = 0; i < 5; i++)
    {
        if (i < level)
        {
            lv_obj_set_style_bg_color(bat_grid[i], color, 0);
            lv_obj_set_style_bg_opa(bat_grid[i], 255, 0);
        }
        else
        {
            lv_obj_set_style_bg_opa(bat_grid[i], 0, 0);
        }
    }
}

//================================================================
// UI构建：5格电池 + 充电图标
//================================================================
static void battery_card_construct(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(parent, 128, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(parent, 0, 0);

    // 5格电量指示
    int grid_w = 6;
    int grid_h = 18;
    int x_start = -12;
    int y_pos = 1;

    for (int i = 0; i < 5; i++)
    {
        bat_grid[i] = lv_obj_create(parent);
        lv_obj_set_size(bat_grid[i], grid_w, grid_h);
        lv_obj_set_pos(bat_grid[i], x_start + i * (grid_w + 2), y_pos);
        lv_obj_set_style_border_width(bat_grid[i], 0, 0);
        lv_obj_set_style_radius(bat_grid[i], 1, 0);
        lv_obj_set_style_bg_color(bat_grid[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bat_grid[i], 0, 0);
    }

    // 充电闪电图标
    img_bolt = lv_img_create(parent);
    lv_img_set_src(img_bolt, &bolt);
    lv_obj_set_pos(img_bolt, 36, 2);
    lv_obj_set_style_opa(img_bolt, 0, 0);
}

//================================================================
// 电池主函数（已删除所有 current_mode 判断）
//================================================================
void battery_card_check()
{
    static int cnt = 0;

    // 只初始化一次（永久显示，不再隐藏）
    if (!inited)
    {
        inited = true;
        LOCKLV();
        // 创建卡片
        if (card_Battery.obj == NULL || !lv_obj_is_valid(card_Battery.obj))
        {
            card_Battery.create(lv_layer_sys(),
                BATTERY_CARD_X, BATTERY_CARD_SHOW_Y,
                BATTERY_CARD_WIDTH, BATTERY_CARD_HEIGHT,
                LV_ALIGN_TOP_LEFT);
            card_Battery.show(CARD_ANIM_NONE);
            battery_card_construct(card_Battery.obj);
        }
        // 固定显示位置
        card_Battery.move(BATTERY_CARD_X, BATTERY_CARD_SHOW_Y);
        UNLOCKLV();
        cnt = 20;
    }

    // 定时刷新
    if (++cnt >= 20)
    {
        cnt = 0;

        int16_t voltage_mv = battery_get_filtered_voltage();
        uint8_t level = battery_voltage_to_level(voltage_mv);
        bool charging = PowerManager_isCharging();
        lv_color_t color = battery_get_color(voltage_mv, charging);

        LOCKLV();
        battery_update_grid(level, color);
        UNLOCKLV();

        // 充电状态动画
        if (charging != last_charging)
        {
            last_charging = charging;
            LOCKLV();
            if (charging)
            {
                card_Battery.size(BATTERY_CARD_WIDTH_CHARGING, BATTERY_CARD_HEIGHT);
                lv_obj_fade_in(img_bolt, 500, 0);
            }
            else
            {
                card_Battery.size(BATTERY_CARD_WIDTH, BATTERY_CARD_HEIGHT);
                lv_obj_fade_out(img_bolt, 300, 0);
            }
            UNLOCKLV();
        }
    }
}
