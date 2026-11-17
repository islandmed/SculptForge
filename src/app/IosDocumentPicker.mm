// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The iPad's Open dialog (NativeDialog.cpp; SDL has none on iPadOS): the system's document picker, which hands back copies
// of the chosen files. Each copy is moved into the app's own storage (Library/Application Support/SculptForge/Opened), so a
// recent file still opens after iPadOS clears its temporary folder. Saving needs no picker: Documents (dialogs::iosDocumentsDir),
// which the Files app shows as "On My iPad > SculptForge".
#include "app/NativeDialog.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <string>
#include <vector>

@interface SFDocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) SDL_DialogFileCallback callback;
@property(nonatomic) void* userdata;
@end

static SFDocumentPickerDelegate* g_pending = nil; // (the picker holds its delegate weakly: kept here until it answers)

// The callback gets the kept copies, or an empty list for a cancel (as SDL's dialogs do).
static void answer(SFDocumentPickerDelegate* d, const std::vector<std::string>& paths) {
    std::vector<const char*> list;
    for (const std::string& p : paths) list.push_back(p.c_str());
    list.push_back(nullptr);
    d.callback(d.userdata, list.data(), -1);
    g_pending = nil;
}

@implementation SFDocumentPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSURL* support = [[fm URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask] firstObject];
    NSURL* dir = [support URLByAppendingPathComponent:@"SculptForge/Opened" isDirectory:YES];
    [fm createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];
    std::vector<std::string> paths;
    for (NSURL* url in urls) {
        if (!url.fileURL) continue;
        NSURL* to = [dir URLByAppendingPathComponent:url.lastPathComponent];
        [fm removeItemAtURL:to error:nil];
        NSError* error = nil;
        if ([fm moveItemAtURL:url toURL:to error:&error] || [fm copyItemAtURL:url toURL:to error:&error])
            paths.push_back(to.path.UTF8String);
        else SDL_Log("Couldn't keep %s: %s", url.path.UTF8String, error.localizedDescription.UTF8String);
    }
    answer(self, paths);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    answer(self, {});
}
@end

namespace sf::dialogs {

std::string iosDocumentsDir() {
    NSURL* docs = [[NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
    return docs ? std::string(docs.path.UTF8String) : std::string();
}

void iosOpenFile(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const SDL_DialogFileFilter* filters,
                 int nfilters, bool allowMany) {
    // The filters' extensions as content types ("sfproj;obj" -> two); "*" or none: any file.
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (int i = 0; filters && i < nfilters; ++i) {
        const std::string pattern = filters[i].pattern ? filters[i].pattern : "*";
        size_t at = 0;
        while (at <= pattern.size()) {
            const size_t end = std::min(pattern.find(';', at), pattern.size());
            const std::string ext = pattern.substr(at, end - at);
            if (ext == "*") [types addObject:UTTypeItem];
            else if (!ext.empty())
                if (UTType* t = [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:ext.c_str()]]) [types addObject:t];
            at = end + 1;
        }
    }
    if (types.count == 0) [types addObject:UTTypeItem];
    UIWindow* uiWindow = window ? (__bridge UIWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                             SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr)
                                : nil;
    UIViewController* top = uiWindow.rootViewController;
    while (top.presentedViewController) top = top.presentedViewController;
    if (!top || g_pending) { // (no window to show it on, or one already open)
        SDL_SetError("no document picker now");
        callback(userdata, nullptr, -1);
        return;
    }
    SFDocumentPickerDelegate* d = [SFDocumentPickerDelegate new];
    d.callback = callback;
    d.userdata = userdata;
    g_pending = d;
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types asCopy:YES];
    picker.allowsMultipleSelection = allowMany;
    picker.delegate = d;
    [top presentViewController:picker animated:YES completion:nil];
}

} // namespace sf::dialogs
