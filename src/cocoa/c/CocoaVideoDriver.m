// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2012 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#include "cocoa/c/CocoaVideoDriver.h"

#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>
#include <mach/mach.h>
#include <mach/clock.h>

static bool mouse_visible = true;

@implementation NSDate(AntaresAdditions)
- (NSTimeInterval)timeIntervalSinceSystemStart {
    clock_serv_t system_clock;
    kern_return_t status = host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &system_clock);
    mach_timespec_t now;
    clock_get_time(system_clock, &now);
    NSTimeInterval now_secs = now.tv_sec + (now.tv_nsec / 1e9);
    return now_secs + [self timeIntervalSinceNow];
}
@end

bool antares_is_active() {
    return [NSApp isActive];
}

void antares_menu_bar_hide() {
    [NSMenu setMenuBarVisible:NO];
}

void antares_menu_bar_show() {
    [NSMenu setMenuBarVisible:YES];
}

void antares_mouse_hide() {
    if (mouse_visible) {
        [NSCursor hide];
        mouse_visible = false;
    }
}

void antares_mouse_show() {
    if (!mouse_visible) {
        [NSCursor unhide];
        mouse_visible = true;
    }
}

int64_t antares_double_click_interval_usecs() {
    return GetDblTime() * 1000000 / 60;
    // 10.6+: return [NSEvent doubleClickInterval] * 1e6;
}

struct AntaresWindow {
    int32_t screen_width;
    int32_t screen_height;
    NSOpenGLPixelFormat* pixel_format;
    NSOpenGLContext* context;
    NSOpenGLView* view;
    NSWindow* window;
};

AntaresWindow* antares_window_create(
        CGLPixelFormatObj pixel_format, CGLContextObj context,
        int32_t screen_width, int32_t screen_height,
        bool fullscreen, bool retina) {
    AntaresWindow* window = malloc(sizeof(AntaresWindow));
    window->screen_width = screen_width;
    window->screen_height = screen_height;
    window->pixel_format = [[NSOpenGLPixelFormat alloc] initWithCGLPixelFormatObj:pixel_format];
    window->context = [[NSOpenGLContext alloc] initWithCGLContextObj:context];

    NSRect screen_rect = [[NSScreen mainScreen] frame];
    NSRect display_rect = NSMakeRect(0, 0, screen_width, screen_height);
    NSRect window_rect;
    int style_mask;
    if (fullscreen) {
        window_rect = screen_rect;
        style_mask = NSBorderlessWindowMask;
        GLint gl_size[2] = {screen_width, screen_height};
        CGLSetParameter(context, kCGLCPSurfaceBackingSize, gl_size);
        CGLEnable(context, kCGLCESurfaceBackingSize);
    } else {
        window_rect = display_rect;
        style_mask = NSTitledWindowMask | NSMiniaturizableWindowMask;
    }

    window->view = [[NSOpenGLView alloc] initWithFrame:window_rect
        pixelFormat:window->pixel_format];
    if (retina) {
        [window->view setWantsBestResolutionOpenGLSurface:YES];
    }
    [window->view setOpenGLContext:window->context];

    window->window = [[NSWindow alloc] initWithContentRect:window_rect
        styleMask:style_mask
        backing:NSBackingStoreBuffered
        defer:NO];
    [window->window setAcceptsMouseMovedEvents:YES];
    [window->window setContentView:window->view];
    [window->window makeKeyAndOrderFront:NSApp];
    if (fullscreen) {
        [window->window setLevel:NSMainMenuWindowLevel+1];
    } else {
        [window->window center];
    }
    return window;
}

void antares_window_destroy(AntaresWindow* window) {
    [window->window release];
    [window->view release];
    [window->context release];
    [window->pixel_format release];
    free(window);
}

int32_t antares_window_viewport_width(const AntaresWindow* window) {
    return [window->view convertRectToBacking:[window->view bounds]].size.width;
}

int32_t antares_window_viewport_height(const AntaresWindow* window) {
    // [self convertRectToBacking:[self bounds]];
    return [window->view convertRectToBacking:[window->view bounds]].size.height;
}

