#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "Delay.h"
#include "OLED.h"
#include "BH1750.h"
#include "DHT11.h"
#include "PCA9685.h"
#include "usart1.h"

/* 全局监测变量 */
static uint16_t g_Lux[4] = {0};        // 4路光照度 (Lux) [0:左上TL, 1:右上TR, 2:左下BL, 3:右下BR]
static uint8_t  g_Temp = 0;            // 温度 (°C)
static uint8_t  g_Humi = 0;            // 湿度 (%RH)
static float    g_Angle_Ch0 = 45.0f;   // 通道0舵机当前角度 (俯仰轴 Pitch)
static float    g_Angle_Ch1 = 90.0f;   // 通道1舵机当前角度 (方位轴 Yaw)
static float    g_Target_Ch0 = 45.0f;  // 通道0目标角度
static float    g_Target_Ch1 = 90.0f;  // 通道1目标角度
static uint8_t  g_AutoReport = 1;      // 定时上传使能标志位 (1: 开启, 0: 关闭)

/* 舵机机械方向和限位 */
#define TRACK_YAW_SIGN       -1.0f
#define TRACK_PITCH_SIGN      1.0f
#define TRACK_YAW_MIN        20.0f
#define TRACK_YAW_MAX        160.0f
#define TRACK_PITCH_MIN      15.0f
#define TRACK_PITCH_MAX      90.0f
#define TRACK_HOME_YAW       90.0f
#define TRACK_HOME_PITCH     45.0f

/* 动态死区与自适应步长 */
#define TRACK_DEADBAND_BASE  15.0f    // 基础死区，平衡点彻底静止
#define TRACK_MIN_LUX        20UL
#define TRACK_FILTER_ALPHA   0.85f    // 快速低通滤波，去除高频毛刺同时几乎零相位延迟

static uint8_t g_TrackingEnabled = 1;
static uint8_t g_DhtValid = 0;
/* OLED: MAN=manual, ERR=sensor, DARK=low light, HOLD=balanced, LIM=limit. */
static char *g_TrackStatus = "WAIT";
static float g_HorizontalFiltered = 0.0f;
static float g_VerticalFiltered = 0.0f;

