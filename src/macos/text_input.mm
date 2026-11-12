// the ime client for glfw's view. glfw 3.4 hands every key to the app before the ime sees it,
// drops the composing (marked) text and tells the ime there is no caret, so input methods that
// compose (vietnamese, chinese, japanese, korean) mistype. this replaces its NSTextInputClient
// methods: the composition is typed into the field and edited in place (ime.cpp), keys the ime
// takes while composing stay away from the app, and the ime reads the field around the caret
#include "../ime.hpp"
#include "imgui_internal.h"

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace ime = imshell::detail::ime;

namespace {
    NSView* gView = nil;

    std::string utf8(id string) {
        NSString* s = [string isKindOfClass:[NSAttributedString class]] ? [string string] : string;
        return s.UTF8String ? s.UTF8String : "";
    }

    // the field as the ime sees it, in utf-16: the active InputText's text, else (an editor
    // imshell cannot read) a document holding only the composition
    struct Doc {
        std::string_view text;
        int caret = 0, selStart = 0, selEnd = 0; // bytes
        bool real = false;
    };

    Doc doc() {
        Doc d;
        if (ime::field(d.text, d.caret, d.selStart, d.selEnd)) {
            d.real = true;
            return d;
        }
        d.text = ime::text();
        d.caret = d.selStart = d.selEnd = static_cast<int>(d.text.size());
        return d;
    }

    NSRange selected() {
        const Doc d = doc();
        const int start = ime::utf16Length(d.text.substr(0, d.selStart));
        return NSMakeRange(start, ime::utf16Length(d.text.substr(d.selStart, d.selEnd - d.selStart)));
    }

    NSRange marked() {
        if (!ime::composing())
            return NSMakeRange(NSNotFound, 0);
        const Doc d = doc();
        const int caret = ime::utf16Length(d.text.substr(0, d.caret));
        const int length = ime::utf16Length(ime::text());
        return NSMakeRange(std::max(0, caret - length), length);
    }

    // an ime replacing text it did not compose (it read it with attributedSubstring): when the
    // range ends at the caret, backspace over it
    void replaceBeforeCaret(NSRange range) {
        if (range.location == NSNotFound || ime::composing())
            return;
        const Doc d = doc();
        if (!d.real)
            return;
        const int start = ime::utf16ToByte(d.text, static_cast<int>(range.location));
        const int end = ime::utf16ToByte(d.text, static_cast<int>(range.location + range.length));
        if (end == d.caret && start < end)
            ime::backspace(ime::codepoints(d.text.substr(start, end - start)));
    }

    void addMethod(Class c, SEL sel, id block) {
        Method m = class_getInstanceMethod(c, sel);
        class_replaceMethod(c, sel, imp_implementationWithBlock(block), method_getTypeEncoding(m));
    }
} // namespace

namespace imshell::detail {
    void installTextInput(NSView* view) {
        gView = view;
        Class c = object_getClass(view);
        using KeyDown = void (*)(id, SEL, NSEvent*);
        static KeyDown glfwKeyDown =
            reinterpret_cast<KeyDown>(method_getImplementation(class_getInstanceMethod(c, @selector(keyDown:))));
        // while composing, the ime owns backspace, enter, arrows and escape: the app must not
        // also see them
        addMethod(c, @selector(keyDown:), ^(NSView* self, NSEvent* event) {
          if (ime::composing())
              [self interpretKeyEvents:@[ event ]];
          else
              glfwKeyDown(self, @selector(keyDown:), event);
        });
        addMethod(c, @selector(setMarkedText:selectedRange:replacementRange:),
                  ^(NSView*, id string, NSRange, NSRange replacement) {
                    if (ime::trace())
                        std::fprintf(stderr, "ime: setMarkedText \"%s\" replacing {%ld,%ld}\n",
                                     utf8(string).c_str(), (long)replacement.location, (long)replacement.length);
                    replaceBeforeCaret(replacement);
                    ime::compose(utf8(string));
                  });
        addMethod(c, @selector(unmarkText), ^(NSView*) {
          if (ime::trace())
              std::fprintf(stderr, "ime: unmarkText\n");
          ime::commit(std::string(ime::text()));
        });
        addMethod(c, @selector(insertText:replacementRange:), ^(NSView*, id string, NSRange replacement) {
          std::string text = utf8(string);
          if (ime::trace())
              std::fprintf(stderr, "ime: insertText \"%s\" replacing {%ld,%ld}\n", text.c_str(),
                           (long)replacement.location, (long)replacement.length);
          if (NSApp.currentEvent.modifierFlags & NSEventModifierFlagCommand)
              return; // a shortcut, not text
          // function keys come as private-use characters (U+F700..U+F7FF, 3 bytes: EF 9C..9F)
          std::string clean;
          for (size_t i = 0; i < text.size();) {
              const auto b = static_cast<unsigned char>(text[i]);
              const size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
              const bool function = n == 3 && b == 0xEF && i + 1 < text.size() &&
                                    (static_cast<unsigned char>(text[i + 1]) & 0xFC) == 0x9C;
              if (!function)
                  clean.append(text, i, n);
              i += n;
          }
          replaceBeforeCaret(replacement);
          ime::commit(clean);
        });
        addMethod(c, @selector(hasMarkedText), ^BOOL(NSView*) {
          return ime::composing();
        });
        addMethod(c, @selector(markedRange), ^NSRange(NSView*) {
          return marked();
        });
        addMethod(c, @selector(selectedRange), ^NSRange(NSView*) {
          return selected();
        });
        addMethod(c, @selector(attributedSubstringForProposedRange:actualRange:),
                  ^NSAttributedString*(NSView*, NSRange range, NSRangePointer actual) {
                    const Doc d = doc();
                    NSString* all = [[NSString alloc] initWithBytes:d.text.data()
                                                             length:d.text.size()
                                                           encoding:NSUTF8StringEncoding];
                    if (!all || range.location == NSNotFound || range.location > all.length)
                        return nil;
                    range.length = std::min(range.length, all.length - range.location);
                    if (actual)
                        *actual = range;
                    return [[NSAttributedString alloc] initWithString:[all substringWithRange:range]];
                  });
        // the candidate window opens under the caret
        addMethod(c, @selector(firstRectForCharacterRange:actualRange:),
                  ^NSRect(NSView* self, NSRange range, NSRangePointer actual) {
                    if (actual)
                        *actual = range;
                    const ImGuiPlatformImeData& data = GImGui->PlatformImeData;
                    const float h = data.InputLineHeight > 0 ? data.InputLineHeight : ImGui::GetFontSize();
                    const NSRect local =
                        NSMakeRect(data.InputPos.x, self.bounds.size.height - (data.InputPos.y + h), 1, h);
                    return [self.window convertRectToScreen:[self convertRect:local toView:nil]];
                  });
        addMethod(c, @selector(characterIndexForPoint:), ^NSUInteger(NSView*, NSPoint) {
          return NSNotFound;
        });
    }

    // after the app's frame: a composition whose field went away ends where it is
    void textInputFrame() {
        if (ime::composing() && !GImGui->PlatformImeData.WantTextInput) {
            [gView.inputContext discardMarkedText];
            ime::forget();
        }
        drawComposingUnderline(ime::text().c_str());
    }
} // namespace imshell::detail
