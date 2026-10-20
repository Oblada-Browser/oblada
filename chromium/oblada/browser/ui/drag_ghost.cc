#include "oblada/browser/ui/drag_ghost.h"

#include <algorithm>
#include <utility>

#include "base/numerics/safe_conversions.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace oblada {

namespace {

constexpr int kHeight = 32;
constexpr int kMinWidth = 180;
constexpr int kMaxWidth = 240;
constexpr int kCornerRadius = 8;
constexpr int kIconSize = 16;

}  // namespace

DragGhost::DragGhost(views::Widget* parent)
    : widget_(std::make_unique<views::Widget>()) {
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.parent = parent->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.accept_events = false;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  params.z_order = ui::ZOrderLevel::kFloatingUIElement;
  params.name = "ObladaDragGhost";
  widget_->Init(std::move(params));

  auto chip = std::make_unique<views::View>();
  chip->SetBackground(views::CreateRoundedRectBackground(
      kColorTabBackgroundActiveFrameActive, kCornerRadius));
  auto* layout = chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 10), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  icon_ = chip->AddChildView(std::make_unique<views::ImageView>());
  icon_->SetImageSize(gfx::Size(kIconSize, kIconSize));
  title_ = chip->AddChildView(std::make_unique<views::Label>());
  title_->SetEnabledColor(kColorTabForegroundActiveFrameActive);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  layout->SetFlexForView(title_, 1);
  widget_->SetContentsView(std::move(chip));
  widget_->SetOpacity(0.9f);
}

DragGhost::~DragGhost() = default;

void DragGhost::Show(content::WebContents* contents,
                     const gfx::Point& point_in_screen,
                     const gfx::PointF& grab) {
  if (!contents) {
    return;
  }
  gfx::Image favicon;
  if (auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents)) {
    favicon = driver->GetFavicon();
  }
  icon_->SetImage(favicon.IsEmpty() ? favicon::GetDefaultFaviconModel()
                                    : ui::ImageModel::FromImage(favicon));
  title_->SetText(contents->GetTitle());

  const int width = std::clamp(
      widget_->GetContentsView()->GetPreferredSize().width(), kMinWidth,
      kMaxWidth);
  // It ignores the pointer, which goes through to what's under it.
  widget_->SetBounds(gfx::Rect(
      point_in_screen -
          gfx::Vector2d(base::ClampRound(std::clamp(grab.x(), 0.f, 1.f) * width),
                        base::ClampRound(std::clamp(grab.y(), 0.f, 1.f) * kHeight)),
      gfx::Size(width, kHeight)));
  if (!widget_->IsVisible()) {
    widget_->ShowInactive();
  }
}

void DragGhost::Hide() {
  widget_->Hide();
}

gfx::NativeWindow DragGhost::GetNativeWindow() const {
  return widget_->GetNativeWindow();
}

}  // namespace oblada
