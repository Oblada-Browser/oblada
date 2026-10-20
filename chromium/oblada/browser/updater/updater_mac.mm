#include "oblada/browser/updater/updater_platform.h"

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

#include <algorithm>
#include <string>

#include "base/apple/bundle_locations.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "oblada/browser/updater/updater.h"

// Sparkle as Oblada uses it: without any window of Sparkle's, it reports to updater.cc
// (the About page) and keeps the update that's ready until Oblada quits or relaunches.
// Sparkle.framework is in the app's Frameworks and loaded at startup, rather than
// linked: only the browser process needs it, and only an app packaged for release.
@interface ObladaSparkleDriver : NSObject <SPUUserDriver, SPUUpdaterDelegate>
@property(nonatomic, strong) SPUUpdater* updater;
- (void)checkNow;
- (void)installNow;
@end

@implementation ObladaSparkleDriver {
  oblada::updater::UpdaterStatus _status;
  // Installs the update that's ready, and quits and relaunches Oblada.
  void (^_installNow)(void);
  uint64_t _expectedLength;
  uint64_t _receivedLength;
}

- (void)report:(oblada::updater::UpdaterStatus::State)state {
  _status.state = state;
  if (state != oblada::updater::UpdaterStatus::State::kDownloading) {
    _status.progress = 0;
  }
  if (state != oblada::updater::UpdaterStatus::State::kFailed) {
    _status.error.clear();
  }
  oblada::updater::SetStatus(_status);
}

- (void)setVersion:(SUAppcastItem*)item {
  _status.version = base::SysNSStringToUTF8(item.displayVersionString);
}

- (void)setReady:(void (^)(void))installNow {
  _installNow = installNow;
  [self report:oblada::updater::UpdaterStatus::State::kReady];
}

- (void)fail:(NSError*)error {
  // An update that's ready stays so: a later check failing doesn't change it.
  if (_status.state == oblada::updater::UpdaterStatus::State::kReady) {
    return;
  }
  LOG(ERROR) << "Oblada's updater: " << base::SysNSStringToUTF8(error.description);
  _status.error = base::SysNSStringToUTF16(error.localizedDescription);
  [self report:oblada::updater::UpdaterStatus::State::kFailed];
}

- (void)checkNow {
  if (_status.state == oblada::updater::UpdaterStatus::State::kReady ||
      self.updater.sessionInProgress) {
    return;
  }
  [self report:oblada::updater::UpdaterStatus::State::kChecking];
  [self.updater checkForUpdatesInBackground];
}

- (void)installNow {
  if (_installNow) {
    _installNow();
  }
}

// SPUUserDriver. Sparkle comes here for what it doesn't do silently (an update marked
// critical, or any when automatic downloads are off): Oblada answers as automatic
// updates would, without asking.

- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request
                              reply:(void (^)(SUUpdatePermissionResponse*))reply {
  // The app's Info.plist turns automatic checks on (SUEnableAutomaticChecks), so Sparkle
  // doesn't ask; if it did, the answer is the same.
  reply([[NSClassFromString(@"SUUpdatePermissionResponse") alloc]
      initWithAutomaticUpdateChecks:YES
                  sendSystemProfile:NO]);
}

- (void)showUserInitiatedUpdateCheckWithCancellation:
    (void (^)(void))cancellation {
  [self report:oblada::updater::UpdaterStatus::State::kChecking];
}

- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)item
                                 state:(SPUUserUpdateState*)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply {
  if (item.informationOnlyUpdate) {
    reply(SPUUserUpdateChoiceDismiss);
    return;
  }
  [self setVersion:item];
  if (state.stage == SPUUserUpdateStageNotDownloaded) {
    // Downloaded, as automatic updates are.
    [self report:oblada::updater::UpdaterStatus::State::kDownloading];
    reply(SPUUserUpdateChoiceInstall);
    return;
  }
  // Downloaded already: installed when Oblada quits, or now from the About page.
  [self setReady:^{
    reply(SPUUserUpdateChoiceInstall);
  }];
}

- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)downloadData {
}

- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error {
}

- (void)showUpdateNotFoundWithError:(NSError*)error
                    acknowledgement:(void (^)(void))acknowledgement {
  [self report:oblada::updater::UpdaterStatus::State::kUpToDate];
  acknowledgement();
}

- (void)showUpdaterError:(NSError*)error
         acknowledgement:(void (^)(void))acknowledgement {
  [self fail:error];
  acknowledgement();
}

- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation {
  _expectedLength = 0;
  _receivedLength = 0;
  [self report:oblada::updater::UpdaterStatus::State::kDownloading];
}

- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)expectedContentLength {
  _expectedLength = expectedContentLength;
}

- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length {
  _receivedLength += length;
  if (_expectedLength == 0) {
    return;
  }
  // Reported once per percent, not for every chunk.
  int progress = static_cast<int>(
      std::min<uint64_t>(100, _receivedLength * 100 / _expectedLength));
  if (progress != _status.progress) {
    _status.progress = progress;
    [self report:oblada::updater::UpdaterStatus::State::kDownloading];
  }
}

- (void)showDownloadDidStartExtractingUpdate {
}

- (void)showExtractionReceivedProgress:(double)progress {
}

- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply {
  [self setReady:^{
    reply(SPUUserUpdateChoiceInstall);
  }];
}

- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)applicationTerminated
                          retryTerminatingApplication:
                              (void (^)(void))retryTerminatingApplication {
  // Sparkle replaces the app once Oblada has quit: quit as the menu's Quit does. If
  // that's cancelled (a page asking to stay), the About page's button tries again.
  _installNow = ^{
    retryTerminatingApplication();
    chrome::AttemptExit();
  };
  if (!applicationTerminated) {
    chrome::AttemptExit();
  }
}

- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched
                         acknowledgement:(void (^)(void))acknowledgement {
  acknowledgement();
}

- (void)dismissUpdateInstallation {
}

// SPUUpdaterDelegate. Sparkle's own checks, once a day, come here: an update they find
// is downloaded in the background, then waits for Oblada to quit.

- (BOOL)updater:(SPUUpdater*)updater
    mayPerformUpdateCheck:(SPUUpdateCheck)updateCheck
                    error:(NSError* __autoreleasing*)error {
  if (_status.state != oblada::updater::UpdaterStatus::State::kReady) {
    [self report:oblada::updater::UpdaterStatus::State::kChecking];
  }
  return YES;
}

- (void)updater:(SPUUpdater*)updater didFindValidUpdate:(SUAppcastItem*)item {
  [self setVersion:item];
  [self report:oblada::updater::UpdaterStatus::State::kDownloading];
}

- (void)updaterDidNotFindUpdate:(SPUUpdater*)updater error:(NSError*)error {
  [self report:oblada::updater::UpdaterStatus::State::kUpToDate];
}

- (void)updater:(SPUUpdater*)updater didAbortWithError:(NSError*)error {
  if ([error.domain isEqualToString:@"SUSparkleErrorDomain"] &&
      error.code == SUNoUpdateError) {
    return;
  }
  [self fail:error];
}

- (BOOL)updater:(SPUUpdater*)updater
    willInstallUpdateOnQuit:(SUAppcastItem*)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler {
  [self setVersion:item];
  [self setReady:immediateInstallHandler];
  return YES;
}

- (BOOL)updaterShouldRelaunchApplication:(SPUUpdater*)updater {
  // A headless Oblada (tests, automation) isn't opened again as a window.
  return !base::CommandLine::ForCurrentProcess()->HasSwitch("headless");
}

@end

namespace oblada::updater {

namespace {

ObladaSparkleDriver* __strong g_driver;

}  // namespace

bool StartPlatformUpdater() {
  NSBundle* app = base::apple::OuterBundle();
  // An app packaged for release has them (dmg.sh); a build doesn't update itself.
  if (![app objectForInfoDictionaryKey:@"SUFeedURL"] ||
      ![app objectForInfoDictionaryKey:@"SUPublicEDKey"]) {
    return false;
  }
  NSBundle* sparkle = [NSBundle
      bundleWithURL:[app.privateFrameworksURL
                        URLByAppendingPathComponent:@"Sparkle.framework"]];
  NSError* error = nil;
  if (![sparkle loadAndReturnError:&error]) {
    LOG(ERROR) << "Oblada's updater: Sparkle didn't load: "
               << base::SysNSStringToUTF8(error.description);
    return false;
  }
  ObladaSparkleDriver* driver = [[ObladaSparkleDriver alloc] init];
  SPUUpdater* updater = [[NSClassFromString(@"SPUUpdater") alloc]
      initWithHostBundle:app
       applicationBundle:app
              userDriver:driver
                delegate:driver];
  if (![updater startUpdater:&error]) {
    LOG(ERROR) << "Oblada's updater: "
               << base::SysNSStringToUTF8(error.description);
    return false;
  }
  driver.updater = updater;
  g_driver = driver;
  return true;
}

void CheckNow() {
  [g_driver checkNow];
}

void InstallNow() {
  [g_driver installNow];
}

}  // namespace oblada::updater
