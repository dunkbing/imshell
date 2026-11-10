#include "mydear/app.hpp"
#include "mydear/ios.h"
#include "mydear/themes.hpp"
#include "../native_view.hpp"
#include "../touch.hpp"

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
    MTKView* gView = nil;
    id<MTLCommandQueue> gQueue = nil;
    CFTimeInterval gLastTime = 0;
    CGFloat gKeyboardInset = 0; // how much of the view the soft keyboard covers
    mydear::App* gApp = nullptr; // created from the root controller's loadView
} // namespace

// ---- the view: touches, the ipad pointer and keys become imgui input ----

namespace {
    namespace touch = mydear::touch; // the shared one-finger model (src/touch.cpp)

    ImVec2 vec(CGPoint p) {
        return ImVec2(static_cast<float>(p.x), static_cast<float>(p.y));
    }

    ImGuiKey keyFor(UIKeyboardHIDUsage u) {
        if (u >= UIKeyboardHIDUsageKeyboardA && u <= UIKeyboardHIDUsageKeyboardZ)
            return static_cast<ImGuiKey>(ImGuiKey_A + (u - UIKeyboardHIDUsageKeyboardA));
        if (u >= UIKeyboardHIDUsageKeyboard1 && u <= UIKeyboardHIDUsageKeyboard9)
            return static_cast<ImGuiKey>(ImGuiKey_1 + (u - UIKeyboardHIDUsageKeyboard1));
        if (u >= UIKeyboardHIDUsageKeyboardF1 && u <= UIKeyboardHIDUsageKeyboardF12)
            return static_cast<ImGuiKey>(ImGuiKey_F1 + (u - UIKeyboardHIDUsageKeyboardF1));
        switch (u) {
        case UIKeyboardHIDUsageKeyboard0:
            return ImGuiKey_0;
        case UIKeyboardHIDUsageKeyboardReturnOrEnter:
            return ImGuiKey_Enter;
        case UIKeyboardHIDUsageKeypadEnter:
            return ImGuiKey_KeypadEnter;
        case UIKeyboardHIDUsageKeyboardEscape:
            return ImGuiKey_Escape;
        case UIKeyboardHIDUsageKeyboardDeleteOrBackspace:
            return ImGuiKey_Backspace;
        case UIKeyboardHIDUsageKeyboardDeleteForward:
            return ImGuiKey_Delete;
        case UIKeyboardHIDUsageKeyboardTab:
            return ImGuiKey_Tab;
        case UIKeyboardHIDUsageKeyboardSpacebar:
            return ImGuiKey_Space;
        case UIKeyboardHIDUsageKeyboardHome:
            return ImGuiKey_Home;
        case UIKeyboardHIDUsageKeyboardEnd:
            return ImGuiKey_End;
        case UIKeyboardHIDUsageKeyboardPageUp:
            return ImGuiKey_PageUp;
        case UIKeyboardHIDUsageKeyboardPageDown:
            return ImGuiKey_PageDown;
        case UIKeyboardHIDUsageKeyboardLeftArrow:
            return ImGuiKey_LeftArrow;
        case UIKeyboardHIDUsageKeyboardRightArrow:
            return ImGuiKey_RightArrow;
        case UIKeyboardHIDUsageKeyboardUpArrow:
            return ImGuiKey_UpArrow;
        case UIKeyboardHIDUsageKeyboardDownArrow:
            return ImGuiKey_DownArrow;
        case UIKeyboardHIDUsageKeyboardSlash:
            return ImGuiKey_Slash;
        case UIKeyboardHIDUsageKeyboardLeftControl:
            return ImGuiKey_LeftCtrl;
        case UIKeyboardHIDUsageKeyboardRightControl:
            return ImGuiKey_RightCtrl;
        case UIKeyboardHIDUsageKeyboardLeftShift:
            return ImGuiKey_LeftShift;
        case UIKeyboardHIDUsageKeyboardRightShift:
            return ImGuiKey_RightShift;
        case UIKeyboardHIDUsageKeyboardLeftAlt:
            return ImGuiKey_LeftAlt;
        case UIKeyboardHIDUsageKeyboardRightAlt:
            return ImGuiKey_RightAlt;
        case UIKeyboardHIDUsageKeyboardLeftGUI:
            return ImGuiKey_LeftSuper;
        case UIKeyboardHIDUsageKeyboardRightGUI:
            return ImGuiKey_RightSuper;
        default:
            return ImGuiKey_None;
        }
    }

