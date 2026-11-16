// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Small, separately-signable macOS window capture helper. Keeping this in its
// own app bundle lets the user grant Screen Recording to this tool rather than
// to the terminal or the game.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static void copy_string(CFStringRef value, char *buffer, size_t size, const char *fallback) {
	if (value == NULL || !CFStringGetCString(value, buffer, (CFIndex)size, kCFStringEncodingUTF8)) {
		snprintf(buffer, size, "%s", fallback);
	}
}


static bool owner_matches(CFDictionaryRef window, const char *owner_filter) {
	if (owner_filter == NULL) {
		return true;
	}
	CFStringRef owner = CFDictionaryGetValue(window, kCGWindowOwnerName);
	if (owner == NULL) {
		return false;
	}
	CFStringRef filter = CFStringCreateWithCString(kCFAllocatorDefault, owner_filter, kCFStringEncodingUTF8);
	if (filter == NULL) {
		return false;
	}
	CFRange found = CFStringFind(owner, filter, kCFCompareCaseInsensitive);
	CFRelease(filter);
	return found.location != kCFNotFound;
}


static bool window_number(CFDictionaryRef window, CGWindowID *window_id) {
	CFNumberRef number = CFDictionaryGetValue(window, kCGWindowNumber);
	return number != NULL
	       && CFNumberGetValue(number, kCFNumberSInt32Type, window_id);
}


static CFArrayRef copy_windows(void) {
	const uint32_t options = kCGWindowListOptionOnScreenOnly
	                         | kCGWindowListExcludeDesktopElements;
	return CGWindowListCopyWindowInfo(options, kCGNullWindowID);
}


static bool find_window(const char *owner_filter, CGWindowID *result) {
	CFArrayRef windows = copy_windows();
	if (windows == NULL) {
		return false;
	}
	bool found = false;
	for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
		CFDictionaryRef window = CFArrayGetValueAtIndex(windows, i);
		CGWindowID window_id = kCGNullWindowID;
		if (owner_matches(window, owner_filter) && window_number(window, &window_id)) {
			*result = window_id;
			found = true;
			break;
		}
	}
	CFRelease(windows);
	return found;
}


static int list_windows(const char *owner_filter) {
	CFArrayRef windows = copy_windows();
	if (windows == NULL) {
		fprintf(stderr, "Could not enumerate macOS windows.\n");
		return 5;
	}
	int matches = 0;
	for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
		CFDictionaryRef window = CFArrayGetValueAtIndex(windows, i);
		CGWindowID window_id = kCGNullWindowID;
		if (!owner_matches(window, owner_filter) || !window_number(window, &window_id)) {
			continue;
		}
		char owner[256];
		char title[1024];
		copy_string(CFDictionaryGetValue(window, kCGWindowOwnerName), owner, sizeof(owner), "<unknown>");
		copy_string(CFDictionaryGetValue(window, kCGWindowName), title, sizeof(title), "<untitled>");
		CGRect bounds = CGRectZero;
		CGRectMakeWithDictionaryRepresentation(CFDictionaryGetValue(window, kCGWindowBounds), &bounds);
		printf("id=%u owner=%s title=%s bounds=%.0f,%.0f,%.0f,%.0f\n",
		       window_id, owner, title,
		       bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
		matches += 1;
	}
	CFRelease(windows);
	if (matches == 0) {
		fprintf(stderr, "No on-screen window matched owner '%s'.\n", owner_filter);
		return 4;
	}
	return 0;
}


static int permission_status(bool request) {
	if (request) {
		CGRequestScreenCaptureAccess();
	}
	if (CGPreflightScreenCaptureAccess()) {
		printf("SCREEN_RECORDING_GRANTED\n");
		return 0;
	}
	printf("SCREEN_RECORDING_NOT_GRANTED\n");
	printf("Grant OpenageWindowCapture access under System Settings > Privacy & Security > Screen Recording.\n");
	return 3;
}


static int accessibility_status(bool request) {
	NSDictionary *options = @{
		(__bridge id)kAXTrustedCheckOptionPrompt: @(request),
	};
	if (AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options)) {
		printf("ACCESSIBILITY_GRANTED\n");
		return 0;
	}
	printf("ACCESSIBILITY_NOT_GRANTED\n");
	printf("Grant OpenageWindowCapture access under System Settings > Privacy & Security > Accessibility.\n");
	return 6;
}


