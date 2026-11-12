import { http } from '@kit.NetworkKit';
import { createDemoSolarPanelStatus, SolarPanelStatus, AlarmItem, SolarPanelProperties, AlarmList } from '../model/SolarPanelModels';
import {
  HUAWEI_CLOUD_DOMAIN_NAME,
  HUAWEI_CLOUD_IAM_USER,
  HUAWEI_CLOUD_IAM_PASSWORD,
  HUAWEI_CLOUD_REGION,
  HUAWEI_CLOUD_PROJECT_ID,
  HUAWEI_IAM_ENDPOINT,
  IOTDA_APP_ENDPOINT,
  IOTDA_DEVICE_ID,
  IOTDA_SERVICE_ID,
  API_BASE_URL
} from './HuaweiCloudConfig';

export { API_BASE_URL };

let cachedToken: string = '';
let tokenExpireTime: number = 0;

function extractNumberSimple(json: string, key: string, fallback: number = 0): number {
  const needle = `"${key}":`;
  const idx = json.indexOf(needle);
  if (idx === -1) {
    return fallback;
  }
  const start = idx + needle.length;
  let end = start;
  while (end < json.length) {
    const ch = json.charAt(end);
    if ((ch >= '0' && ch <= '9') || ch === '.' || ch === '-') {
      end++;
    } else {
      break;
    }
  }
  const slice = json.substring(start, end).trim();
  const val = Number(slice);
  return Number.isNaN(val) ? fallback : val;
}

function extractStringSimple(json: string, key: string, fallback: string = ''): string {
  const needle = `"${key}":"`;
  const idx = json.indexOf(needle);
  if (idx === -1) {
    return fallback;
  }
  const start = idx + needle.length;
  const end = json.indexOf('"', start);
  if (end === -1) {
    return fallback;
  }
  return json.substring(start, end);
}

function extractBooleanSimple(json: string, key: string, fallback: boolean = false): boolean {
  const needle = `"${key}":`;
  const idx = json.indexOf(needle);
  if (idx === -1) {
    return fallback;
  }
  const rest = json.substring(idx + needle.length, idx + needle.length + 10);
  if (rest.startsWith('true')) {
    return true;
  }
  if (rest.startsWith('false')) {
    return false;
  }
  return fallback;
}

/**
 * 解析华为云时间格式（如 20261108T134647Z 或 2026-11-08T13:48:17.162Z）为毫秒时间戳
 */
function parseHuaweiTimeMs(timeStr: string): number {
  if (!timeStr || timeStr.length < 15) {
    return 0;
  }
  try {
    const s = timeStr.replace(/-/g, '').replace(/:/g, '');
    if (s.length >= 15 && s.charAt(8) === 'T') {
      const year = parseInt(s.substring(0, 4), 10);
      const month = parseInt(s.substring(4, 6), 10) - 1;
      const day = parseInt(s.substring(6, 8), 10);
      const hour = parseInt(s.substring(9, 11), 10);
      const min = parseInt(s.substring(11, 13), 10);
      const sec = parseInt(s.substring(13, 15), 10);
      return Date.UTC(year, month, day, hour, min, sec);
    }
  } catch (e) {
    // 忽略异常
  }
  return 0;
}

export interface FetchResult {
  status: SolarPanelStatus;
  rawJson: string;
}

export class SolarPanelApi {
  /**
   * 向华为云 IAM 服务申请 Token
   */
  private static async getHuaweiToken(): Promise<string> {
    const now = Date.now();
    if (cachedToken.length > 0 && now < tokenExpireTime - 300000) {
      return cachedToken;
    }

    const request = http.createHttp();
    try {
      const url = `${HUAWEI_IAM_ENDPOINT}/v3/auth/tokens?nocatalog=true`;
      const bodyStr = `{"auth":{"identity":{"methods":["password"],"password":{"user":{"domain":{"name":${JSON.stringify(HUAWEI_CLOUD_DOMAIN_NAME)}},"name":${JSON.stringify(HUAWEI_CLOUD_IAM_USER)},"password":${JSON.stringify(HUAWEI_CLOUD_IAM_PASSWORD)}}}},"scope":{"project":{"name":${JSON.stringify(HUAWEI_CLOUD_REGION)}}}}}`;

      const response: http.HttpResponse = await request.request(url, {
        method: http.RequestMethod.POST,
        header: {
          'Content-Type': 'application/json;charset=UTF-8'
        },
        extraData: bodyStr,
        connectTimeout: 10000,
        readTimeout: 10000
      });

      if (response.responseCode !== 201) {
        throw new Error(`IAM 认证失败：HTTP ${response.responseCode}`);
      }

      let token = '';
      if (response.header) {
        const headerMap = response.header as Record<string, string | string[]>;
        const keys = Object.keys(headerMap);
        for (let i = 0; i < keys.length; i++) {
          const key = keys[i];
          if (key.toLowerCase() === 'x-subject-token') {
            const val = headerMap[key];
            token = Array.isArray(val) ? val[0] : val;
            break;
          }
        }
      }

      if (token.length === 0) {
        throw new Error('未在响应头中获取到 X-Subject-Token');
      }

      cachedToken = token;
      tokenExpireTime = now + 24 * 3600 * 1000;
      return cachedToken;
    } finally {
      request.destroy();
    }
  }

