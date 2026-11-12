# 华为云 IoTDA 桥接服务端

本服务用于为鸿蒙端（`hm_app`）提供华为云 IoTDA 实时设备状态转换接口。

## 为什么需要此服务端？
- **移动端安全原则**：移动端 App（ArkTS）中切勿直接内置主账号密码或 AK/SK 敏感凭据；
- **协议转换**：鸿蒙端通过轻量级 HTTP GET `/api/v1/solar-panel/status` 轮询请求，由后端调用华为云 IAM 获取临时 Token 并查询设备影子（Device Shadow），然后格式化为 App 所需的模型格式返回。

## 快速使用

1. 打开并编辑 `start_backend.bat`（或在环境变量中配置）：
   - `HUAWEI_DOMAIN_NAME`：华为云主账号名称
   - `HUAWEI_USER_NAME`：IAM 用户名或账号名
   - `HUAWEI_PASSWORD`：密码
   - `HUAWEI_PROJECT_ID`：华东-上海一的项目 ID（可在华为云控制台右上角“我的凭证”->“API凭证”中查看）
2. 双击运行 `start_backend.bat` 启动服务；
3. 将运行服务端的电脑 IP 地址配置在鸿蒙 App 的 `HuaweiCloudConfig.ets` 中：
   ```typescript
   export const API_BASE_URL: string = 'http://<你的电脑局域网IP>:8080';
   ```
4. 在 DevEco Studio 中重新运行鸿蒙应用，即可实时显示开发板上报给华为云的所有真实数据！