struct AntaresEventTranslator {
    void (*mouse_down_callback)(int button, int32_t x, int32_t y, void* userdata);
    void* mouse_down_userdata;

    void (*mouse_up_callback)(int button, int32_t x, int32_t y, void* userdata);
    void* mouse_up_userdata;

    void (*mouse_move_callback)(int32_t x, int32_t y, void* userdata);
    void* mouse_move_userdata;

    void (*caps_lock_callback)(void* userdata);
    void* caps_lock_userdata;

    void (*caps_unlock_callback)(void* userdata);
    void* caps_unlock_userdata;

    int32_t screen_width;
    int32_t screen_height;
    AntaresWindow* window;

    int32_t last_flags;
};

static NSPoint translate_coords(
        AntaresEventTranslator* translator, NSWindow* from_window, NSPoint input) {
    NSWindow* to_window = (translator->window != nil) ? translator->window->window : nil;
    if (from_window != to_window) {
        if (from_window != nil) {
            input = [from_window convertBaseToScreen:input];
        }
        if (to_window != nil) {
            input = [to_window convertScreenToBase:input];
        }
    }
    input = [translator->window->view convertPoint:input fromView:nil];
    NSSize view_size = [translator->window->view bounds].size;
    input.x = round(input.x / view_size.width * translator->screen_width);
    input.y = round(input.y / view_size.height * translator->screen_height);
    return NSMakePoint(input.x, translator->screen_height - input.y);
}

AntaresEventTranslator* antares_event_translator_create(
        int32_t screen_width, int32_t screen_height) {
    AntaresEventTranslator* translator = malloc(sizeof(AntaresEventTranslator));
    memset(translator, 0, sizeof(AntaresEventTranslator));
    translator->screen_width = screen_width;
    translator->screen_height = screen_height;
    translator->window = nil;
    translator->last_flags = 0;
    return translator;
}

void antares_event_translator_destroy(AntaresEventTranslator* translator) {
    free(translator);
}

void antares_event_translator_set_window(
        AntaresEventTranslator* translator, AntaresWindow* window) {
    translator->window = window;
}

void antares_get_mouse_location(AntaresEventTranslator* translator, int32_t* x, int32_t* y) {
    NSPoint location = translate_coords(translator, nil, [NSEvent mouseLocation]);
    *x = location.x;
    *y = location.y;
}