  /**
   * 直连华为云 IoTDA 查询设备在线状态（GET /v5/iot/{project_id}/devices/{device_id}）
   */
  private static async getDeviceOnlineStatus(token: string): Promise<boolean> {
    const request = http.createHttp();
    try {
      const url = `${IOTDA_APP_ENDPOINT}/v5/iot/${HUAWEI_CLOUD_PROJECT_ID}/devices/${IOTDA_DEVICE_ID}`;
      const response: http.HttpResponse = await request.request(url, {
        method: http.RequestMethod.GET,
        header: {
          'Content-Type': 'application/json;charset=UTF-8',
          'X-Auth-Token': token
        },
        connectTimeout: 5000,
        readTimeout: 5000
      });

      if (response.responseCode === 200) {
        const resStr = typeof response.result === 'string' ? response.result : JSON.stringify(response.result);
        const statusVal = extractStringSimple(resStr, 'status', '');
        return statusVal.toUpperCase() === 'ONLINE';
      }
    } catch (e) {
      // 容错处理
    } finally {
      request.destroy();
    }
    return false;
  }

  /**
   * 直连华为云 IoTDA 查询设备影子，获取实时上报数据以及原始报文
   */
  static async getStatusWithRaw(): Promise<FetchResult> {
    const token = await SolarPanelApi.getHuaweiToken();
    const request = http.createHttp();
    try {
      // 并行请求：获取设备影子数据 + 查询设备在线状态
      const shadowUrl = `${IOTDA_APP_ENDPOINT}/v5/iot/${HUAWEI_CLOUD_PROJECT_ID}/devices/${IOTDA_DEVICE_ID}/shadow`;
      const shadowResponse: http.HttpResponse = await request.request(shadowUrl, {
        method: http.RequestMethod.GET,
        header: {
          'Content-Type': 'application/json;charset=UTF-8',
          'X-Auth-Token': token
        },
        connectTimeout: 10000,
        readTimeout: 10000
      });
      const isDeviceOnline: boolean = await SolarPanelApi.getDeviceOnlineStatus(token);

      if (shadowResponse.responseCode !== 200) {
        throw new Error(`IoTDA 影子请求失败：HTTP ${shadowResponse.responseCode}`);
      }

      const rawJson = typeof shadowResponse.result === 'string' ? shadowResponse.result : JSON.stringify(shadowResponse.result);

      const statusResult = new SolarPanelStatus();
      statusResult.device_id = IOTDA_DEVICE_ID;
      statusResult.device_name = 'rk3576';
      statusResult.service_id = IOTDA_SERVICE_ID;

      // 提取时间戳
      const eventTime = extractStringSimple(rawJson, 'event_time', new Date().toLocaleTimeString());
      statusResult.timestamp = eventTime;

      // 核心判断：华为云设备状态为 ONLINE，且数据最后更新时间在 15 秒之内
      const eventTimeMs = parseHuaweiTimeMs(eventTime);
      const isFresh = eventTimeMs > 0 ? (Date.now() - eventTimeMs < 15000) : true;
      statusResult.online = isDeviceOnline && isFresh;

      // 构造属性容器
      const props = new SolarPanelProperties();
      const alarmList = new AlarmList();

      // 先尝试通过标准 JSON.parse 解析
      let parsedSuccessfully = false;
      try {
        const parsed = JSON.parse(rawJson) as Record<string, Object>;
        const shadow = (parsed['shadow'] as Array<Record<string, Object>>) || [];
        for (let i = 0; i < shadow.length; i++) {
          const s = shadow[i];
          if (s['service_id'] === IOTDA_SERVICE_ID && s['reported']) {
            const reported = s['reported'] as Record<string, Object>;
            if (reported['properties']) {
              const p = reported['properties'] as Record<string, Object>;
              
              if (p['light_avg'] !== undefined && p['light_avg'] !== null) props.light_avg = Number(p['light_avg']);
              if (p['light_tl'] !== undefined && p['light_tl'] !== null) props.light_tl = Number(p['light_tl']);
              if (p['light_tr'] !== undefined && p['light_tr'] !== null) props.light_tr = Number(p['light_tr']);
              if (p['light_bl'] !== undefined && p['light_bl'] !== null) props.light_bl = Number(p['light_bl']);
              if (p['light_br'] !== undefined && p['light_br'] !== null) props.light_br = Number(p['light_br']);
              if (p['elevation'] !== undefined && p['elevation'] !== null) props.elevation = Number(p['elevation']);
              if (p['azimuth'] !== undefined && p['azimuth'] !== null) props.azimuth = Number(p['azimuth']);
              if (p['target_elevation'] !== undefined && p['target_elevation'] !== null) props.target_elevation = Number(p['target_elevation']);
              if (p['target_azimuth'] !== undefined && p['target_azimuth'] !== null) props.target_azimuth = Number(p['target_azimuth']);
              if (p['temperature'] !== undefined && p['temperature'] !== null) props.temperature = Number(p['temperature']);
              if (p['humidity'] !== undefined && p['humidity'] !== null) props.humidity = Number(p['humidity']);
              if (p['weather']) props.weather = String(p['weather']);
              if (p['tracking'] !== undefined && p['tracking'] !== null) props.tracking = Boolean(p['tracking']);
              if (p['warning'] !== undefined && p['warning'] !== null) props.warning = Boolean(p['warning']);

              if (p['alarm_list']) {
                const al = p['alarm_list'] as Record<string, Object>;
                const detections = (al['detections'] as Array<Record<string, Object>>) || [];
                for (let j = 0; j < detections.length; j++) {
                  const d = detections[j];
                  const item = new AlarmItem();
                  item.code = `DET_${j + 1}`;
                  item.level = 'WARN';
                  const label = d['label'] ? String(d['label']) : 'unknown';
                  const conf = Math.round((Number(d['confidence']) || 0) * 100);
                  item.message = `目标检测: ${label} (${conf}%)`;
                  item.time = eventTime;
                  alarmList.items.push(item);
                }
              }

              parsedSuccessfully = true;
            }
          }
        }
      } catch (e) {
        parsedSuccessfully = false;
      }

      // 如果 JSON 解析未提取到关键数据（例如 light_avg 仍为 0），使用字符串切片做兜底保证
      if (!parsedSuccessfully || props.light_avg === 0) {
        props.light_avg = extractNumberSimple(rawJson, 'light_avg', props.light_avg);
        props.light_tl = extractNumberSimple(rawJson, 'light_tl', props.light_tl);
        props.light_tr = extractNumberSimple(rawJson, 'light_tr', props.light_tr);
        props.light_bl = extractNumberSimple(rawJson, 'light_bl', props.light_bl);
        props.light_br = extractNumberSimple(rawJson, 'light_br', props.light_br);
        props.elevation = extractNumberSimple(rawJson, 'elevation', props.elevation);
        props.azimuth = extractNumberSimple(rawJson, 'azimuth', props.azimuth);
        props.target_elevation = extractNumberSimple(rawJson, 'target_elevation', props.target_elevation);
        props.target_azimuth = extractNumberSimple(rawJson, 'target_azimuth', props.target_azimuth);
        props.temperature = extractNumberSimple(rawJson, 'temperature', props.temperature);
        props.humidity = extractNumberSimple(rawJson, 'humidity', props.humidity);
        props.weather = extractStringSimple(rawJson, 'weather', props.weather);
        props.tracking = extractBooleanSimple(rawJson, 'tracking', props.tracking);
        props.warning = extractBooleanSimple(rawJson, 'warning', props.warning);
      }

      alarmList.count = alarmList.items.length;
      if (alarmList.count > 0) {
        props.warning = true;
      }
      props.alarm_list = alarmList;

      statusResult.properties = props;
      const res: FetchResult = {
        status: statusResult,
        rawJson: rawJson
      };
      return res;
    } finally {
      request.destroy();
    }
  }

  static async getStatus(): Promise<SolarPanelStatus> {
    const res = await SolarPanelApi.getStatusWithRaw();
    return res.status;
  }
}
