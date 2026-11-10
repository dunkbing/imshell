// the native half of hello: appkit controls for mydear::NativeView, and a probe that
// reports where the shell put one
#import <AppKit/AppKit.h>
#include <cstdio>

void* makeTextField() {
    NSTextField* field = [NSTextField textFieldWithString:@""];
    field.placeholderString = @"A native NSTextField";
    return (__bridge_retained void*)field;
}

void* makeDatePicker() {
    NSDatePicker* picker = [[NSDatePicker alloc] init];
    picker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    picker.dateValue = [NSDate date];
    return (__bridge_retained void*)picker;
}

// "x,y wxh shown|hidden" of the view's clip box, in window points from the top left
void describeNativeView(void* view, char* out, int size) {
    NSView* clip = ((__bridge NSView*)view).superview;
    const NSRect r = clip.frame;
    std::snprintf(out, size, "%.0f,%.0f %.0fx%.0f %s", r.origin.x, r.origin.y, r.size.width,
                  r.size.height, clip.hidden ? "hidden" : "shown");
}

// the probe closes the window the way the close button does (windowShouldClose:)
void closeLikeUser() {
    [NSApp.windows.firstObject performClose:nil];
}
