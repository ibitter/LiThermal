#include <my_main.h>
#include <cmath>
#include <stdint.h>
#include <stdlib.h>

//======================= 电池参数配置 =======================
#define BAT_FULL_MV         4200
#define BAT_EMPTY_MV        3000
#define BAT_LOW_MV          3300
#define BAT_CRIT_MV         3100
#define BAT_FILTER_COUNT    5
#define BAT_MAX_DELTA_MV    150

//======================= 界面布局 =======================
#define BATTERY_CARD_X              250
#define BATTERY_CARD_SHOW_Y         -13
#define BATTERY_CARD_HIDE_Y         -43
#define BATTERY_CARD_WIDTH          56
#define BATTERY_CARD_WIDTH_CHARGING (56 + 12)
#define BATTERY_CARD_HEIGHT         33

extern "C" const lv_img_dsc_t bolt;

//======================= 全局UI对象 =======================
static MyCard card_Battery;
static lv_obj_t *img_bolt = NULL;
//static lv_obj_t *lbl_battery = NULL;
static lv_obj_t *lbl_battery_percent = NULL;

//======================= 状态变量 =======================
static bool expanded = false;
static int16_t filter_buffer[BAT_FILTER_COUNT] = {0};
static uint8_t filter_index = 0;
static int16_t last_stable_voltage = 3500;
static bool last_charging = false;

//======================= 函数声明 =======================
static int16_t battery_get_filtered_voltage(void);
static uint8_t battery_voltage_to_percent(int16_t mv);
static lv_color_t battery_get_text_color(int16_t mv, bool charging);
static void battery_card_construct(lv_obj_t *parent);
static void battery_card_create(void);

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
// 电压转百分比
//================================================================
static uint8_t battery_voltage_to_percent(int16_t mv)
{
    if (mv >= BAT_FULL_MV)  return 100;
    if (mv <= BAT_EMPTY_MV) return 0;
    return (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
}

//================================================================
// 文字颜色
//================================================================
static lv_color_t battery_get_text_color(int16_t mv, bool charging)
{
    if (charging)
        return lv_color_make(0, 180, 255);   // 充电蓝
    if (mv >= 4150)
        return lv_color_make(0, 220, 80);    // 满电绿
    if (mv <= BAT_CRIT_MV)
        return lv_color_make(255, 60, 60);   // 极低红
    if (mv <= BAT_LOW_MV)
        return lv_color_make(255, 200, 0);   // 低电黄
    return lv_color_white();                 // 默认白
}

//================================================================
// UI构建
//================================================================
static void battery_card_construct(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(parent, 128, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(parent, 0, 0);

    // 电压标签（完全保持你原来的位置）
    //lbl_battery = lv_label_create(parent);
    //lv_obj_set_align(lbl_battery, LV_ALIGN_TOP_LEFT);
    //lv_obj_set_x(lbl_battery, -7);
    //lv_label_set_text(lbl_battery, "0.00V");

    // 百分比标签（安全位置）
    lbl_battery_percent = lv_label_create(parent);
    lv_obj_set_align(lbl_battery_percent, LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(lbl_battery_percent, -7);
    lv_label_set_text(lbl_battery_percent, "0%");

    // 闪电图标
    img_bolt = lv_img_create(parent);
    lv_img_set_src(img_bolt, &bolt);
    lv_obj_set_pos(img_bolt, 36, 2);
    lv_obj_set_style_opa(img_bolt, 0, 0);
}

//================================================================
// 创建卡片
//================================================================
static void battery_card_create()
{
    if (card_Battery.obj == NULL || lv_obj_is_valid(card_Battery.obj) == false)
    {
        card_Battery.create(lv_layer_sys(), BATTERY_CARD_X, BATTERY_CARD_HIDE_Y, BATTERY_CARD_WIDTH, BATTERY_CARD_HEIGHT, LV_ALIGN_TOP_LEFT);
        card_Battery.show(CARD_ANIM_NONE);
        battery_card_construct(card_Battery.obj);
    }
}

//================================================================
// 主检查函数
//================================================================
void battery_card_check()
{
    static int cnt = 0;
    static bool last_charging = false;
    if (current_mode == MODE_MAINMENU)
    {
        if (expanded == false)
        {
            expanded = true;
            LOCKLV();
            battery_card_create();
            card_Battery.move(BATTERY_CARD_X, BATTERY_CARD_SHOW_Y);
            UNLOCKLV();
            cnt = 20;
        }
        ++cnt;
        if (cnt >= 20)
        {
            cnt = 0;

            // 读取稳定电压
            int16_t voltage_mv = battery_get_filtered_voltage();
            uint8_t percent = battery_voltage_to_percent(voltage_mv);
            bool charging = PowerManager_isCharging();
            lv_color_t color = battery_get_text_color(voltage_mv, charging);

            LOCKLV();
            // 更新电压
            //lv_label_set_text_fmt(lbl_battery, "%d.%02dV", voltage_mv / 1000, (voltage_mv % 1000) / 10);
            // 更新百分比
            lv_label_set_text_fmt(lbl_battery_percent, "%d%%", percent);
            // 设置颜色
            //lv_obj_set_style_text_color(lbl_battery, color, 0);
            lv_obj_set_style_text_color(lbl_battery_percent, color, 0);
            UNLOCKLV();

            // 充电状态切换
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
    else
    {
        if (expanded)
        {
            expanded = false;
            LOCKLV();
            card_Battery.move(BATTERY_CARD_X, BATTERY_CARD_HIDE_Y);
            UNLOCKLV();
        }
    }
}
