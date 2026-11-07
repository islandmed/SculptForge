// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SculptForge > Preferences... in the macOS app menu (#111; MacAppMenu.h).
#import <Cocoa/Cocoa.h>

#include "app/MacAppMenu.h"

namespace sf {
void wakeMainLoop(); // App.h (not included: it pulls in the whole app)
}

@interface SFPreferencesTarget : NSObject
- (void)openPreferences:(id)sender;
@end

@implementation SFPreferencesTarget
- (void)openPreferences:(id)sender {
    NSEvent* e = [NSApp currentEvent];
    if (e && sf::macmenu::isOwnKeyEquivalent(e.type == NSEventTypeKeyDown,
                                             (e.modifierFlags & NSEventModifierFlagCommand) != 0,
                                             e.charactersIgnoringModifiers.UTF8String))
        return; // Cmd+,: SDL already gave the key to the app, whose shortcut opens Preferences
    sf::macmenu::requestPreferences();
    sf::wakeMainLoop();
}
@end

namespace sf::macmenu {

void install() {
    static SFPreferencesTarget* target = [[SFPreferencesTarget alloc] init]; // (NSMenuItem keeps its target weakly)
    NSMenu* bar = [NSApp mainMenu];
    if (bar.numberOfItems == 0) return; // (no menu bar: nothing to fill)
    NSMenu* appMenu = [[bar itemAtIndex:0] submenu];
    for (NSMenuItem* item in appMenu.itemArray)
        if ([item.keyEquivalent isEqualToString:@","] && item.action == nil) { // SDL's "Preferences..." (Cmd+,)
            item.target = target;
            item.action = @selector(openPreferences:);
            return;
        }
}

} // namespace sf::macmenu
