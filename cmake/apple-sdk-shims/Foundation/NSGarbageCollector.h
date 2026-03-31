#pragma once

#import <Foundation/NSObject.h>

NS_HEADER_AUDIT_BEGIN(nullability, sendability)

@interface NSGarbageCollector : NSObject
+ (NSGarbageCollector*) defaultCollector;
+ (BOOL) isCollecting;
- (void) disable;
- (void) enable;
- (BOOL) isEnabled;
- (BOOL) isCollecting;
- (void) collectExhaustively;
@end

NS_HEADER_AUDIT_END(nullability, sendability)
