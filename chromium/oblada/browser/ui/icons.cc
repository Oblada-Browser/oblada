#include "oblada/browser/ui/icons.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace oblada::icons {

namespace {

constexpr Icon kIcons[] = {
    // Everyday.
    {"home", &kHomeIcon},
    {"work", &vector_icons::kWorkIcon},
    {"star", &kStarIcon},
    {"bolt", &kBoltIcon},
    {"lightbulb", &vector_icons::kLightbulbIcon},
    {"rocket_launch", &kRocketLaunchIcon},
    {"celebration", &vector_icons::kCelebrationIcon},
    {"globe", &kGlobeIcon},
    // Money and shopping.
    {"payments", &vector_icons::kPaymentsIcon},
    {"credit_card", &kCreditCardIcon},
    {"wallet", &kWalletIcon},
    {"shopping_bag", &vector_icons::kShoppingBagIcon},
    {"storefront", &vector_icons::kStorefrontIcon},
    {"receipt_long", &vector_icons::kReceiptLongIcon},
    {"local_shipping", &vector_icons::kLocalShippingIcon},
    {"loyalty", &vector_icons::kLoyaltyIcon},
    // Travel and places.
    {"flight", &vector_icons::kFlightIcon},
    {"trip", &vector_icons::kTripIcon},
    {"travel_explore", &kTravelExploreIcon},
    {"directions_car", &vector_icons::kDirectionsCarIcon},
    {"location_on", &vector_icons::kLocationOnIcon},
    {"hotel_class", &kHotelClassIcon},
    {"domain", &vector_icons::kDomainIcon},
    {"passport", &vector_icons::kPassportIcon},
    // Leisure.
    {"headphones", &vector_icons::kHeadphonesIcon},
    {"queue_music", &kQueueMusicIcon},
    {"piano", &vector_icons::kPianoIcon},
    {"videogame_asset", &vector_icons::kVideogameAssetIcon},
    {"video_library", &vector_icons::kVideoLibraryIcon},
    {"tv", &kTvIcon},
    {"photo", &vector_icons::kPhotoIcon},
    {"videocam", &vector_icons::kVideocamIcon},
    // Work and study.
    {"code", &kCodeIcon},
    {"database", &vector_icons::kDatabaseIcon},
    {"science", &vector_icons::kScienceIcon},
    {"menu_book", &kMenuBookIcon},
    {"sticky_note", &vector_icons::kStickyNote2Icon},
    {"edit", &kEditIcon},
    {"checklist", &vector_icons::kChecklistIcon},
    {"table_chart", &vector_icons::kTableChartIcon},
    // People.
    {"mail", &vector_icons::kMailIcon},
    {"chat", &vector_icons::kChatIcon},
    {"call", &vector_icons::kCallIcon},
    {"calendar", &vector_icons::kCalendarTodayIcon},
    {"person", &kPersonFilledIcon},
    {"family", &vector_icons::kFamilyLinkIcon},
    {"group", &kGroupCustomIcon},
    {"pets", &vector_icons::kPetsIcon},
    // Privacy and devices.
    {"fingerprint", &kFingerprintIcon},
    {"lock", &vector_icons::kLockIcon},
    {"shield", &vector_icons::kShieldIcon},
    {"security", &kSecurityIcon},
    {"incognito", &kIncognitoIcon},
    {"cookie", &vector_icons::kCookieIcon},
    {"laptop", &kLaptopWindowsIcon},
    {"mobile", &kMobileIcon},
    // Tools.
    {"settings", &kSettingsIcon},
    {"tune", &vector_icons::kTuneIcon},
    {"speed", &kSpeedIcon},
    {"timer", &kTimerIcon},
    {"hourglass", &kHourglassIcon},
    {"keyboard", &vector_icons::kKeyboardIcon},
    {"extension", &vector_icons::kExtensionFilledIcon},
    {"folder", &vector_icons::kFolderOpenIcon},
    // And more (before a picker's "none").
    {"description", &vector_icons::kDescriptionIcon},
    {"id_card", &vector_icons::kIdCardIcon},
    {"history", &kHistoryIcon},
    {"devices", &kDevicesIcon},
    {"speaker", &kSpeakerIcon},
    {"mic", &vector_icons::kMicIcon},
    {"thumb_up", &vector_icons::kThumbUpIcon},
};

// Other names for them: Firefox's container icons, and Oblada's older ones.
constexpr std::pair<std::string_view, std::string_view> kAliases[] = {
    {"briefcase", "work"},     {"dollar", "payments"},
    {"circle-dollar-sign", "payments"}, {"cart", "shopping_bag"},
    {"shopping-cart", "shopping_bag"},  {"gift", "celebration"},
    {"vacation", "flight"},    {"pet", "pets"},
    {"chill", "headphones"},
};

// Firefox's container icons none of Oblada's is.
constexpr std::string_view kFirefoxOnly[] = {"food", "fruit", "tree", "fence"};

constexpr int kCell = 32;
constexpr int kGap = 2;
constexpr int kColumns = 8;
constexpr int kIconSize = 18;
constexpr int kSwatch = 22;

// What the picker shows, and passes on.
struct PickerState {
  std::string icon;
  std::vector<PickerColor> colors;
  std::string color;
  base::RepeatingCallback<void(const std::string&)> on_icon;
  base::RepeatingCallback<void(const std::string&)> on_color;