    // keys whose text comes through insertText:, so uikit must still see them
    bool typesText(ImGuiKey k) {
        return k == ImGuiKey_None || k == ImGuiKey_Space || k == ImGuiKey_Slash ||
               (k >= ImGuiKey_0 && k <= ImGuiKey_9) || (k >= ImGuiKey_A && k <= ImGuiKey_Z);
    }

    void tapKey(ImGuiKey k) {
        ImGui::GetIO().AddKeyEvent(k, true);
        ImGui::GetIO().AddKeyEvent(k, false);
    }

    UIView* gNoKeyboard = nil; // an empty input view: first responder, no keyboard

} // namespace

// UITextInput over a document of one position: it exists for the keyboard's hold-space
// trackpad, whose floating cursor turns into arrow keys. imgui keeps the real text
// ponytail: no positions, so no loupe or selection handles; those need the real text here
@interface MydearTextPosition : UITextPosition
@end
@implementation MydearTextPosition
@end

@interface MydearTextRange : UITextRange
@end
@implementation MydearTextRange
- (UITextPosition*)start {
    static MydearTextPosition* p = [MydearTextPosition new];
    return p;
}
- (UITextPosition*)end {
    return self.start;
}
- (BOOL)isEmpty {
    return YES;
}
@end

namespace {
    MydearTextRange* gRange = [MydearTextRange new];
    CGPoint gCursor; // floating cursor, as far as arrows have moved it
    int gMarked = 0; // characters of an ime composition already sent
} // namespace

@interface MydearView : MTKView <UITextInput>
@property(nonatomic) BOOL keyboardWanted;
@end

@implementation MydearView
@synthesize inputDelegate, markedTextStyle;
- (UITextRange*)selectedTextRange {
    return gRange;
}
- (void)setSelectedTextRange:(UITextRange*)range {
}
- (UITextRange*)markedTextRange {
    return nil;
}
// a composition replaces what it sent last time
- (void)setMarkedText:(NSString*)text selectedRange:(NSRange)range {
    for (; gMarked > 0; --gMarked)
        tapKey(ImGuiKey_Backspace);
    ImGui::GetIO().AddInputCharactersUTF8(text.UTF8String);
    gMarked = (int)text.length;
}
- (void)unmarkText {
    gMarked = 0;
}
- (NSString*)textInRange:(UITextRange*)range {
    return @"";
}
- (void)replaceRange:(UITextRange*)range withText:(NSString*)text {
    [self insertText:text];
}
- (UITextPosition*)beginningOfDocument {
    return gRange.start;
}
- (UITextPosition*)endOfDocument {
    return gRange.start;
}
- (UITextRange*)textRangeFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)to {
    return gRange;
}
- (UITextPosition*)positionFromPosition:(UITextPosition*)position offset:(NSInteger)offset {
    return offset ? nil : position;
}
- (UITextPosition*)positionFromPosition:(UITextPosition*)position
                            inDirection:(UITextLayoutDirection)direction
                                 offset:(NSInteger)offset {
    return offset ? nil : position;
}
- (NSComparisonResult)comparePosition:(UITextPosition*)a toPosition:(UITextPosition*)b {
    return NSOrderedSame;
}
- (NSInteger)offsetFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)to {
    return 0;
}
- (id<UITextInputTokenizer>)tokenizer {
    return [[UITextInputStringTokenizer alloc] initWithTextInput:self];
}
- (UITextPosition*)positionWithinRange:(UITextRange*)range
                   farthestInDirection:(UITextLayoutDirection)direction {
    return gRange.start;
}
- (UITextRange*)characterRangeByExtendingPosition:(UITextPosition*)position
                                      inDirection:(UITextLayoutDirection)direction {
    return gRange;
}
- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition*)position
                                          inDirection:(UITextStorageDirection)direction {
    return NSWritingDirectionLeftToRight;
}
- (void)setBaseWritingDirection:(NSWritingDirection)direction forRange:(UITextRange*)range {
}
- (CGRect)firstRectForRange:(UITextRange*)range {
    return CGRectZero;
}
- (CGRect)caretRectForPosition:(UITextPosition*)position {
    return CGRectZero;
}
- (NSArray<UITextSelectionRect*>*)selectionRectsForRange:(UITextRange*)range {
    return @[];
}
- (UITextPosition*)closestPositionToPoint:(CGPoint)point {
    return gRange.start;
}
- (UITextPosition*)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange*)range {
    return gRange.start;
}
- (UITextRange*)characterRangeAtPoint:(CGPoint)point {
    return gRange;
}
- (void)beginFloatingCursorAtPoint:(CGPoint)point {
    gCursor = point;
}
// an arrow per character width or line the finger travels
- (void)updateFloatingCursorAtPoint:(CGPoint)point {
    const float w = ImGui::GetFontSize() * 0.6f, h = ImGui::GetTextLineHeightWithSpacing();
    for (; point.x - gCursor.x >= w; gCursor.x += w)
        tapKey(ImGuiKey_RightArrow);
    for (; gCursor.x - point.x >= w; gCursor.x -= w)
        tapKey(ImGuiKey_LeftArrow);
    for (; point.y - gCursor.y >= h; gCursor.y += h)
        tapKey(ImGuiKey_DownArrow);
    for (; gCursor.y - point.y >= h; gCursor.y -= h)
        tapKey(ImGuiKey_UpArrow);
}
- (void)endFloatingCursor {
}
- (BOOL)canBecomeFirstResponder {
    return YES; // always: hardware keys and shortcuts arrive even with no text field active
}
- (UIView*)inputView {
    if (!gNoKeyboard)
        gNoKeyboard = [[UIView alloc] initWithFrame:CGRectZero];
    return self.keyboardWanted ? nil : gNoKeyboard;
}
- (BOOL)hasText {
    return YES;
}
- (void)insertText:(NSString*)text {
    if ([text isEqualToString:@"\n"])
        tapKey(ImGuiKey_Enter);
    else
        ImGui::GetIO().AddInputCharactersUTF8(text.UTF8String);
}
- (void)deleteBackward {
    tapKey(ImGuiKey_Backspace);
}
// sql, not prose
- (UITextAutocorrectionType)autocorrectionType {
    return UITextAutocorrectionTypeNo;
}
- (UITextAutocapitalizationType)autocapitalizationType {
    return UITextAutocapitalizationTypeNone;
}
- (UITextSpellCheckingType)spellCheckingType {
    return UITextSpellCheckingTypeNo;
}
- (UITextSmartQuotesType)smartQuotesType {
    return UITextSmartQuotesTypeNo;
}
- (UITextSmartDashesType)smartDashesType {
    return UITextSmartDashesTypeNo;
}
- (UIKeyboardType)keyboardType {
    return UIKeyboardTypeASCIICapable;
}

