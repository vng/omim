#import "Statistics.h"
#import "AppInfo.h"
#import "MWMCustomFacebookEvents.h"
#import "MWMSettings.h"

#import "3party/Alohalytics/src/alohalytics_objc.h"

#import <FBSDKCoreKit/FBSDKCoreKit.h>
#import <AdSupport/ASIdentifierManager.h>

#include "platform/platform.hpp"
#include "platform/settings.hpp"

#include "base/macros.hpp"

// If you have a "missing header error" here, then please run configure.sh script in the root repo folder.
#import "private.h"


@interface Statistics ()

@property(nonatomic) NSDate * lastLocationLogTimestamp;

@end

@implementation Statistics

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  // _enabled should be already correctly set up in init method.
  if ([MWMSettings statisticsEnabled])
  {
    [Alohalytics setup:@(ALOHALYTICS_URL) withLaunchOptions:launchOptions];
  }
  // Always call Facebook method, looks like it is required to handle some url schemes and sign on scenarios.
  return [[FBSDKApplicationDelegate sharedInstance] application:application didFinishLaunchingWithOptions:launchOptions];
}

- (void)logEvent:(NSString *)eventName withParameters:(NSDictionary *)parameters
{
  if (![MWMSettings statisticsEnabled])
    return;
  NSMutableDictionary * params = [self addDefaultAttributesToParameters:parameters];
  [Alohalytics logEvent:eventName withDictionary:params];
}

- (void)logEvent:(NSString *)eventName withParameters:(NSDictionary *)parameters atLocation:(CLLocation *)location
{
  if (![MWMSettings statisticsEnabled])
    return;
  NSMutableDictionary * params = [self addDefaultAttributesToParameters:parameters];
  [Alohalytics logEvent:eventName withDictionary:params atLocation:location];
  auto const & coordinate = location ? location.coordinate : kCLLocationCoordinate2DInvalid;
  params[kStatLocation] = makeLocationEventValue(coordinate.latitude, coordinate.longitude);
}

- (NSMutableDictionary *)addDefaultAttributesToParameters:(NSDictionary *)parameters
{
  NSMutableDictionary * params = [parameters mutableCopy];
  BOOL isLandscape = UIDeviceOrientationIsLandscape(UIDevice.currentDevice.orientation);
  params[kStatOrientation] = isLandscape ? kStatLandscape : kStatPortrait;
  AppInfo * info = [AppInfo sharedInfo];
  params[kStatCountry] = info.countryCode;
  if (info.languageId)
    params[kStatLanguage] = info.languageId;
  return params;
}

- (void)logEvent:(NSString *)eventName
{
  [self logEvent:eventName withParameters:@{}];
}

- (void)logApiUsage:(NSString *)programName
{
  if (![MWMSettings statisticsEnabled])
    return;
  if (programName)
    [self logEvent:@"Api Usage" withParameters: @{@"Application Name" : programName}];
  else
    [self logEvent:@"Api Usage" withParameters: @{@"Application Name" : @"Error passing nil as SourceApp name."}];
}

- (void)applicationDidBecomeActive
{
  if (![MWMSettings statisticsEnabled])
    return;
  [FBSDKAppEvents activateApp];
  // Special FB events to improve marketing campaigns quality.
  [MWMCustomFacebookEvents optimizeExpenses];
}

+ (instancetype)instance
{
  static Statistics * instance;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^
  {
    instance = [[self alloc] init];
  });
  return instance;
}

+ (void)logEvent:(NSString *)eventName
{
  [[self instance] logEvent:eventName];
}

+ (void)logEvent:(NSString *)eventName withParameters:(NSDictionary *)parameters
{
  [[self instance] logEvent:eventName withParameters:parameters];
}

+ (void)logEvent:(NSString *)eventName withParameters:(NSDictionary *)parameters atLocation:(CLLocation *)location
{
  [[self instance] logEvent:eventName withParameters:parameters atLocation:location];
}

+ (NSString *)connectionTypeString
{
  switch (Platform::ConnectionStatus())
  {
  case Platform::EConnectionType::CONNECTION_WWAN:
    return kStatMobile;
  case Platform::EConnectionType::CONNECTION_WIFI:
    return kStatWifi;
  case Platform::EConnectionType::CONNECTION_NONE:
    return kStatNone;
  }
}

@end
