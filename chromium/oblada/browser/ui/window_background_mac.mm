#include "oblada/browser/ui/window_background.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/runtime.h>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "skia/ext/skia_utils_mac.h"
#include "ui/gfx/color_utils.h"

// A window's background, kept with its NSWindow: what Set() asked for, and what
// BrowserNativeWidgetMac last gave Paint() (the view over the glass, the window's
// own color and opacity).
@interface ObladaWindowBackground : NSObject {
 @public
  oblada::window_background::Background background;
  SkColor frameColor;
  double defaultOpacity;
  BOOL painted;
}
@property(nonatomic, weak) NSView* tintView;
@end

@implementation ObladaWindowBackground
@end

namespace oblada::window_background {

namespace {

// The key of the window's ObladaWindowBackground.
char kKey;

// Changes (e.g. to another workspace's colors) fade in.
constexpr CFTimeInterval kFadeDuration = 0.3;

ObladaWindowBackground* StateOf(NSWindow* window, bool create) {
  ObladaWindowBackground* state = objc_getAssociatedObject(window, &kKey);
  if (!state && create) {
    state = [[ObladaWindowBackground alloc] init];
    state->frameColor = SK_ColorGRAY;
    state->defaultOpacity = 0.3;
    objc_setAssociatedObject(window, &kKey, state,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  return state;
}

// The grain's tile as a pattern that covers any window.
CGColorRef GrainPattern() {
  static NSColor* pattern = [] {
    const SkBitmap& tile = GrainTile();
    NSImage* image = skia::SkBitmapToNSImage(tile);
    // In points: Retina pixels.
    image.size = NSMakeSize(tile.width() / 2, tile.height() / 2);
    return [NSColor colorWithPatternImage:image];
  }();
  return pattern.CGColor;
}

// `parent`'s sublayer `name`, a `layer_class` made the first time: as big as it, at
// once.
CALayer* Sublayer(CALayer* parent, NSString* name, Class layer_class) {
  for (CALayer* layer in parent.sublayers) {
    if ([layer.name isEqualToString:name]) {
      return layer;
    }
  }
  CALayer* layer = [layer_class layer];
  layer.name = name;
  layer.frame = parent.bounds;
  layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  // It follows the window's size without lagging behind; only its colors fade.
  layer.actions = @{
    @"bounds" : NSNull.null,
    @"position" : NSNull.null,
    @"frame" : NSNull.null,
    @"backgroundColor" : NSNull.null,
  };
  [parent addSublayer:layer];
  return layer;
}

void Apply(ObladaWindowBackground* state) {
  NSView* tint_view = state.tintView;
  CALayer* layer = tint_view.layer;
  if (!layer) {
    return;
  }
  [CATransaction begin];
  // At once the first time, when the window opens.
  [CATransaction setDisableActions:!state->painted];
  [CATransaction setAnimationDuration:kFadeDuration];
  layer.backgroundColor = NSColor.clearColor.CGColor;

  CAGradientLayer* gradient = base::apple::ObjCCastStrict<CAGradientLayer>(
      Sublayer(layer, @"oblada.gradient", CAGradientLayer.class));
  std::vector<SkColor> colors = state->background.colors;
  if (colors.empty()) {
    colors.push_back(state->frameColor);
  }
  if (colors.size() == 1) {
    colors.push_back(colors.front());
  }
  NSMutableArray* cg_colors = [NSMutableArray array];
  for (SkColor color : colors) {
    [cg_colors addObject:(__bridge id)skia::SkColorToSRGBNSColor(color).CGColor];
  }
  gradient.colors = cg_colors;
  // Layers have y going up: from the top left to the bottom right.
  gradient.startPoint = CGPointMake(0, 1);
  gradient.endPoint = CGPointMake(1, 0);
  gradient.opacity =
      state->background.opacity.value_or(state->defaultOpacity);

  CALayer* grain = Sublayer(layer, @"oblada.grain", CALayer.class);
  grain.backgroundColor = GrainPattern();
  grain.opacity = static_cast<float>(state->background.grain) * kMaxGrain;
  [CATransaction commit];
}

}  // namespace

void Set(gfx::NativeWindow window, const Background& background) {
  NSWindow* ns_window = window.GetNativeNSWindow();
  if (!ns_window) {
    return;
  }
  ObladaWindowBackground* state = StateOf(ns_window, /*create=*/true);
  if (state->background == background) {
    return;
  }
  if (state->background.dark != background.dark) {
    // The glass dark or light with the window's colors, whatever the system's.
    ns_window.appearance =
        background.dark ? [NSAppearance appearanceNamed:*background.dark
                                                            ? NSAppearanceNameDarkAqua
                                                            : NSAppearanceNameAqua]
                        : nil;
  }
  state->background = background;
  Apply(state);
}

void Paint(NSView* tint_view, SkColor frame_color, double default_opacity) {
  NSWindow* window = tint_view.window;
  if (!window) {
    return;
  }
  ObladaWindowBackground* state = StateOf(window, /*create=*/true);
  state.tintView = tint_view;
  state->frameColor = frame_color;
  state->defaultOpacity = default_opacity;
  Apply(state);
  state->painted = YES;
}

Background Get(gfx::NativeWindow window) {
  NSWindow* ns_window = window.GetNativeNSWindow();
  ObladaWindowBackground* state =
      ns_window ? StateOf(ns_window, /*create=*/false) : nil;
  return state ? state->background : Background();
}

double DefaultOpacity(gfx::NativeWindow window) {
  NSWindow* ns_window = window.GetNativeNSWindow();
  ObladaWindowBackground* state =
      ns_window ? StateOf(ns_window, /*create=*/false) : nil;
  return state ? state->defaultOpacity : 0.3;
}

SkBitmap Render(gfx::NativeWindow window, const gfx::Size& size) {
  NSWindow* ns_window = window.GetNativeNSWindow();
  ObladaWindowBackground* state =
      ns_window ? StateOf(ns_window, /*create=*/false) : nil;
  CALayer* layer = state.tintView.layer;
  const CGFloat width = NSWidth(ns_window.contentView.bounds);
  SkBitmap bitmap;
  if (!layer || width <= 0 || size.IsEmpty() ||
      !bitmap.tryAllocN32Pixels(size.width(), size.height())) {
    return SkBitmap();
  }
  // The glass blurs what's behind the window: a plain gray stands for it here.
  const bool dark = state->background.dark.value_or(
      color_utils::IsDark(state->frameColor));
  bitmap.eraseColor(GlassStandIn(dark));
  base::apple::ScopedCFTypeRef<CGColorSpaceRef> color_space(
      CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
  base::apple::ScopedCFTypeRef<CGContextRef> context(CGBitmapContextCreate(
      bitmap.getPixels(), bitmap.width(), bitmap.height(), 8, bitmap.rowBytes(),
      color_space.get(),
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little));
  if (!context) {
    return SkBitmap();
  }
  const CGFloat scale = size.width() / width;
  CGContextScaleCTM(context.get(), scale, scale);
  [layer renderInContext:context.get()];
  return bitmap;
}

}  // namespace oblada::window_background