- (void)sendPresses:(NSSet<UIPress*>*)presses down:(BOOL)down event:(UIPressesEvent*)event {
    ImGuiIO& io = ImGui::GetIO();
    NSMutableSet<UIPress*>* text = [NSMutableSet set];
    for (UIPress* press in presses) {
        if (!press.key) {
            [text addObject:press];
            continue;
        }
        const UIKeyModifierFlags mods = press.key.modifierFlags;
        const ImGuiKey key = keyFor(press.key.keyCode);
        switch (key) {
        case ImGuiKey_LeftCtrl:
        case ImGuiKey_RightCtrl:
            io.AddKeyEvent(ImGuiMod_Ctrl, down);
            break;
        case ImGuiKey_LeftShift:
        case ImGuiKey_RightShift:
            io.AddKeyEvent(ImGuiMod_Shift, down);
            break;
        case ImGuiKey_LeftAlt:
        case ImGuiKey_RightAlt:
            io.AddKeyEvent(ImGuiMod_Alt, down);
            break;
        case ImGuiKey_LeftSuper:
        case ImGuiKey_RightSuper:
            io.AddKeyEvent(ImGuiMod_Super, down);
            break;
        default:
            break;
        }
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, down);
        // ⌘C and friends are shortcuts, not text
        if (typesText(key) && !(mods & (UIKeyModifierCommand | UIKeyModifierControl)))
            [text addObject:press];
    }
    if (text.count == 0)
        return;
    if (down)
        [super pressesBegan:text withEvent:event];
    else
        [super pressesEnded:text withEvent:event];
}
- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [self sendPresses:presses down:YES event:event];
}
- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [self sendPresses:presses down:NO event:event];
}
- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [self sendPresses:presses down:NO event:event];
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!self.isFirstResponder)
        [self becomeFirstResponder]; // back from a native text field
    UITouch* touch = touches.anyObject;
    const CGPoint p = [touch locationInView:self];
    ImGuiIO& io = ImGui::GetIO();
    if (touch.type == UITouchTypeIndirectPointer) { // trackpad or mouse: a mouse
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent((event.buttonMask & UIEventButtonMaskSecondary) ? 1 : 0, true);
        return;
    }
    mydear::touch::fling(ImVec2(0, 0)); // a touch stops a flick
    if (mydear::touch::state() != mydear::touch::State::None)
        return; // one finger drives the mouse
    // hover only: the cursor shape tells the pan what lies under the finger
    mydear::touch::down(vec(p), CACurrentMediaTime());
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    UITouch* touch = touches.anyObject;
    const CGPoint p = [touch locationInView:self];
    if (touch.type == UITouchTypeIndirectPointer) {
        ImGui::GetIO().AddMousePosEvent(p.x, p.y);
        return;
    }
    mydear::touch::follow(vec(p));
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    UITouch* touch = touches.anyObject;
    ImGuiIO& io = ImGui::GetIO();
    if (touch.type == UITouchTypeIndirectPointer) {
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        return;
    }
    // a tap clicks, a press lets go; the cursor stays (the ipad pointer hovers anyway)
    mydear::touch::up(CACurrentMediaTime(), false);
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (touches.anyObject.type == UITouchTypeIndirectPointer) {
        mydear::touch::releaseAway();
        return;
    }
    mydear::touch::cancel();
}
@end


