#import "MWMPushNotifications.h"

#import "3party/Alohalytics/src/alohalytics_objc.h"

// If you have a "missing header error" here, then please run configure.sh script in the root repo
// folder.
#import "private.h"

#include "std/string.hpp"

namespace
{
NSString * const kPushDeviceTokenLogEvent = @"iOSPushDeviceToken";
}  // namespace

@implementation MWMPushNotifications

+ (void)setup:(NSDictionary *)launchOptions
{
}

+ (void)application:(UIApplication *)application
    didRegisterForRemoteNotificationsWithDeviceToken:(NSData *)deviceToken
{
}

+ (void)application:(UIApplication *)application
    didFailToRegisterForRemoteNotificationsWithError:(NSError *)error
{
}

+ (void)application:(UIApplication *)application
    didReceiveRemoteNotification:(NSDictionary *)userInfo
          fetchCompletionHandler:(void (^)(UIBackgroundFetchResult))completionHandler
{
  completionHandler(UIBackgroundFetchResultNoData);
}

+ (BOOL)handleURLPush:(NSDictionary *)userInfo
{
  auto app = UIApplication.sharedApplication;
  CLS_LOG(@"Handle url push");
  CLS_LOG(@"User info: %@", userInfo);
  if (app.applicationState != UIApplicationStateInactive)
    return NO;
  NSString * openLink = userInfo[@"openURL"];
  CLS_LOG(@"Push's url: %@", openLink);
  if (!openLink)
    return NO;
  NSURL * url = [NSURL URLWithString:openLink];
  [app openURL:url];
  return YES;
}

@end