  // The icons' color: the one picked, else the bubble's text's.
  SkColor IconColor(const ui::ColorProvider* provider) const {
    for (const PickerColor& option : colors) {
      if (option.id == color) {
        return option.color;
      }
    }
    return provider->GetColor(ui::kColorIcon);
  }
};

// An icon to pick (or, with an empty id, none: a dot), or a color.
class Cell : public views::Button {
  METADATA_HEADER(Cell, views::Button)

 public:
  Cell(PickerState* state, std::string id, bool is_color, int size)
      : views::Button(base::BindRepeating(&Cell::Pick, base::Unretained(this))),
        state_(state),
        id_(std::move(id)),
        is_color_(is_color) {
    SetPreferredSize(gfx::Size(size, size));
    std::string name = id_.empty() ? std::string("—") : id_;
    std::ranges::replace(name, '_', ' ');
    SetTooltipText(base::UTF8ToUTF16(name));
    GetViewAccessibility().SetName(base::UTF8ToUTF16(name));
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    if (!provider) {
      return;
    }
    const SkColor foreground = provider->GetColor(ui::kColorIcon);
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (is_color_) {
      SkColor color = foreground;
      for (const PickerColor& option : state_->colors) {
        if (option.id == id_) {
          color = option.color;
        }
      }
      if (state_->color == id_) {
        flags.setStyle(cc::PaintFlags::kStroke_Style);
        flags.setStrokeWidth(2);
        flags.setColor(foreground);
        canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 2 - 1, flags);
        flags.setStyle(cc::PaintFlags::kFill_Style);
      }
      flags.setColor(color);
      canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 2 - 4, flags);
      return;
    }
    const bool selected = Canonical(state_->icon) == id_;
    if (selected || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      flags.setColor(SkColorSetA(foreground, selected ? 0x33 : 0x18));
      canvas->DrawRoundRect(bounds, 8, flags);
    }
    const SkColor color = state_->IconColor(provider);
    const ui::ImageModel image = Image(id_, color, kIconSize);
    if (image.IsEmpty()) {
      flags.setColor(color);
      canvas->DrawCircle(bounds.CenterPoint(), 5, flags);
      return;
    }
    const gfx::ImageSkia skia = image.Rasterize(provider);
    canvas->DrawImageInt(skia, (width() - skia.width()) / 2,
                         (height() - skia.height()) / 2);
  }

 private:
  void Pick() {
    if (is_color_) {
      state_->color = id_;
      state_->on_color.Run(id_);
    } else {
      state_->icon = id_;
      state_->on_icon.Run(id_);
    }
    // Every cell, the icons in the new color.
    parent()->parent()->SchedulePaint();
    for (views::View* row : parent()->parent()->children()) {
      for (views::View* cell : row->children()) {
        cell->SchedulePaint();
      }
    }
  }

  raw_ptr<PickerState> state_;
  const std::string id_;
  const bool is_color_;
};

BEGIN_METADATA(Cell)
END_METADATA

// The icons in rows of kColumns.
class Grid : public views::View {
  METADATA_HEADER(Grid, views::View)

 public:
  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int rows =
        (static_cast<int>(children().size()) + kColumns - 1) / kColumns;
    return gfx::Size(kColumns * kCell + (kColumns - 1) * kGap,
                     rows * kCell + std::max(0, rows - 1) * kGap);
  }

  void Layout(PassKey) override {
    for (size_t i = 0; i < children().size(); ++i) {
      const int column = static_cast<int>(i) % kColumns;
      const int row = static_cast<int>(i) / kColumns;
      children()[i]->SetBounds(column * (kCell + kGap), row * (kCell + kGap),
                               kCell, kCell);
    }
  }
};

BEGIN_METADATA(Grid)
END_METADATA