// ---- the controller: owns the view and draws each frame ----

@implementation MydearViewController {
    UIPanGestureRecognizer* pan_;
    UILongPressGestureRecognizer* longPress_;
    UIEditMenuInteraction* editMenu_;
}

- (void)loadView {
    MydearView* view = [[MydearView alloc] initWithFrame:UIScreen.mainScreen.bounds
                                                  device:MTLCreateSystemDefaultDevice()];
    view.delegate = self;
    view.preferredFramesPerSecond = 120;
    view.multipleTouchEnabled = YES;
    // pointer hover (trackpad, mouse) moves the imgui cursor without a click
    [view addGestureRecognizer:[[UIHoverGestureRecognizer alloc] initWithTarget:self
                                                                         action:@selector(hover:)]];
    // a finger drag (or two, or a trackpad swipe) scrolls
    pan_ = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(pan:)];
    pan_.allowedScrollTypesMask = UIScrollTypeMaskAll;
    pan_.delegate = self;
    [view addGestureRecognizer:pan_];
    // long press = right click (context menus)
    longPress_ = [[UILongPressGestureRecognizer alloc] initWithTarget:self
                                                               action:@selector(longPress:)];
    longPress_.delegate = self;
    [view addGestureRecognizer:longPress_];
    editMenu_ = [[UIEditMenuInteraction alloc] initWithDelegate:self];
    [view addInteraction:editMenu_];
    // the imgui view keeps clear of the notch in landscape
    UIView* container = [[UIView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [container addSubview:view];
    [NSLayoutConstraint activateConstraints:@[
        [view.topAnchor constraintEqualToAnchor:container.topAnchor],
        [view.bottomAnchor constraintEqualToAnchor:container.bottomAnchor],
        [view.leadingAnchor constraintEqualToAnchor:container.safeAreaLayoutGuide.leadingAnchor],
        [view.trailingAnchor constraintEqualToAnchor:container.safeAreaLayoutGuide.trailingAnchor],
    ]];
    self.view = container;
    gView = view;
    gQueue = [view.device newCommandQueue];
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.edgesForExtendedLayout = UIRectEdgeNone; // imgui y = 0 is below the bar, for dialogs too
    self.navigationController.navigationBarHidden = YES;
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(keyboardFrame:)
                                               name:UIKeyboardWillChangeFrameNotification
                                             object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(keyboardFrame:)
                                               name:UIKeyboardWillHideNotification
                                             object:nil];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [gView becomeFirstResponder];
}
// the ui lays out above the keyboard (a floating or undocked one covers nothing)
- (void)keyboardFrame:(NSNotification*)note {
    if ([note.name isEqualToString:UIKeyboardWillHideNotification]) {
        gKeyboardInset = 0;
        return;
    }
    const CGRect screen = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    const CGRect local = [gView convertRect:screen
                        fromCoordinateSpace:gView.window.screen.coordinateSpace];
    const CGRect covered = CGRectIntersection(gView.bounds, local);
    gKeyboardInset = CGRectIsNull(covered) || CGRectGetMaxY(covered) < CGRectGetMaxY(gView.bounds)
                         ? 0
                         : covered.size.height;
}
// gestures are for fingers; a trackpad click or drag is a mouse
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)g shouldReceiveTouch:(UITouch*)touch {
    return touch.type == UITouchTypeDirect;
}
- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer*)g {
    if (g != pan_ || g.numberOfTouches != 1)
        return YES; // long press, two fingers, trackpad scroll
    // one finger on a splitter, column edge or text: drag it instead of scrolling
    if (touch::scrolls())
        return YES;
    touch::press(touch::start(), vec([g locationInView:gView]));
    return NO;
}
- (void)hover:(UIHoverGestureRecognizer*)g {
    const CGPoint p = [g locationInView:gView];
    ImGui::GetIO().AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
}
- (void)pan:(UIPanGestureRecognizer*)g {
    const CGPoint d = [g translationInView:gView];
    [g setTranslation:CGPointZero inView:gView];
    if (g.numberOfTouches > 0)
        touch::hover(vec([g locationInView:gView])); // scroll the window under the finger
    touch::scrollBy(vec(d));
    static bool finger = false; // wheels and trackpads have no touches: no coasting
    if (g.state == UIGestureRecognizerStateBegan)
        finger = g.numberOfTouches > 0;
    touch::fling(g.state == UIGestureRecognizerStateEnded && finger
                     ? vec([g velocityInView:gView])
                     : ImVec2(0, 0));
}
- (void)longPress:(UILongPressGestureRecognizer*)g {
    if (g.state != UIGestureRecognizerStateBegan)
        return;
    [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
    touch::cancel(); // a press in progress lets go away from its target
    const CGPoint p = [g locationInView:gView];
    touch::hover(vec(p));
    // a few frames for imgui to report what is under the finger: text gets the edit menu,
    // anything else a right click
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
          ImGuiIO& io = ImGui::GetIO();
          if (ImGui::GetMouseCursor() != ImGuiMouseCursor_TextInput) {
              touch::rightClick();
              return;
          }
          if (!io.WantTextInput) { // focus the field first
              io.AddMouseButtonEvent(0, true);
              io.AddMouseButtonEvent(0, false);
          }
          [editMenu_ presentEditMenuWithConfiguration:[UIEditMenuConfiguration
                                                          configurationWithIdentifier:nil
                                                                          sourcePoint:p]];
        });
}
- (UIMenu*)editMenuInteraction:(UIEditMenuInteraction*)interaction
          menuForConfiguration:(UIEditMenuConfiguration*)configuration
              suggestedActions:(NSArray<UIMenuElement*>*)suggested {
    auto item = [](NSString* title, ImGuiKey key) {
        return [UIAction actionWithTitle:title
                                   image:nil
                              identifier:nil
                                 handler:^(UIAction*) {
                                   ImGuiIO& io = ImGui::GetIO();
                                   io.AddKeyEvent(ImGuiMod_Super, true);
                                   tapKey(key);
                                   io.AddKeyEvent(ImGuiMod_Super, false);
                                 }];
    };
    NSMutableArray* items =
        [NSMutableArray arrayWithArray:@[ item(@"Cut", ImGuiKey_X), item(@"Copy", ImGuiKey_C) ]];
    if (UIPasteboard.generalPasteboard.hasStrings)
        [items addObject:item(@"Paste", ImGuiKey_V)];
    [items addObject:item(@"Select All", ImGuiKey_A)];
    return [UIMenu menuWithChildren:items];
}
- (void)drawInMTKView:(MTKView*)view {
    if (!gApp)
        return;
    gApp->renderFrame();
    // show the soft keyboard while a text field wants it
    MydearView* v = (MydearView*)view;
    const BOOL want =
        ImGui::GetIO().WantTextInput || ImGui::GetCurrentContext()->PlatformImeData.WantVisible;
    if (want != v.keyboardWanted) {
        v.keyboardWanted = want;
        [v reloadInputViews];
    }
}
- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
}
- (BOOL)prefersStatusBarHidden {
    return NO;
}
@end