static float Limit(float value, float lower, float upper)
{
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

/**
  * @brief  根据光敏传感器误差更新目标角度 (运行周期 20ms，实时零滞后)
  */
static void Update_Tracker_Target(void)
{
    uint32_t sum;
    float avg;
    float horizontal, vertical;
    float deadband;

    if (!g_TrackingEnabled)
    {
        g_TrackStatus = "MAN ";
        return;
    }
    if (g_Lux[0] == 65535U || g_Lux[1] == 65535U ||
        g_Lux[2] == 65535U || g_Lux[3] == 65535U)
    {
        g_TrackStatus = "ERR ";
        return;
    }

    sum = (uint32_t)g_Lux[0] + g_Lux[1] + g_Lux[2] + g_Lux[3];
    avg = (float)sum / 4.0f;
    if (avg < (float)TRACK_MIN_LUX)
    {
        g_TrackStatus = "DARK";
        /* 暗光平滑复位至安全中位 (Pitch=45°, Yaw=90°) */
        g_Target_Ch0 = TRACK_HOME_PITCH;
        g_Target_Ch1 = TRACK_HOME_YAW;
        return;
    }

    // 1. 绝对差值计算 (lx)
    horizontal = ((float)g_Lux[0] + g_Lux[2] - g_Lux[1] - g_Lux[3]) / 2.0f;
    vertical = ((float)g_Lux[0] + g_Lux[1] - g_Lux[2] - g_Lux[3]) / 2.0f;

    /* 滤波平滑光照差，消除传感器噪声 */
    g_HorizontalFiltered += TRACK_FILTER_ALPHA * (horizontal - g_HorizontalFiltered);
    g_VerticalFiltered += TRACK_FILTER_ALPHA * (vertical - g_VerticalFiltered);
    horizontal = g_HorizontalFiltered;
    vertical = g_VerticalFiltered;

    // 2. 归一化相对误差计算 (-1.0 ~ +1.0)
    // 无论阳光是 1,000 lx 还是 50,000 lx，norm_h / norm_v 都能真实反映光源偏角比例，彻底避免强光暴冲
    float norm_h = horizontal / (avg + 10.0f);
    float norm_v = vertical / (avg + 10.0f);

    // 死区阈值：归一化相对误差小于 6% 或 绝对光差小于 15 lx 时视为平衡
    deadband = TRACK_DEADBAND_BASE;

    float effective_yaw_sign = (g_Angle_Ch0 > 90.0f) ? (-TRACK_YAW_SIGN) : TRACK_YAW_SIGN;

    float yaw_step = 0.0f;
    float pitch_step = 0.0f;

    // 水平轴计算：基于归一化相对偏角线性控制
    // 相对偏差越大步长越大（0.2° ~ 1.5°），每 20ms 最多前进 1.5° (即最高 75°/s)，既灵敏又绝不暴冲超调
    if (fabsf(horizontal) > deadband && fabsf(norm_h) > 0.06f)
    {
        float ratio = (fabsf(norm_h) - 0.06f);
        float step = 0.25f + ratio * 2.0f;
        if (step > 1.5f) step = 1.5f;
        yaw_step = (horizontal > 0.0f) ? (step * effective_yaw_sign) : (-step * effective_yaw_sign);
    }

    // 俯仰轴计算
    if (fabsf(vertical) > deadband && fabsf(norm_v) > 0.06f)
    {
        float ratio = (fabsf(norm_v) - 0.06f);
        float step = 0.25f + ratio * 2.0f;
        if (step > 1.5f) step = 1.5f;
        pitch_step = (vertical > 0.0f) ? (step * TRACK_PITCH_SIGN) : (-step * TRACK_PITCH_SIGN);
    }

    g_Target_Ch1 = Limit(g_Target_Ch1 + yaw_step, TRACK_YAW_MIN, TRACK_YAW_MAX);
    g_Target_Ch0 = Limit(g_Target_Ch0 + pitch_step, TRACK_PITCH_MIN, TRACK_PITCH_MAX);

    g_TrackStatus = "RUN ";
    if (pitch_step == 0.0f && yaw_step == 0.0f)
        g_TrackStatus = "HOLD";
    else if ((g_Target_Ch0 <= TRACK_PITCH_MIN || g_Target_Ch0 >= TRACK_PITCH_MAX) &&
             (g_Target_Ch1 <= TRACK_YAW_MIN || g_Target_Ch1 >= TRACK_YAW_MAX))
        g_TrackStatus = "LIM ";
}

/**
  * @brief  舵机平滑逼近驱动 (运行周期 20ms)
  *         平滑跟进目标角度，过冲抑制阻尼
  */
static void Servo_Smooth_Drive(void)
{
    uint8_t moved = 0;

    // 1. 俯仰轴 Pitch (CH0) 平滑逼近
    float diff0 = g_Target_Ch0 - g_Angle_Ch0;
    if (fabsf(diff0) > 0.12f)
    {
        float max_s = 0.25f + fabsf(diff0) * 0.25f;
        if (max_s > 1.8f) max_s = 1.8f;
        
        if (diff0 > max_s) g_Angle_Ch0 += max_s;
        else if (diff0 < -max_s) g_Angle_Ch0 -= max_s;
        else g_Angle_Ch0 = g_Target_Ch0;

        PCA9685_SetServoAngle(0, g_Angle_Ch0);
        moved = 1;
    }

    // 2. 方位轴 Yaw (CH1) 平滑逼近
    float diff1 = g_Target_Ch1 - g_Angle_Ch1;
    if (fabsf(diff1) > 0.12f)
    {
        float max_s = 0.25f + fabsf(diff1) * 0.25f;
        if (max_s > 1.8f) max_s = 1.8f;

        if (diff1 > max_s) g_Angle_Ch1 += max_s;
        else if (diff1 < -max_s) g_Angle_Ch1 -= max_s;
        else g_Angle_Ch1 = g_Target_Ch1;

        PCA9685_SetServoAngle(1, g_Angle_Ch1);
        moved = 1;
    }

    if (moved && PCA9685_WriteFailed())
    {
        g_TrackStatus = "BUS ";
    }
}

static void Report_Data(void)
{
    USART1_Printf("$DATA,L1:%u,L2:%u,L3:%u,L4:%u,TEMP:%d,HUMI:%d,CH0:%.1f,CH1:%.1f,TRACK:%u,DHT:%u\r\n",
                  g_Lux[0], g_Lux[1], g_Lux[2], g_Lux[3], g_Temp, g_Humi,
                  g_Angle_Ch0, g_Angle_Ch1, g_TrackingEnabled, g_DhtValid);
}

/**
  * @brief  解析并执行来自串口的控制命令
  * @param  cmd: 接收到的字符串指令
  */
static void Process_Uart_Command(char *cmd)
{
    size_t len;
    while (*cmd == ' ' || *cmd == '\r' || *cmd == '\n') 
    {
        cmd++;
    }

    len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\r' || cmd[len-1] == '\n')) cmd[--len] = '\0';
    if (*cmd == '\0') return;

    // 指令1: #SERVO:ch,angle
    if (strncmp(cmd, "#SERVO:", 7) == 0 || strncmp(cmd, "SERVO ", 6) == 0)
    {
        int ch = 0;
        float angle = 0.0f;
        char *p = (strncmp(cmd, "#SERVO:", 7) == 0) ? (cmd + 7) : (cmd + 6);
        if (sscanf(p, "%d,%f", &ch, &angle) == 2 || sscanf(p, "%d %f", &ch, &angle) == 2)
        {
            if (ch >= 0 && ch <= 15 && angle >= 0.0f && angle <= 180.0f &&
                (ch != 0 || (angle >= TRACK_PITCH_MIN && angle <= TRACK_PITCH_MAX)) &&
                (ch != 1 || (angle >= TRACK_YAW_MIN && angle <= TRACK_YAW_MAX)))
            {
                if (ch == 0 || ch == 1) g_TrackingEnabled = 0;
                if (ch == 0) { g_Target_Ch0 = angle; }
                if (ch == 1) { g_Target_Ch1 = angle; }
                if (ch > 1) { PCA9685_SetServoAngle((uint8_t)ch, angle); }
                USART1_Printf("[OK] SERVO ch=%d, angle=%.1f\r\n", ch, angle);
                return;
            }
        }
        USART1_Printf("[ERR] Invalid SERVO params. Usage: #SERVO:ch,angle (0~180)\r\n");
        return;
    }

    // 指令2: #POS:pitch,yaw
    if (strncmp(cmd, "#POS:", 5) == 0 || strncmp(cmd, "POS ", 4) == 0)
    {
        float pitch = 0.0f, yaw = 0.0f;
        char *p = (strncmp(cmd, "#POS:", 5) == 0) ? (cmd + 5) : (cmd + 4);
        if (sscanf(p, "%f,%f", &pitch, &yaw) == 2 || sscanf(p, "%f %f", &pitch, &yaw) == 2)
        {
            if (pitch >= TRACK_PITCH_MIN && pitch <= TRACK_PITCH_MAX &&
                yaw >= TRACK_YAW_MIN && yaw <= TRACK_YAW_MAX)
            {
                g_TrackingEnabled = 0;
                g_Target_Ch0 = pitch;
                g_Target_Ch1 = yaw;
                USART1_Printf("[OK] POS pitch=%.1f, yaw=%.1f\r\n", pitch, yaw);
                return;
            }
        }
        USART1_Printf("[ERR] Invalid POS params. Usage: #POS:pitch,yaw\r\n");
        return;
    }

    // 指令3: #GET
    if (strncmp(cmd, "#GET", 4) == 0 || strncmp(cmd, "GET", 3) == 0)
    {
        Report_Data();
        return;
    }

    // 指令4: #AUTO:1 或 #AUTO:0
    if (strncmp(cmd, "#AUTO:", 6) == 0)
    {
        g_AutoReport = (cmd[6] == '1') ? 1 : 0;
        USART1_Printf("[OK] Auto Report = %d\r\n", g_AutoReport);
        return;
    }

    /* #TRACK:1 开启下位机跟踪；#TRACK:0 保持当前角度。 */
    if (strcmp(cmd, "#TRACK:1") == 0)
    {
        g_TrackingEnabled = 1;
        g_HorizontalFiltered = 0.0f;
        g_VerticalFiltered = 0.0f;
        USART1_SendString("[OK] TRACK=1\r\n");
        return;
    }
    if (strcmp(cmd, "#TRACK:0") == 0)
    {
        g_TrackingEnabled = 0;
        USART1_SendString("[OK] TRACK=0\r\n");
        return;
    }

    USART1_Printf("[ERR] Unknown command: %s\r\n", cmd);
}

int main(void)
{
    uint32_t report_tick = 0;
    uint8_t dht_err = 0;
    uint8_t dht_divider = 1;

    // 1. 系统外设初始化
    USART1_Init(115200);           // 串口1初始化 (PA9 TX, PA10 RX)
    OLED_Init();                   // OLED初始化 (PB8 SCL, PB9 SDA)
    BH1750_Init();                 // 4路光照度传感器初始化 (SCL:PB11, SDA:PB10,PA4,PA5,PA6)
    DHT11_Init();                  // 温湿度传感器初始化 (PB12 DATA)
    PCA9685_Init(50);              // 16路PWM舵机控制器初始化 (PB13 SCL, PB14 SDA, 50Hz)

    // 2. 舵机初始位置归位 (安全中位)
    PCA9685_SetServoAngle(0, g_Angle_Ch0);
    PCA9685_SetServoAngle(1, g_Angle_Ch1);

    // 3. OLED UI 初始静态框架显示
    OLED_Clear();
    OLED_ShowString(1, 1, "1:     2:    ");
    OLED_ShowString(2, 1, "3:     4:    ");
    OLED_ShowString(3, 1, "T:   C H:   %");
    OLED_ShowString(4, 1, "P:     Y:    ");

    USART1_SendString("\r\n========================================\r\n");
    USART1_SendString("   4-Sensor Solar Tracker System Ready  \r\n");
    USART1_SendString("   4x BH1750 + DHT11 + PCA9685 Servos   \r\n");
    USART1_SendString("========================================\r\n");

    while (1)
    {
        // -------------------------------------------------------------
        // A. 串口控制消息接收与处理
        // -------------------------------------------------------------
        if (USART1_RxFlag == 1)
        {
            Process_Uart_Command((char *)USART1_RxBuffer);
            USART1_ClearRx();
        }

        // -------------------------------------------------------------
        // B. 20ms 同步实时闭环 (50Hz 采样 + 50Hz 动力学平滑驱动，完全对齐舵机PWM刷新率)
        // -------------------------------------------------------------
        Delay_ms(20);
        report_tick += 20;

        // 1. 20ms 实时采集 4 路光强 (无延迟，立即感知光源位置)
        BH1750_Read_All(g_Lux);
        Update_Tracker_Target();

        // 2. 20ms 动态自适应速度平滑驱动 (根据角误差自动调整速度，近处减速，远处加速)
        Servo_Smooth_Drive();

        // -------------------------------------------------------------
        // C. 定时 500ms 刷新 OLED 和上传状态
        // -------------------------------------------------------------
        if (report_tick >= 500)
        {
            report_tick = 0;

            // 采集温湿度
            if (++dht_divider >= 4)
            {
                dht_divider = 0;
                dht_err = DHT11_ReadData(&g_Temp, &g_Humi);
                g_DhtValid = (dht_err == 0) ? 1 : 0;
            }

            // OLED 刷新显示
            if (g_Lux[0] == 65535U) OLED_ShowString(1, 3, "ERR  ");
            else OLED_ShowNum(1, 3, g_Lux[0], 5);
            if (g_Lux[1] == 65535U) OLED_ShowString(1, 10, "ERR  ");
            else OLED_ShowNum(1, 10, g_Lux[1], 5);

            if (g_Lux[2] == 65535U) OLED_ShowString(2, 3, "ERR  ");
            else OLED_ShowNum(2, 3, g_Lux[2], 5);
            if (g_Lux[3] == 65535U) OLED_ShowString(2, 10, "ERR  ");
            else OLED_ShowNum(2, 10, g_Lux[3], 5);

            OLED_ShowNum(3, 3, g_Temp, 2);
            OLED_ShowNum(3, 9, g_Humi, 2);

            OLED_ShowNum(4, 3, (uint32_t)g_Angle_Ch0, 3);
            OLED_ShowNum(4, 10, (uint32_t)g_Angle_Ch1, 3);

            OLED_ShowString(4, 13, g_TrackStatus);

            // 串口定时主动上报
            if (g_AutoReport)
            {
                Report_Data();
            }
        }
    }
}