static int write_png(CGImageRef image, const char *output_path) {
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(
		kCFAllocatorDefault, (const UInt8 *)output_path, (CFIndex)strlen(output_path), false);
	if (url == NULL) {
		fprintf(stderr, "Invalid output path: %s\n", output_path);
		return 2;
	}
	CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
	CFRelease(url);
	if (destination == NULL) {
		fprintf(stderr, "Could not create PNG destination: %s\n", output_path);
		return 5;
	}
	CGImageDestinationAddImage(destination, image, NULL);
	bool ok = CGImageDestinationFinalize(destination);
	CFRelease(destination);
	if (!ok) {
		fprintf(stderr, "Could not write PNG: %s\n", output_path);
		return 5;
	}
	return 0;
}


static int capture_window(CGWindowID window_id, const char *output_path) {
	if (!CGPreflightScreenCaptureAccess()) {
		return permission_status(false);
	}
	__block int result = 5;
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	[SCShareableContent getShareableContentExcludingDesktopWindows:YES
	                                          onScreenWindowsOnly:YES
	                                            completionHandler:^(SCShareableContent *content, NSError *content_error) {
		if (content_error != nil || content == nil) {
			fprintf(stderr, "ScreenCaptureKit could not enumerate windows: %s\n",
			        content_error.localizedDescription.UTF8String);
			dispatch_semaphore_signal(done);
			return;
		}
		SCWindow *matched_window = nil;
		for (SCWindow *window in content.windows) {
			if (window.windowID == window_id) {
				matched_window = window;
				break;
			}
		}
		if (matched_window == nil) {
			fprintf(stderr, "ScreenCaptureKit could not find on-screen window id=%u.\n", window_id);
			result = 4;
			dispatch_semaphore_signal(done);
			return;
		}
		SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:matched_window];
		SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
		configuration.showsCursor = NO;
		configuration.ignoreShadowsSingleWindow = YES;
		[SCScreenshotManager captureImageWithFilter:filter
		                              configuration:configuration
		                          completionHandler:^(CGImageRef image, NSError *capture_error) {
			if (capture_error != nil || image == NULL) {
				fprintf(stderr, "ScreenCaptureKit could not capture window id=%u: %s\n",
				        window_id, capture_error.localizedDescription.UTF8String);
				dispatch_semaphore_signal(done);
				return;
			}
			result = write_png(image, output_path);
			if (result == 0) {
				printf("SCREENSHOT_SAVED id=%u path=%s width=%zu height=%zu\n",
				       window_id, output_path, CGImageGetWidth(image), CGImageGetHeight(image));
			}
			dispatch_semaphore_signal(done);
		}];
	}];
	if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) != 0) {
		fprintf(stderr, "ScreenCaptureKit timed out after 30 seconds for window id=%u.\n", window_id);
		return 5;
	}
	return result;
}