// The bubble, while it's open.
class Picker {
 public:
  Picker(views::View* anchor, std::unique_ptr<PickerState> state)
      : state_(std::move(state)) {
    // Above what's in the lower half of its window (e.g. the workspaces at the
    // sidebar's bottom), else below.
    const views::Widget* window = anchor->GetWidget();
    const bool above =
        window && anchor->GetBoundsInScreen().CenterPoint().y() >
                      window->GetWindowBoundsInScreen().CenterPoint().y();
    delegate_ = std::make_unique<views::BubbleDialogDelegate>(
        anchor,
        above ? views::BubbleBorder::BOTTOM_LEFT : views::BubbleBorder::TOP_LEFT,
        views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
    delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    delegate_->SetShowCloseButton(false);
    delegate_->set_margins(gfx::Insets(10));

    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
    if (!state_->colors.empty()) {
      auto* row = contents->AddChildView(std::make_unique<views::View>());
      row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 5),
          (kColumns * kCell + (kColumns - 1) * kGap - 10 -
           static_cast<int>(state_->colors.size()) * kSwatch) /
              std::max<int>(1, state_->colors.size() - 1)));
      for (const PickerColor& color : state_->colors) {
        row->AddChildView(std::make_unique<Cell>(state_.get(), color.id,
                                                 /*is_color=*/true, kSwatch));
      }
    }
    // A theme a row, and none (a dot) last.
    auto* grid = contents->AddChildView(std::make_unique<Grid>());
    for (const Icon& icon : All()) {
      grid->AddChildView(std::make_unique<Cell>(
          state_.get(), std::string(icon.id), /*is_color=*/false, kCell));
    }
    grid->AddChildView(std::make_unique<Cell>(state_.get(), std::string(),
                                              /*is_color=*/false, kCell));
    delegate_->SetContentsView(std::move(contents));
    widget_ = views::BubbleDialogDelegate::CreateBubble(
        delegate_.get(),
        base::BindOnce(
            [](base::WeakPtr<Picker> picker, views::Widget::ClosedReason) {
              // Not from within its own closing.
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, base::BindOnce(
                                 [](base::WeakPtr<Picker> picker) {
                                   if (picker && Current().get() == picker.get()) {
                                     Current().reset();
                                   }
                                 },
                                 picker));
            },
            weak_factory_.GetWeakPtr()));
    widget_->Show();
  }

  // The one open, if any.
  static std::unique_ptr<Picker>& Current() {
    static base::NoDestructor<std::unique_ptr<Picker>> current;
    return *current;
  }

 private:
  std::unique_ptr<PickerState> state_;
  std::unique_ptr<views::BubbleDialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::WeakPtrFactory<Picker> weak_factory_{this};
};

}  // namespace

base::span<const Icon> All() {
  return kIcons;
}

std::string_view Canonical(std::string_view id) {
  for (const auto& [alias, canonical] : kAliases) {
    if (id == alias) {
      return canonical;
    }
  }
  for (const Icon& icon : kIcons) {
    if (id == icon.id) {
      return icon.id;
    }
  }
  if (std::ranges::find(kFirefoxOnly, id) != std::end(kFirefoxOnly)) {
    return id;
  }
  return std::string_view();
}

const gfx::VectorIcon* Find(std::string_view id) {
  const std::string_view canonical = Canonical(id);
  for (const Icon& icon : kIcons) {
    if (canonical == icon.id) {
      return icon.vector;
    }
  }
  return nullptr;
}

std::string_view FirefoxName(std::string_view id) {
  const std::string_view canonical = Canonical(id);
  if (std::ranges::find(kFirefoxOnly, canonical) != std::end(kFirefoxOnly) ||
      canonical == "fingerprint") {
    return canonical;
  }
  for (const auto& [alias, oblada] : kAliases) {
    // The first name of each (Firefox's come first).
    if (oblada == canonical && alias.find('-') == std::string_view::npos) {
      return alias;
    }
  }
  return "circle";
}

ui::ImageModel Image(std::string_view id, SkColor color, int size) {
  const gfx::VectorIcon* icon = Find(id);
  return icon ? ui::ImageModel::FromVectorIcon(*icon, color, size)
              : ui::ImageModel();
}

std::string MaskUrl(std::string_view id, int size) {
  const gfx::VectorIcon* icon = Find(id);
  if (!icon) {
    return std::string();
  }
  const gfx::ImageSkia image =
      gfx::CreateVectorIcon(*icon, size * 2, SK_ColorBLACK);
  std::optional<std::vector<uint8_t>> png = gfx::PNGCodec::EncodeBGRASkBitmap(
      image.GetRepresentation(1.0f).GetBitmap(), /*discard_transparency=*/false);
  return png ? "data:image/png;base64," + base::Base64Encode(*png)
             : std::string();
}

void ShowPicker(views::View* anchor,
                std::string icon,
                std::vector<PickerColor> colors,
                std::string color,
                base::RepeatingCallback<void(const std::string&)> on_icon,
                base::RepeatingCallback<void(const std::string&)> on_color) {
  if (!anchor || !anchor->GetWidget()) {
    return;
  }
  Picker::Current().reset();
  auto state = std::make_unique<PickerState>();
  state->icon = std::string(Canonical(icon));
  state->colors = std::move(colors);
  state->color = std::move(color);
  state->on_icon = std::move(on_icon);
  state->on_color = std::move(on_color);
  Picker::Current() = std::make_unique<Picker>(anchor, std::move(state));
}

}  // namespace oblada::icons