// ---- scene and app delegates ----

namespace {
    Class gRootClass = nil;
}

@interface MydearSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(strong, nonatomic) UIWindow* window;
@end

@implementation MydearSceneDelegate
- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)options {
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    self.window.rootViewController =
        [[UINavigationController alloc] initWithRootViewController:[[gRootClass alloc] init]];
    [self.window makeKeyAndVisible];
}
@end

@interface MydearAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation MydearAppDelegate
- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options {
    UISceneConfiguration* config = [[UISceneConfiguration alloc] initWithName:@"Default"
                                                                  sessionRole:session.role];
    config.delegateClass = MydearSceneDelegate.class;
    return config;
}
@end

namespace mydear {
    int RunUIKit(int argc, char* argv[], Class root) {
        gRootClass = root ?: MydearViewController.class;
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(MydearAppDelegate.class));
    }

    struct App::Impl {
        Delegate* delegate = nullptr;
    };

    App::App() : impl_(std::make_unique<Impl>()) {}

    App::~App() {
        destroy();
    }

    // the view exists (root controller's loadView): imgui, its metal backend, the clipboard
    bool App::create(const Config& config, Delegate& delegate) {
        if (!gView) {
            std::fprintf(stderr, "mydear: create the app from the root controller's loadView\n");
            return false;
        }
        impl_->delegate = &delegate;
        gApp = this;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        if (config.docking)
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendPlatformName = "mydear_ios";
        io.MouseDoubleClickMaxDist = 20.0f; // fingers land less precisely than a mouse
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetWindowWorkAreaInsets = [](ImGuiViewport*) { return detail::safeArea(); };
        pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            static std::string text;
            text = UIPasteboard.generalPasteboard.string.UTF8String ?: "";
            return text.c_str();
        };
        pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            UIPasteboard.generalPasteboard.string = [NSString stringWithUTF8String:text];
        };
        ImGui::StyleColorsDark();
        Theme::ApplyTheme(Theme::Current()->colors);
        delegate.setup();
        return ImGui_ImplMetal_Init(gView.device);
    }

    void App::run() {} // uikit runs the loop

    bool App::pollEvents() {
        return true;
    }

    void App::renderFrame() {
        @autoreleasepool {
            ImGuiIO& io = ImGui::GetIO();
            const CGSize size = gView.bounds.size;
            const CGFloat scale = gView.contentScaleFactor;
            io.DisplaySize =
                ImVec2(size.width, std::max<CGFloat>(size.height - std::max(gKeyboardInset,
                                                                             gView.safeAreaInsets.bottom),
                                                     1));
            io.DisplayFramebufferScale = ImVec2(scale, scale);
            // shortcut: top only (status and navigation bar); the display already stops above
            // the home indicator and keyboard. add the sides when landscape iphones need them
            detail::safeArea() = ImVec4(0, static_cast<float>(gView.safeAreaInsets.top), 0, 0);
            const CFTimeInterval now = CACurrentMediaTime();
            io.DeltaTime = gLastTime > 0 ? static_cast<float>(now - gLastTime) : 1.0f / 60.0f;
            gLastTime = now;
            touch::coast(io.DeltaTime); // the flick coasts on

            MTLRenderPassDescriptor* pass = gView.currentRenderPassDescriptor;
            if (!pass)
                return;
            const ImVec4 base = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            pass.colorAttachments[0].clearColor = MTLClearColorMake(base.x, base.y, base.z, 1);
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;

            id<MTLCommandBuffer> commands = [gQueue commandBuffer];
            id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
            // ponytail: sideways only, vertical padding would mis-pick adjacent rows
            ImGui::GetStyle().TouchExtraPadding = ImVec2(Theme::Spacing::S, 0.0f);
            ImGui_ImplMetal_NewFrame(pass);
            ImGui::NewFrame();
            impl_->delegate->frame();
            ImGui::Render();
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commands, encoder);
            [encoder endEncoding];
            [commands presentDrawable:gView.currentDrawable];
            [commands commit];
            detail::placeNativeViews();
        }
    }

    void App::destroy() {
        if (gApp != this)
            return;
        detail::dropNativeViews();
        ImGui_ImplMetal_Shutdown();
        ImGui::DestroyContext();
        gApp = nullptr;
    }

    // ponytail: the display link paces ios; vsync and the cap are desktop settings
    void App::setVSync(bool) {}
    void App::setMaxFps(int) {}

    ImTextureID App::createTexture(const uint8_t* rgba, const int width, const int height) {
        if (!gView || !rgba)
            return ImTextureID{};
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [gView.device newTextureWithDescriptor:desc];
        [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                   mipmapLevel:0
                     withBytes:rgba
                   bytesPerRow:width * 4];
        // imgui holds the id, not a reference: keep the texture alive with the app
        return (ImTextureID)(intptr_t)(__bridge_retained void*)texture;
    }

    GLFWwindow* App::glfwWindow() const {
        return nullptr;
    }

    void* App::nativeWindow() const {
        return gView ? (__bridge void*)gView.window : nullptr;
    }

    void* detail::nativeHost() {
        return (__bridge void*)gView;
    }

    void* App::nativeView() const {
        return (__bridge void*)gView;
    }
} // namespace mydear
