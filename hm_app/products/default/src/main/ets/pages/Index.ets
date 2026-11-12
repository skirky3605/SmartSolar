import { AlarmItem, createDemoSolarPanelStatus, SolarPanelStatus } from '../model/SolarPanelModels';
import { SolarPanelApi } from '../service/SolarPanelApi';

@Entry
@Component
struct Index {
  @StorageProp('topSafeHeight') topSafeHeight: number = 38;
  @StorageProp('bottomSafeHeight') bottomSafeHeight: number = 20;

  @State status: SolarPanelStatus = createDemoSolarPanelStatus();
  @State manualRefreshing: boolean = false;
  private isAutoFetching: boolean = false;
  @State errorMessage: string = '';
  @State rawJsonText: string = '尚未拉取数据';
  @State showDebug: boolean = false;

  // 顶层状态变量
  @State lightAvg: number = 0;
  @State lightTl: number = 0;
  @State lightTr: number = 0;
  @State lightBl: number = 0;
  @State lightBr: number = 0;
  @State pitch: number = 0;
  @State yaw: number = 0;
  @State targetPitch: number = 0;
  @State targetYaw: number = 0;
  @State temp: number = 0;
  @State humi: number = 0;
  @State weatherStr: string = 'clear';
  @State isTracking: boolean = false;
  @State isWarning: boolean = false;
  @State alarmItems: AlarmItem[] = [];

  private refreshTimer: number = -1;

  onPageShow(): void {
    this.refreshStatus(false);
    if (this.refreshTimer === -1) {
      this.refreshTimer = setInterval(() => {
        if (!this.isAutoFetching && !this.manualRefreshing) {
          this.refreshStatus(false);
        }
      }, 2000);
    }
  }

  onPageHide(): void {
    this.stopRefreshTimer();
  }

  aboutToDisappear(): void {
    this.stopRefreshTimer();
  }

  private stopRefreshTimer(): void {
    if (this.refreshTimer !== -1) {
      clearInterval(this.refreshTimer);
      this.refreshTimer = -1;
    }
  }

  private async refreshStatus(isManual: boolean = false): Promise<void> {
    if (isManual) {
      this.manualRefreshing = true;
    } else {
      this.isAutoFetching = true;
    }
    try {
      const result = await SolarPanelApi.getStatusWithRaw();
      this.status = result.status;
      this.rawJsonText = result.rawJson;

      const p = result.status.properties;
      this.lightAvg = p.light_avg;
      this.lightTl = p.light_tl;
      this.lightTr = p.light_tr;
      this.lightBl = p.light_bl;
      this.lightBr = p.light_br;
      this.pitch = p.elevation;
      this.yaw = p.azimuth;
      this.targetPitch = p.target_elevation;
      this.targetYaw = p.target_azimuth;
      this.temp = p.temperature;
      this.humi = p.humidity;
      this.weatherStr = p.weather;
      this.isTracking = p.tracking;
      this.isWarning = p.warning;
      this.alarmItems = p.alarm_list.items;

      this.errorMessage = '';
    } catch (error) {
      this.errorMessage = `云端同步失败：${error instanceof Error ? error.message : '网络异常'}`;
    } finally {
      if (isManual) {
        this.manualRefreshing = false;
      } else {
        this.isAutoFetching = false;
      }
    }
  }

  private weatherText(weather: string): string {
    if (weather === 'clear') {
      return '晴朗';
    }
    if (weather === 'cloudy') {
      return '多云';
    }
    if (weather === 'rainy') {
      return '雨天';
    }
    return '未知';
  }

  @Builder
  sectionTitle(title: string) {
    Text(title)
      .width('100%')
      .fontSize(19)
      .fontWeight(FontWeight.Bold)
      .fontColor('#17202A')
      .margin({ top: 18, bottom: 10 })
  }

