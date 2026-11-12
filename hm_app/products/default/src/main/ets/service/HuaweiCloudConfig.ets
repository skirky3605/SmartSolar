/**
 * 华为云接入配置（App 原生直连模式）
 * 包含 IAM 认证参数与 IoTDA 设备影子接口配置。
 */

// 华为云主账号名称
export const HUAWEI_CLOUD_DOMAIN_NAME: string = 'GT-skirky';

// 华为云 IAM 子用户名及密码
export const HUAWEI_CLOUD_IAM_USER: string = 'hm_app_user';
export const HUAWEI_CLOUD_IAM_PASSWORD: string = 'Tm7630_2&oYTjCB3';

// 区域及项目 ID (华东-上海一)
export const HUAWEI_CLOUD_REGION: string = 'cn-east-3';
export const HUAWEI_CLOUD_PROJECT_ID: string = '8747495ba2b3446eadab47c99a06e800';

// IAM API 根地址 (获取 Token)
export const HUAWEI_IAM_ENDPOINT: string = 'https://iam.cn-east-3.myhuaweicloud.com';

// IoTDA 应用侧 OpenAPI 访问地址 (标准版实例专属域名)
export const IOTDA_APP_ENDPOINT: string = 'https://634c99984d.st1.iotda-app.cn-east-3.myhuaweicloud.com';

// 设备 ID 与 物模型服务 ID
export const IOTDA_DEVICE_ID: string = '6addd23218855b39c55968de_rk3576';
export const IOTDA_SERVICE_ID: string = 'solar_panel';

// 保留兼容常量
export const API_BASE_URL: string = '';
export const STATUS_PATH: string = '';