void antares_get_mouse_button(AntaresEventTranslator* translator, int32_t* button, int which) {
    switch (which) {
      case 0:
        *button = CGEventSourceButtonState(
                kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
        break;

      case 1:
        *button = CGEventSourceButtonState(
                kCGEventSourceStateCombinedSessionState, kCGMouseButtonRight);
        break;

      case 2:
        *button = CGEventSourceButtonState(
                kCGEventSourceStateCombinedSessionState, kCGMouseButtonCenter);
        break;
    }
}

void antares_event_translator_set_mouse_down_callback(
        AntaresEventTranslator* translator,
        void (*callback)(int button, int32_t x, int32_t y, void* userdata), void* userdata) {
    translator->mouse_down_callback = callback;
    translator->mouse_down_userdata = userdata;
}

void antares_event_translator_set_mouse_up_callback(
        AntaresEventTranslator* translator,
        void (*callback)(int button, int32_t x, int32_t y, void* userdata), void* userdata) {
    translator->mouse_up_callback = callback;
    translator->mouse_up_userdata = userdata;
}

void antares_event_translator_set_mouse_move_callback(
        AntaresEventTranslator* translator,
        void (*callback)(int32_t x, int32_t y, void* userdata), void* userdata) {
    translator->mouse_move_callback = callback;
    translator->mouse_move_userdata = userdata;
}

void antares_event_translator_set_caps_lock_callback(
        AntaresEventTranslator* translator,
        void (*callback)(void* userdata), void* userdata) {
    translator->caps_lock_callback = callback;
    translator->caps_lock_userdata = userdata;
}

void antares_event_translator_set_caps_unlock_callback(
        AntaresEventTranslator* translator,
        void (*callback)(void* userdata), void* userdata) {
    translator->caps_unlock_callback = callback;
    translator->caps_unlock_userdata = userdata;
}

static void hide_unhide(AntaresWindow* window, NSPoint location) {
    if (!window) {
        return;
    }
    bool in_window =
        location.x >= 0 && location.y >= 0 &&
        location.x < window->screen_width && location.y < window->screen_height;
    if (in_window) {
        antares_mouse_hide();
    } else {
        antares_mouse_show();
    }
}

static int button_for(NSEvent* event) {
    switch ([event type]) {
      case NSLeftMouseDown:
      case NSLeftMouseUp:
        return 0;

      case NSRightMouseDown:
      case NSRightMouseUp:
        return 1;

      case NSOtherMouseDown:
      case NSOtherMouseUp:
        return 2;

      default:
        return -1;
    }
}

static void mouse_down(AntaresEventTranslator* translator, NSEvent* event) {
    NSPoint where = translate_coords(translator, [event window], [event locationInWindow]);
    hide_unhide(translator->window, where);
    int button = button_for(event);
    translator->mouse_down_callback(button, where.x, where.y, translator->mouse_down_userdata);
}

static void mouse_up(AntaresEventTranslator* translator, NSEvent* event) {
    NSPoint where = translate_coords(translator, [event window], [event locationInWindow]);
    hide_unhide(translator->window, where);
    int button = button_for(event);
    translator->mouse_up_callback(button, where.x, where.y, translator->mouse_up_userdata);
}

static void mouse_move(AntaresEventTranslator* translator, NSEvent* event) {
    NSPoint where = translate_coords(translator, [event window], [event locationInWindow]);
    hide_unhide(translator->window, where);
    translator->mouse_move_callback(where.x, where.y, translator->mouse_move_userdata);
}

bool antares_event_translator_next(AntaresEventTranslator* translator, int64_t until) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSDate* date = [NSDate dateWithTimeIntervalSince1970:(until * 1e-6)];
    bool waiting = true;
    while (waiting) {
        NSEvent* event =
            [NSApp nextEventMatchingMask:NSAnyEventMask untilDate:date inMode:NSDefaultRunLoopMode
             dequeue:YES];
        if (!event) {
            break;
        }
        // Put events after `until` back in the queue.
        if ([event timestamp] > [date timeIntervalSinceSystemStart]) {
            [NSApp postEvent:event atStart:true];
            break;
        }

        // Send non-key events.
        switch ([event type]) {
          case NSKeyDown:
          case NSKeyUp:
            break;

          default:
            [NSApp sendEvent:event];
            break;
        };

        // Handle events.
        switch ([event type]) {
          case NSLeftMouseDown:
          case NSRightMouseDown:
            mouse_down(translator, event);
            waiting = false;
            break;

          case NSLeftMouseUp:
          case NSRightMouseUp:
            mouse_up(translator, event);
            waiting = false;
            break;

          case NSMouseMoved:
          case NSLeftMouseDragged:
          case NSRightMouseDragged:
            mouse_move(translator, event);
            waiting = false;
            break;

          case NSApplicationDefined:
            waiting = false;
            break;

          case NSFlagsChanged:
            if ([event modifierFlags] & NSAlphaShiftKeyMask) {
                translator->caps_lock_callback(translator->caps_lock_userdata);
            } else {
                translator->caps_unlock_callback(translator->caps_unlock_userdata);
            }
            break;

          default:
            break;
        }
    }
    [pool drain];
    return !waiting;
}

void antares_event_translator_cancel(AntaresEventTranslator* translator) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSEvent* event = [NSEvent
        otherEventWithType:NSApplicationDefined location:NSMakePoint(0, 0) modifierFlags:0
        timestamp:[[NSDate date] timeIntervalSinceSystemStart] windowNumber:0 context:nil subtype:0
        data1:0 data2:0];
    [NSApp postEvent:event atStart:true];
    [pool drain];
}
