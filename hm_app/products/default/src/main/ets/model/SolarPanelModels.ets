export class AlarmItem {
  code: string = '';
  level: string = '';
  message: string = '';
  time: string = '';
}

export class AlarmList {
  count: number = 0;
  items: AlarmItem[] = [];
}

export class SolarPanelProperties {
  light_avg: number = 0;
  light_tl: number = 0;
  light_tr: number = 0;
  light_bl: number = 0;
  light_br: number = 0;
  elevation: number = 0;
  azimuth: number = 0;
  target_elevation: number = 0;
  target_azimuth: number = 0;
  temperature: number = 0;
  humidity: number = 0;
  weather: string = 'clear';
  tracking: boolean = false;
  warning: boolean = false;
  alarm_list: AlarmList = new AlarmList();
}

export class SolarPanelStatus {
  device_id: string = '';
  device_name: string = '';
  online: boolean = false;
  service_id: string = '';
  properties: SolarPanelProperties = new SolarPanelProperties();
  timestamp: string = '';
}

export function createDemoSolarPanelStatus(): SolarPanelStatus {
  const status = new SolarPanelStatus();
  status.device_id = '6addd23218855b39c55968de_rk3576';
  status.device_name = 'rk3576';
  status.service_id = 'solar_panel';
  status.timestamp = '等待连接...';
  return status;
}