  build() {
    Scroll() {
      Column() {
        Row() {
          Column({ space: 4 }) {
            Text('智能光伏').fontSize(28).fontWeight(FontWeight.Bold).fontColor('#17202A')
            Text(`${this.status.device_name} · ${this.status.service_id}`).fontSize(13).fontColor('#64748B')
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)

          Row({ space: 6 }) {
            Button(this.showDebug ? '收起调试' : '调试原始数据')
              .type(ButtonType.Capsule)
              .fontSize(12)
              .backgroundColor('#4B5563')
              .onClick(() => {
                this.showDebug = !this.showDebug;
              })

            Button(this.manualRefreshing ? '同步中' : '刷新')
              .enabled(!this.manualRefreshing)
              .type(ButtonType.Capsule)
              .fontSize(14)
              .backgroundColor('#0A7F5A')
              .onClick(() => {
                this.refreshStatus(true);
              })
          }
        }
        .width('100%')

        Row({ space: 8 }) {
          Text(this.status.online ? '设备在线' : '设备离线')
            .fontColor(this.status.online ? '#087F5B' : '#C0392B')
            .fontWeight(FontWeight.Medium)

          Text(`更新: ${this.status.timestamp}`)
            .fontSize(11)
            .fontColor('#94A3B8')
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .layoutWeight(1)
        }
        .width('100%')
        .margin({ top: 12 })
        .padding(12)
        .borderRadius(8)
        .backgroundColor(this.status.online ? '#EAF7F1' : '#FEECEB')

        if (this.errorMessage.length > 0) {
          Text(this.errorMessage)
            .width('100%')
            .fontColor('#B42318')
            .backgroundColor('#FEF3F2')
            .padding(12)
            .margin({ top: 10 })
            .borderRadius(8)
        }

        if (this.showDebug) {
          Column({ space: 6 }) {
            Text('🔍 华为云 IoTDA 原始响应 JSON:')
              .fontSize(14)
              .fontWeight(FontWeight.Bold)
              .fontColor('#1F2937')
            Scroll() {
              Text(this.rawJsonText)
                .fontSize(11)
                .fontColor('#111827')
                .fontFamily('monospace')
            }
            .width('100%')
            .height(200)
            .padding(10)
            .backgroundColor('#F3F4F6')
            .borderRadius(6)
          }
          .width('100%')
          .padding(12)
          .margin({ top: 10 })
          .borderRadius(8)
          .backgroundColor('#E5E7EB')
        }

        this.sectionTitle('环境状态')
        Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.SpaceBetween }) {
          // 平均光照
          Column({ space: 6 }) {
            Text('平均光照').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(`${this.lightAvg}`).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
              Text('lx').fontSize(12).fontColor('#7B8794')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')

          // 温度
          Column({ space: 6 }) {
            Text('温度').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(`${this.temp}`).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
              Text('°C').fontSize(12).fontColor('#7B8794')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')

          // 湿度
          Column({ space: 6 }) {
            Text('湿度').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(`${this.humi}`).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
              Text('%').fontSize(12).fontColor('#7B8794')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')

          // 天气
          Column({ space: 6 }) {
            Text('天气').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(this.weatherText(this.weatherStr)).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')
        }

        this.sectionTitle('四象限光照 (L1~L4)')
        Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.SpaceBetween }) {
          // 左上 (L1)
          Column({ space: 6 }) {
            Text('左上 (L1)').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(`${this.lightTl}`).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
              Text('lx').fontSize(12).fontColor('#7B8794')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')

          // 右上 (L2)
          Column({ space: 6 }) {
            Text('右上 (L2)').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(`${this.lightTr}`).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
              Text('lx').fontSize(12).fontColor('#7B8794')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')

          // 左下 (L3)
          Column({ space: 6 }) {
            Text('左下 (L3)').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(`${this.lightBl}`).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
              Text('lx').fontSize(12).fontColor('#7B8794')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')

          // 右下 (L4)
          Column({ space: 6 }) {
            Text('右下 (L4)').fontSize(13).fontColor('#6B7280')
            Row({ space: 4 }) {
              Text(`${this.lightBr}`).fontSize(24).fontWeight(FontWeight.Bold).fontColor('#17202A')
              Text('lx').fontSize(12).fontColor('#7B8794')
            }
          }
          .alignItems(HorizontalAlign.Start)
          .width('48%')
          .padding(14)
          .margin({ bottom: 10 })
          .borderRadius(8)
          .backgroundColor('#FFFFFF')
        }

        this.sectionTitle('追光姿态')
        Column({ space: 12 }) {
          Row() {
            Text('追光状态').layoutWeight(1).fontColor('#475569')
            Text(this.isTracking ? '自动追光中' : '已停止')
              .fontWeight(FontWeight.Bold)
              .fontColor(this.isTracking ? '#087F5B' : '#B54708')
          }
          Row() {
            Text('方位角 (Yaw)').layoutWeight(1).fontColor('#475569')
            Text(`当前 ${this.yaw}° / 目标 ${this.targetYaw}°`)
          }
          Row() {
            Text('俯仰角 (Pitch)').layoutWeight(1).fontColor('#475569')
            Text(`当前 ${this.pitch}° / 目标 ${this.targetPitch}°`)
          }
        }
        .width('100%')
        .padding(16)
        .borderRadius(8)
        .backgroundColor('#FFFFFF')

        this.sectionTitle('视觉入侵告警')
        Column({ space: 10 }) {
          Row() {
            Text(this.isWarning ? '存在目标入侵告警' : '运行正常')
              .layoutWeight(1)
              .fontSize(17)
              .fontWeight(FontWeight.Bold)
              .fontColor(this.isWarning ? '#B42318' : '#087F5B')
            Text(`${this.alarmItems.length} 条`).fontColor('#64748B')
          }
          if (this.alarmItems.length === 0) {
            Text('暂无目标入侵或视觉遮挡').width('100%').fontColor('#94A3B8')
          } else {
            ForEach(this.alarmItems, (item: AlarmItem) => {
              Column({ space: 3 }) {
                Text(`${item.level} · ${item.code}`).fontWeight(FontWeight.Bold).fontColor('#B42318')
                Text(item.message).fontColor('#475569')
                Text(item.time).fontSize(11).fontColor('#94A3B8')
              }
              .width('100%')
              .alignItems(HorizontalAlign.Start)
              .padding(10)
              .borderRadius(8)
              .backgroundColor('#FFF7ED')
            }, (item: AlarmItem) => `${item.code}_${item.time}`)
          }
        }
        .width('100%')
        .padding(16)
        .borderRadius(8)
        .backgroundColor('#FFFFFF')

        Text(`设备 ID：${this.status.device_id}`)
          .width('100%')
          .fontSize(11)
          .fontColor('#94A3B8')
          .margin({ top: 18, bottom: 12 })
      }
      .width('100%')
      .padding({
        left: 18,
        right: 18,
        top: this.topSafeHeight > 0 ? this.topSafeHeight + 12 : 20,
        bottom: this.bottomSafeHeight > 0 ? this.bottomSafeHeight + 16 : 24
      })
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#F3F6F8')
    .expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM])
  }
}