static int click_in_window(const char *owner_filter,
                           double relative_x,
                           double relative_y,
                           bool right_button) {	NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @NO};
	if (!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options)) {
		return accessibility_status(false);
	}

	CGWindowID window_id = kCGNullWindowID;
	if (!find_window(owner_filter, &window_id)) {
		fprintf(stderr, "No on-screen window matched owner '%s'.\n", owner_filter);
		return 4;
	}

	CFArrayRef windows = copy_windows();
	if (windows == NULL) {
		return 5;
	}
	CGRect bounds = CGRectZero;
	pid_t owner_pid = -1;
	bool found = false;
	for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
		CFDictionaryRef window = CFArrayGetValueAtIndex(windows, i);
		CGWindowID candidate = kCGNullWindowID;
		if (!window_number(window, &candidate) || candidate != window_id) {
			continue;
		}
		CGRectMakeWithDictionaryRepresentation(
			CFDictionaryGetValue(window, kCGWindowBounds), &bounds);
		CFNumberRef pid_number = CFDictionaryGetValue(window, kCGWindowOwnerPID);
		if (pid_number != NULL) {
			CFNumberGetValue(pid_number, kCFNumberSInt32Type, &owner_pid);
		}
		found = true;
		break;
	}
	CFRelease(windows);
	if (!found || owner_pid < 0) {
		return 4;
	}
	if (relative_x < 0 || relative_y < 0
	    || relative_x >= bounds.size.width || relative_y >= bounds.size.height) {
		fprintf(stderr,
		        "Refusing click outside window id=%u: relative=(%.1f,%.1f) bounds=%.0fx%.0f.\n",
		        window_id, relative_x, relative_y, bounds.size.width, bounds.size.height);
		return 2;
	}

	NSRunningApplication *application =
		[NSRunningApplication runningApplicationWithProcessIdentifier:owner_pid];
	[application activateWithOptions:NSApplicationActivateAllWindows];
	usleep(100000);

	CGPoint point = CGPointMake(bounds.origin.x + relative_x,
	                            bounds.origin.y + relative_y);
	CGMouseButton button = right_button ? kCGMouseButtonRight : kCGMouseButtonLeft;
	CGEventType down_type = right_button ? kCGEventRightMouseDown : kCGEventLeftMouseDown;
	CGEventType up_type = right_button ? kCGEventRightMouseUp : kCGEventLeftMouseUp;
	CGEventRef move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, point, button);
	CGEventRef down = CGEventCreateMouseEvent(NULL, down_type, point, button);
	CGEventRef up = CGEventCreateMouseEvent(NULL, up_type, point, button);
	if (move == NULL || down == NULL || up == NULL) {
		if (move != NULL) CFRelease(move);
		if (down != NULL) CFRelease(down);
		if (up != NULL) CFRelease(up);
		return 5;
	}
	CGEventPost(kCGHIDEventTap, move);
	usleep(50000);
	CGEventPost(kCGHIDEventTap, down);
	usleep(80000);
	CGEventPost(kCGHIDEventTap, up);
	CFRelease(move);
	CFRelease(down);
	CFRelease(up);
	printf("CLICK_POSTED id=%u button=%s relative=%.1f,%.1f global=%.1f,%.1f\n",
	       window_id, right_button ? "right" : "left",
	       relative_x, relative_y, point.x, point.y);
	return 0;
}


static int press_arrow_key(const char *owner_filter, CGKeyCode key_code, int count) {
	NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @NO};
	if (!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options)) {
		return accessibility_status(false);
	}
	CGWindowID window_id = kCGNullWindowID;
	if (!find_window(owner_filter, &window_id)) {
		fprintf(stderr, "No on-screen window matched owner '%s'.\n", owner_filter);
		return 4;
	}
	CFArrayRef windows = copy_windows();
	if (windows == NULL) {
		return 5;
	}
	pid_t owner_pid = -1;
	for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
		CFDictionaryRef window = CFArrayGetValueAtIndex(windows, i);
		CGWindowID candidate = kCGNullWindowID;
		if (window_number(window, &candidate) && candidate == window_id) {
			CFNumberRef pid_number = CFDictionaryGetValue(window, kCGWindowOwnerPID);
			if (pid_number != NULL) {
				CFNumberGetValue(pid_number, kCFNumberSInt32Type, &owner_pid);
			}
			break;
		}
	}
	CFRelease(windows);
	if (owner_pid < 0) {
		return 4;
	}
	NSRunningApplication *application =
		[NSRunningApplication runningApplicationWithProcessIdentifier:owner_pid];
	[application activateWithOptions:NSApplicationActivateAllWindows];
	usleep(100000);
	for (int i = 0; i < count; ++i) {
		CGEventRef down = CGEventCreateKeyboardEvent(NULL, key_code, true);
		CGEventRef up = CGEventCreateKeyboardEvent(NULL, key_code, false);
		if (down == NULL || up == NULL) {
			if (down != NULL) CFRelease(down);
			if (up != NULL) CFRelease(up);
			return 5;
		}
		CGEventPost(kCGHIDEventTap, down);
		CGEventPost(kCGHIDEventTap, up);
		CFRelease(down);
		CFRelease(up);
		usleep(50000);
	}
	printf("KEY_POSTED id=%u key_code=%u count=%d\n", window_id, key_code, count);
	return 0;
}


static void usage(const char *program) {
	fprintf(stderr,
	        "Usage:\n"
	        "  %s list [--owner NAME]\n"
	        "  %s capture WINDOW_ID ABSOLUTE_OUTPUT.png\n"
	        "  %s capture --owner NAME ABSOLUTE_OUTPUT.png\n"
	        "  %s permission [--request]\n"
	        "  %s accessibility [--request]\n"
	        "  %s click --owner NAME X Y [left|right]\n"
	        "  %s key --owner NAME up|down|left|right [COUNT]\n",
	        program, program, program, program, program, program, program);
}


