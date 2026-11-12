#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <string.h>
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
static float    g_Angle_Ch0 = 90.0f;   // 通道0舵机当前角度 (俯仰轴 Pitch)
static float    g_Angle_Ch1 = 90.0f;   // 通道1舵机当前角度 (方位轴 Yaw)
static uint8_t  g_AutoReport = 1;      // 定时上传使能标志位 (1: 开启, 0: 关闭)

/* 舵机机械方向和限位需现场标定，初始 90 度也需确认安全。 */
#define TRACK_YAW_SIGN       1.0f
#define TRACK_PITCH_SIGN     1.0f
#define TRACK_YAW_MIN        0.0f
#define TRACK_YAW_MAX        180.0f
#define TRACK_PITCH_MIN      0.0f
#define TRACK_PITCH_MAX      180.0f
#define TRACK_DEADBAND_LUX   10.0f
#define TRACK_MIN_LUX        20UL
#define TRACK_GAIN           0.02f
#define TRACK_MAX_STEP       3.0f

static uint8_t g_TrackingEnabled = 1;
static uint8_t g_DhtValid = 0;

static float Limit(float value, float lower, float upper)
{
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

static void Update_Tracker(void)
{
    uint32_t sum;
    float horizontal, vertical, yaw_step, pitch_step, yaw, pitch;
    if (!g_TrackingEnabled || g_Lux[0] == 65535U || g_Lux[1] == 65535U ||
        g_Lux[2] == 65535U || g_Lux[3] == 65535U) return;
    sum = (uint32_t)g_Lux[0] + g_Lux[1] + g_Lux[2] + g_Lux[3];
    if (sum / 4U < TRACK_MIN_LUX) return;
    horizontal = ((float)g_Lux[0] + g_Lux[2] - g_Lux[1] - g_Lux[3]) / 2.0f;
    vertical = ((float)g_Lux[0] + g_Lux[1] - g_Lux[2] - g_Lux[3]) / 2.0f;
    yaw_step = (horizontal > TRACK_DEADBAND_LUX || horizontal < -TRACK_DEADBAND_LUX)
        ? Limit(horizontal * TRACK_GAIN * TRACK_YAW_SIGN, -TRACK_MAX_STEP, TRACK_MAX_STEP) : 0.0f;
    pitch_step = (vertical > TRACK_DEADBAND_LUX || vertical < -TRACK_DEADBAND_LUX)
        ? Limit(vertical * TRACK_GAIN * TRACK_PITCH_SIGN, -TRACK_MAX_STEP, TRACK_MAX_STEP) : 0.0f;
    yaw = Limit(g_Angle_Ch1 + yaw_step, TRACK_YAW_MIN, TRACK_YAW_MAX);
    pitch = Limit(g_Angle_Ch0 + pitch_step, TRACK_PITCH_MIN, TRACK_PITCH_MAX);
    if (pitch != g_Angle_Ch0) { PCA9685_SetServoAngle(0, pitch); g_Angle_Ch0 = pitch; }
    if (yaw != g_Angle_Ch1) { PCA9685_SetServoAngle(1, yaw); g_Angle_Ch1 = yaw; }
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
    // 去除前导空格与换行符
    while (*cmd == ' ' || *cmd == '\r' || *cmd == '\n') 
    {
        cmd++;
    }

    len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\r' || cmd[len-1] == '\n')) cmd[--len] = '\0';
    if (*cmd == '\0') return;

    // 指令1: #SERVO:ch,angle 或 SERVO ch angle (如 #SERVO:0,90)
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
                PCA9685_SetServoAngle((uint8_t)ch, angle);
                if (ch == 0) g_Angle_Ch0 = angle;
                if (ch == 1) g_Angle_Ch1 = angle;
                USART1_Printf("[OK] SERVO ch=%d, angle=%.1f\r\n", ch, angle);
                return;
            }
        }
        USART1_Printf("[ERR] Invalid SERVO params. Usage: #SERVO:ch,angle (0~180)\r\n");
        return;
    }

    // 指令2: #POS:pitch,yaw (双轴光伏自动调整角度, CH0俯仰, CH1方位)
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
                PCA9685_SetServoAngle(0, pitch);
                PCA9685_SetServoAngle(1, yaw);
                g_Angle_Ch0 = pitch;
                g_Angle_Ch1 = yaw;
                USART1_Printf("[OK] POS pitch=%.1f, yaw=%.1f\r\n", pitch, yaw);
                return;
            }
        }
        USART1_Printf("[ERR] Invalid POS params. Usage: #POS:pitch,yaw\r\n");
        return;
    }

    // 指令3: #GET 查询当前全部4路光照传感器与舵机数据
    if (strncmp(cmd, "#GET", 4) == 0 || strncmp(cmd, "GET", 3) == 0)
    {
        Report_Data();
        return;
    }

    // 指令4: #AUTO:1 或 #AUTO:0 (开启或关闭定时主动上传)
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

    // 2. 舵机初始位置归位 (默认90度中位)
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
        // B. 定时任务：每 500ms 采集 4 路光照与温湿度，并在 OLED / 串口上传
        // -------------------------------------------------------------
        Delay_ms(20);
        report_tick += 20;

        if (report_tick >= 500)
        {
            report_tick = 0;

            // 1. 一次性采集全部 4 路光照度
            BH1750_Read_All(g_Lux);

            // 2. 采集温湿度
            if (++dht_divider >= 4)
            {
                dht_divider = 0;
                dht_err = DHT11_ReadData(&g_Temp, &g_Humi);
                g_DhtValid = (dht_err == 0) ? 1 : 0;
            }
            Update_Tracker();

            // 3. OLED 刷新显示 (4路照度以四象限呈现)
            if (g_Lux[0] == 65535U) OLED_ShowString(1, 3, "ERR  ");
            else OLED_ShowNum(1, 3, g_Lux[0], 5); // 传感器1 (左上)
            if (g_Lux[1] == 65535U) OLED_ShowString(1, 10, "ERR  ");
            else OLED_ShowNum(1, 10, g_Lux[1], 5); // 传感器2 (右上)

            if (g_Lux[2] == 65535U) OLED_ShowString(2, 3, "ERR  ");
            else OLED_ShowNum(2, 3, g_Lux[2], 5); // 传感器3 (左下)
            if (g_Lux[3] == 65535U) OLED_ShowString(2, 10, "ERR  ");
            else OLED_ShowNum(2, 10, g_Lux[3], 5); // 传感器4 (右下)

            OLED_ShowNum(3, 3, g_Temp, 2);   // 温度
            OLED_ShowNum(3, 9, g_Humi, 2);   // 湿度

            OLED_ShowNum(4, 3, (uint32_t)g_Angle_Ch0, 3); // 俯仰角 P
            OLED_ShowNum(4, 10, (uint32_t)g_Angle_Ch1, 3); // 方位角 Y

            // 4. 通过串口定时主动上报数据
            if (g_AutoReport)
            {
                Report_Data();
            }
        }
    }
}
