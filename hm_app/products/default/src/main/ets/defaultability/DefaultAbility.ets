import { ConfigurationConstant, UIAbility } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';

const DOMAIN = 0x0000;

export default class DefaultAbility extends UIAbility {
  onCreate(): void {
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onCreate');
  }

  onDestroy(): void {
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onDestroy');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    // Main window is created, set main page for this ability
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onWindowStageCreate');

    // 1. 获取主窗口并开启沉浸式全面屏布局
    const mainWindow = windowStage.getMainWindowSync();
    try {
      // 开启全屏沉浸式布局（页面延伸至状态栏和导航条区域）
      mainWindow.setWindowLayoutFullScreen(true);

      // 获取顶部状态栏和底部导航条安全避让区高度并存入 AppStorage
      const typeTop = window.AvoidAreaType.TYPE_SYSTEM;
      const typeBottom = window.AvoidAreaType.TYPE_NAVIGATION_INDICATOR;

      const topArea = mainWindow.getWindowAvoidArea(typeTop);
      const bottomArea = mainWindow.getWindowAvoidArea(typeBottom);

      const topHeight = px2vp(topArea.topRect.height);
      const bottomHeight = px2vp(bottomArea.bottomRect.height);

      AppStorage.setOrCreate('topSafeHeight', topHeight > 0 ? topHeight : 38);
      AppStorage.setOrCreate('bottomSafeHeight', bottomHeight > 0 ? bottomHeight : 20);

      // 监听窗口尺寸及避让区变化（如旋转屏幕或隐藏导航栏）
      mainWindow.on('avoidAreaChange', (data) => {
        if (data.type === window.AvoidAreaType.TYPE_SYSTEM) {
          AppStorage.setOrCreate('topSafeHeight', px2vp(data.area.topRect.height));
        } else if (data.type === window.AvoidAreaType.TYPE_NAVIGATION_INDICATOR) {
          AppStorage.setOrCreate('bottomSafeHeight', px2vp(data.area.bottomRect.height));
        }
      });
    } catch (err) {
      hilog.error(DOMAIN, 'testTag', 'Failed to enable full screen. Cause: %{public}s', JSON.stringify(err));
    }

    // 2. 加载主页面
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(DOMAIN, 'testTag', 'Failed to load the content. Cause: %{public}s', JSON.stringify(err));
        return;
      }
      hilog.info(DOMAIN, 'testTag', 'Succeeded in loading the content.');
    });

    try {
      this.context.getApplicationContext().setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_NOT_SET);
    } catch (err) {
      hilog.error(DOMAIN, 'testTag', 'Failed to set colorMode. Cause: %{public}s', JSON.stringify(err));
    }
  }

  onWindowStageDestroy(): void {
    // Main window is destroyed, release UI related resources
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onWindowStageDestroy');
  }

  onForeground(): void {
    // Ability has brought to foreground
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onForeground');
  }

  onBackground(): void {
    // Ability has back to background
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onBackground');
  }
}