static bool parse_coordinate(const char *text, double *value) {
	char *end = NULL;
	double parsed = strtod(text, &end);
	if (end == text || *end != '\0' || !isfinite(parsed)) {
		return false;
	}
	*value = parsed;
	return true;
}


int main(int argc, char **argv) {
	// ScreenCaptureKit needs a WindowServer connection even for one-shot CLI
	// captures. LaunchServices alone does not initialize ApplicationServices.
	[NSApplication sharedApplication];

	if (argc >= 2 && strcmp(argv[1], "accessibility") == 0) {
		bool request = argc == 3 && strcmp(argv[2], "--request") == 0;
		if (argc > 3 || (argc == 3 && !request)) {
			usage(argv[0]);
			return 2;
		}
		return accessibility_status(request);
	}
	if (argc >= 2 && strcmp(argv[1], "key") == 0) {
		if ((argc == 5 || argc == 6) && strcmp(argv[2], "--owner") == 0) {
			CGKeyCode key_code;
			if (strcmp(argv[4], "up") == 0) key_code = 126;
			else if (strcmp(argv[4], "down") == 0) key_code = 125;
			else if (strcmp(argv[4], "left") == 0) key_code = 123;
			else if (strcmp(argv[4], "right") == 0) key_code = 124;
			else {
				usage(argv[0]);
				return 2;
			}
			int count = 1;
			if (argc == 6) {
				char *end = NULL;
				long parsed = strtol(argv[5], &end, 10);
				if (end == argv[5] || *end != '\0' || parsed < 1 || parsed > 100) {
					fprintf(stderr, "Key count must be an integer from 1 to 100.\n");
					return 2;
				}
				count = (int)parsed;
			}
			return press_arrow_key(argv[3], key_code, count);
		}
		usage(argv[0]);
		return 2;
	}
	if (argc >= 2 && strcmp(argv[1], "click") == 0) {
		if ((argc == 6 || argc == 7) && strcmp(argv[2], "--owner") == 0) {
			bool right_button = argc == 7 && strcmp(argv[6], "right") == 0;
			bool valid_button = argc == 6
			                    || (argc == 7
			                        && (strcmp(argv[6], "left") == 0 || right_button));
			if (valid_button) {
				double x = 0.0;
				double y = 0.0;
				if (!parse_coordinate(argv[4], &x)
				    || !parse_coordinate(argv[5], &y)) {
					fprintf(stderr, "Click coordinates must be finite numbers.\n");
					return 2;
				}
				return click_in_window(argv[3], x, y, right_button);
			}
		}
		usage(argv[0]);
		return 2;
	}
	if (argc >= 2 && strcmp(argv[1], "permission") == 0) {
		bool request = argc == 3 && strcmp(argv[2], "--request") == 0;
		if (argc > 3 || (argc == 3 && !request)) {
			usage(argv[0]);
			return 2;
		}
		return permission_status(request);
	}
	if (argc >= 2 && strcmp(argv[1], "list") == 0) {
		if (argc == 2) {
			return list_windows(NULL);
		}
		if (argc == 4 && strcmp(argv[2], "--owner") == 0) {
			return list_windows(argv[3]);
		}
		usage(argv[0]);
		return 2;
	}
	if (argc >= 2 && strcmp(argv[1], "capture") == 0) {
		CGWindowID window_id = kCGNullWindowID;
		const char *output_path = NULL;
		if (argc == 4) {
			window_id = (CGWindowID)strtoul(argv[2], NULL, 10);
			output_path = argv[3];
		}
		else if (argc == 5 && strcmp(argv[2], "--owner") == 0) {
			if (!find_window(argv[3], &window_id)) {
				fprintf(stderr, "No on-screen window matched owner '%s'.\n", argv[3]);
				return 4;
			}
			output_path = argv[4];
		}
		if (window_id != kCGNullWindowID && output_path != NULL && output_path[0] == '/') {
			return capture_window(window_id, output_path);
		}
	}
	usage(argv[0]);
	return 2;
}
