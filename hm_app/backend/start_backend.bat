@echo off
set HUAWEI_DOMAIN_NAME=您的华为云主账号名
set HUAWEI_USER_NAME=您的IAM用户名或主账号名
set HUAWEI_PASSWORD=您的账号密码
set HUAWEI_PROJECT_ID=您的华东-上海一ProjectID(32位十六进制)
set HUAWEI_INSTANCE_ID=634c99984d
set HUAWEI_DEVICE_ID=6addd23218855b39c55968de_rk3576
set PORT=8080

echo 正在安装依赖...
pip install -r requirements.txt

echo 正在启动华为云 IoTDA 后端桥接服务 (http://0.0.0.0:8080)...
python server.py
pause
