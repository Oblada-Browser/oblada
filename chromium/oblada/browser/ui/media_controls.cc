#include "oblada/browser/ui/media_controls.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/media_player_id.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "oblada/browser/l10n.h"
#include "services/media_session/public/cpp/media_image.h"
#include "services/media_session/public/cpp/media_metadata.h"
#include "services/media_session/public/cpp/media_position.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/text_utils.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"

namespace oblada::media {

namespace {

using media_session::mojom::MediaPictureInPictureState;
using media_session::mojom::MediaPlaybackState;
using media_session::mojom::MediaSessionAction;
using media_session::mojom::MediaSessionInfo;
using SuspendType = media_session::mojom::MediaSession::SuspendType;

// A card stacked (its row of buttons only) and spread out (what's playing above
// them).
constexpr int kCollapsedHeight = 34;
constexpr int kExpandedHeight = 112;
// Stacked, the cards behind the front one peek out above it this far each, and
// are smaller and fainter; only so many do.
constexpr int kPeek = 8;
constexpr double kBehindScale = 0.04;
constexpr double kBehindFade = 0.3;
constexpr int kMaxBehind = 2;
// Between spread out cards, and below the cards (above the workspaces).
constexpr int kGap = 6;
constexpr int kBottomGap = 4;
constexpr int kCornerRadius = 10;
constexpr int kPadding = 10;
// The row of buttons keeps this much from the card's sides.
constexpr int kRowPadding = 6;
constexpr int kButtonSize = 26;
constexpr int kSmallButtonSize = 22;
constexpr int kButtonRadius = 5;
constexpr int kBarHeight = 4;
constexpr int kThumbRadius = 4;
// Between the bar and the times either side of it.
constexpr int kBarSpacing = 8;

constexpr base::TimeDelta kExpandDuration = base::Milliseconds(300);
constexpr base::TimeDelta kPresenceDuration = base::Milliseconds(250);
// A card shows this long after its tab stops being the one shown, so that going
// through the tabs doesn't make it flicker.
constexpr base::TimeDelta kRevealDelay = base::Milliseconds(500);
// How often the time playing moves on.
constexpr base::TimeDelta kTick = base::Seconds(1);

double Lerp(double from, double to, double progress) {
  return from + (to - from) * progress;
}

// The order sessions started playing in: the newest card is in front.
int64_t NextOrder() {
  static int64_t order = 0;
  return ++order;
}

// "3:07", or "1:02:03" for an hour or more.
std::u16string FormatTime(base::TimeDelta time) {
  const int seconds = static_cast<int>(std::max<int64_t>(0, time.InSeconds()));
  return base::ASCIIToUTF16(
      seconds >= 3600 ? base::StringPrintf("%d:%02d:%02d", seconds / 3600,
                                           seconds / 60 % 60, seconds % 60)
                      : base::StringPrintf("%d:%02d", seconds / 60, seconds % 60));
}

// The cards' colors, from the sidebar's: darker than it in a dark theme, lighter in
// a light one.
struct Palette {
  SkColor background;
  SkColor border;
  SkColor text;
  SkColor secondary;
  SkColor disabled;
  SkColor highlight;
  SkColor track;
  SkColor fill;
};

Palette GetPalette(const ui::ColorProvider* colors) {
  const SkColor text = colors->GetColor(kColorTabForegroundActiveFrameActive);
  const SkColor frame = colors->GetColor(ui::kColorFrameActive);
  const bool dark = color_utils::IsDark(frame);
  return {
      .background = dark ? color_utils::AlphaBlend(SK_ColorBLACK, frame, 0.6f)
                         : color_utils::AlphaBlend(SK_ColorWHITE, frame, 0.7f),
      .border = SkColorSetA(text, 0x1F),
      .text = text,
      .secondary = SkColorSetA(text, 0xB3),
      .disabled = SkColorSetA(text, 0x4D),
      .highlight = SkColorSetA(text, 0x1F),
      .track = SkColorSetA(text, 0x33),
      .fill = color_utils::AlphaBlend(
          text, colors->GetColor(ui::kColorSysPrimary), 0.3f),
  };
}

// A card's button: an icon, on a rounded highlight under the pointer.
class CardButton : public views::ImageButton {
  METADATA_HEADER(CardButton, views::ImageButton)

 public:
  CardButton(PressedCallback callback, int size)
      : views::ImageButton(std::move(callback)) {
    SetPreferredSize(gfx::Size(size, size));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kButtonRadius);
  }

  // `icon`, `icon_size` wide, named by l10n's `name`.
  void SetIcon(const gfx::VectorIcon& icon,
               int icon_size,
               std::string_view name) {
    if (icon_ == &icon && name_ == name) {
      return;
    }
    icon_ = &icon;
    icon_size_ = icon_size;
    name_ = name;
    SetTooltipText(L10n(name));
    GetViewAccessibility().SetName(L10n(name));
    UpdateImages();
  }

  // views::ImageButton:
  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    UpdateImages();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetEnabled() && GetColorProvider() &&
        (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED)) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(GetPalette(GetColorProvider()).highlight);
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kButtonRadius, flags);
    }
    views::ImageButton::PaintButtonContents(canvas);
  }

 private:
  void UpdateImages() {
    if (!icon_ || !GetColorProvider()) {
      return;
    }
    const Palette palette = GetPalette(GetColorProvider());
    SetImageModel(STATE_NORMAL, ui::ImageModel::FromVectorIcon(
                                    *icon_, palette.text, icon_size_));
    SetImageModel(STATE_DISABLED, ui::ImageModel::FromVectorIcon(
                                      *icon_, palette.disabled, icon_size_));
  }

  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  int icon_size_ = 0;
  std::string_view name_;
};

BEGIN_METADATA(CardButton)
END_METADATA

// How far along it is: the time played, a bar that can be dragged (or clicked)
// to go elsewhere, and how long it lasts.
class Progress : public views::View {
  METADATA_HEADER(Progress, views::View)

 public:
  using SeekCallback = base::RepeatingCallback<void(base::TimeDelta)>;

  explicit Progress(SeekCallback seek) : seek_(std::move(seek)) {}

  void SetPosition(const std::optional<media_session::MediaPosition>& position,
                   bool seekable) {
    // Live media has no end: nothing to show.
    position_ = position && position->duration().is_positive() &&
                        !position->duration().is_max()
                    ? position
                    : std::nullopt;
    seekable_ = seekable && position_;
    if (!seekable_) {
      dragging_ = false;
    }
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    if (!position_ || !GetColorProvider()) {
      return;
    }
    const Palette palette = GetPalette(GetColorProvider());
    const base::TimeDelta duration = position_->duration();
    const base::TimeDelta elapsed = std::clamp(
        dragging_ ? drag_time_ : position_->GetPosition(), base::TimeDelta(),
        duration);
    const gfx::Rect bar = BarBounds();
    const int text_width = TextWidth();
    canvas->DrawStringRectWithFlags(
        FormatTime(elapsed), font_list_, palette.secondary,
        gfx::Rect(0, 0, text_width, height()),
        gfx::Canvas::TEXT_ALIGN_LEFT | gfx::Canvas::NO_ELLIPSIS);
    canvas->DrawStringRectWithFlags(
        FormatTime(duration), font_list_, palette.secondary,
        gfx::Rect(width() - text_width, 0, text_width, height()),
        gfx::Canvas::TEXT_ALIGN_RIGHT | gfx::Canvas::NO_ELLIPSIS);

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.track);
    canvas->DrawRoundRect(gfx::RectF(bar), kBarHeight / 2.f, flags);
    const float done = static_cast<float>(bar.width() * (elapsed / duration));
    flags.setColor(palette.fill);
    canvas->DrawRoundRect(gfx::RectF(bar.x(), bar.y(), done, bar.height()),
                          kBarHeight / 2.f, flags);
    if (seekable_ && (hovered_ || dragging_)) {
      canvas->DrawCircle(gfx::PointF(bar.x() + done, bar.CenterPoint().y()),
                         kThumbRadius, flags);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!seekable_ || !event.IsOnlyLeftMouseButton()) {
      return false;
    }
    dragging_ = true;
    drag_time_ = TimeAt(event.x());
    SchedulePaint();
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (!dragging_) {
      return false;
    }
    drag_time_ = TimeAt(event.x());
    SchedulePaint();
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (!dragging_) {
      return;
    }
    dragging_ = false;
    seek_.Run(TimeAt(event.x()));
    SchedulePaint();
  }

  void OnMouseCaptureLost() override {
    dragging_ = false;
    SchedulePaint();
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    hovered_ = true;
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    hovered_ = false;
    SchedulePaint();
  }

 private:
  // Wide enough for any time up to the duration (its digits all 8s, the widest), so
  // that the bar keeps still as the time played changes.
  int TextWidth() const {
    std::u16string widest = FormatTime(position_->duration());
    std::ranges::replace_if(
        widest, [](char16_t c) { return c >= u'0' && c <= u'9'; }, u'8');
    return gfx::GetStringWidth(widest, font_list_) + 1;
  }

  gfx::Rect BarBounds() const {
    const int x = TextWidth() + kBarSpacing;
    return gfx::Rect(x, (height() - kBarHeight) / 2,
                     std::max(0, width() - 2 * x), kBarHeight);
  }

  base::TimeDelta TimeAt(int x) const {
    const gfx::Rect bar = BarBounds();
    if (!position_ || bar.width() <= 0) {
      return base::TimeDelta();
    }
    const double fraction =
        std::clamp(static_cast<double>(x - bar.x()) / bar.width(), 0.0, 1.0);
    return position_->duration() * fraction;
  }

  SeekCallback seek_;
  std::optional<media_session::MediaPosition> position_;
  bool seekable_ = false;
  bool hovered_ = false;
  bool dragging_ = false;
  base::TimeDelta drag_time_;
  const gfx::FontList font_list_ =
      gfx::FontList().DeriveWithSizeDelta(-2).DeriveWithWeight(
          gfx::Font::Weight::MEDIUM);
};

BEGIN_METADATA(Progress)
END_METADATA

}  // namespace

// A tab's media, as its media session tells it (what Chromium's own media controls
// show too): what's playing, how far along, what it can do.
class Session : public content::WebContentsObserver,
                public media_session::mojom::MediaSessionObserver {
 public:
  Session(tabs::TabInterface* tab, base::RepeatingClosure changed)
      : content::WebContentsObserver(tab->GetContents()),
        tab_(tab),
        changed_(std::move(changed)) {
    // Tabs without media don't get a session just for this: they have one once
    // they play something.
    if (content::MediaSession::GetIfExists(web_contents())) {
      Bind();
    }
  }
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  ~Session() override = default;

  tabs::TabInterface* tab() const { return tab_; }

  // Whether it has a card: it played since it last became controllable (paused, it
  // keeps it), and wasn't put away since it last started playing. Picture in
  // picture has controls of its own.
  bool WantsCard() const {
    return info_ && started_ && !dismissed_ && (controllable() || tab_muted()) &&
           info_->picture_in_picture_state !=
               MediaPictureInPictureState::kInPictureInPicture;
  }

  // Whether its buttons work. Chromium takes the media of a muted tab out of its
  // session (not to pause others): it plays on, but only its sound can be turned
  // back on.
  bool controllable() const {
    return info_ && info_->is_controllable &&
           info_->state != MediaSessionInfo::SessionState::kInactive;
  }

  // When it started playing, or came back after being put away: newer cards are
  // in front.
  int64_t order() const { return order_; }

  // Not before then (see kRevealDelay).
  base::TimeTicks reveal_at() const { return reveal_at_; }
  void set_reveal_at(base::TimeTicks time) { reveal_at_ = time; }

  bool playing() const {
    return info_ && info_->playback_state == MediaPlaybackState::kPlaying;
  }
  // The player muted (by the page, or the card), or the whole tab.
  bool muted() const { return (info_ && info_->muted) || tab_muted(); }
  bool Can(MediaSessionAction action) const {
    return actions_.contains(action);
  }
  const std::u16string& title() const { return metadata_.title; }
  // The artist, else the site.
  const std::u16string& subtitle() const {
    return metadata_.artist.empty() ? metadata_.source_title
                                    : metadata_.artist;
  }
  const std::optional<media_session::MediaPosition>& position() const {
    return position_;
  }

  void PlayPause() {
    if (content::MediaSession* session = GetSession()) {
      if (playing()) {
        session->Suspend(SuspendType::kUI);
      } else {
        session->Resume(SuspendType::kUI);
      }
    }
  }

  void PreviousTrack() {
    if (content::MediaSession* session = GetSession()) {
      session->PreviousTrack();
    }
  }

  void NextTrack() {
    if (content::MediaSession* session = GetSession()) {
      session->NextTrack();
    }
  }

  void SeekTo(base::TimeDelta time) {
    if (content::MediaSession* session = GetSession()) {
      session->SeekTo(time);
    }
  }

  void EnterPictureInPicture() {
    if (content::MediaSession* session = GetSession()) {
      session->EnterPictureInPicture();
    }
  }

  // Its sound: its player's, which keeps it in its session, if it has just one;
  // else the tab's, as the tab strip's button does.
  void ToggleMute() {
    content::MediaSession* session = GetSession();
    if (muted()) {
      if (session && info_ && info_->muted) {
        session->SetMute(false);
      }
      if (tab_muted()) {
        SetTabAudioMuted(web_contents(), false, TabMutedReason::kAudioIndicator,
                         std::string());
      }
    } else if (session && info_ && info_->audio_video_states &&
               info_->audio_video_states->size() == 1) {
      session->SetMute(true);
    } else if (web_contents()) {
      SetTabAudioMuted(web_contents(), true, TabMutedReason::kAudioIndicator,
                       std::string());
    }
  }

  // Pauses it, and puts its card away until it plays again.
  void Dismiss() {
    dismissed_ = true;
    if (content::MediaSession* session = GetSession()) {
      session->Suspend(SuspendType::kUI);
    }
    changed_.Run();
  }

  // Shows its tab (and so the workspace it's in).
  void ShowTab() {
    BrowserWindowInterface* browser = tab_->GetBrowserWindowInterface();
    TabStripModel* model = browser ? browser->GetTabStripModel() : nullptr;
    const int index =
        model ? model->GetIndexOfTab(tab_) : TabStripModel::kNoTab;
    if (index != TabStripModel::kNoTab) {
      model->ActivateTabAt(
          index, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kOther));
    }
  }

  base::WeakPtr<Session> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // content::WebContentsObserver:
  void MediaStartedPlaying(const MediaPlayerInfo& info,
                           const content::MediaPlayerId& id) override {
    if (!receiver_.is_bound()) {
      Bind();
    }
  }

  void DidUpdateAudioMutingState(bool muted) override { changed_.Run(); }

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr info) override {
    const bool was_playing = playing();
    info_ = std::move(info);
    if (!controllable()) {
      // A muted tab's media is out of its session, but still playing.
      if (!tab_muted()) {
        started_ = false;
        dismissed_ = false;
      }
    } else if (playing() && !was_playing && (!started_ || dismissed_)) {
      started_ = true;
      dismissed_ = false;
      order_ = NextOrder();
    }
    changed_.Run();
  }

  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override {
    metadata_ = metadata.value_or(media_session::MediaMetadata());
    changed_.Run();
  }

  void MediaSessionActionsChanged(
      const std::vector<MediaSessionAction>& actions) override {
    actions_ = base::flat_set<MediaSessionAction>(actions);
    changed_.Run();
  }

  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override {}

  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override {
    position_ = position;
    changed_.Run();
  }

 private:
  bool tab_muted() const {
    return web_contents() && web_contents()->IsAudioMuted();
  }

  void Bind() {
    content::MediaSession::Get(web_contents())
        ->AddObserver(receiver_.BindNewPipeAndPassRemote());
  }

  content::MediaSession* GetSession() const {
    return web_contents() ? content::MediaSession::GetIfExists(web_contents())
                          : nullptr;
  }

  const raw_ptr<tabs::TabInterface> tab_;
  base::RepeatingClosure changed_;
  mojo::Receiver<media_session::mojom::MediaSessionObserver> receiver_{this};
  media_session::mojom::MediaSessionInfoPtr info_;
  media_session::MediaMetadata metadata_;
  base::flat_set<MediaSessionAction> actions_;
  std::optional<media_session::MediaPosition> position_;
  bool started_ = false;
  bool dismissed_ = false;
  int64_t order_ = 0;
  base::TimeTicks reveal_at_;
  base::WeakPtrFactory<Session> weak_factory_{this};
};

// A session's card: its row of buttons, and above it, shown as the cards spread
// out, what's playing.
class MediaCard : public views::View {
  METADATA_HEADER(MediaCard, views::View)

 public:
  MediaCard(Session* session, gfx::AnimationDelegate* delegate)
      : presence_(delegate) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetMasksToBounds(true);
    layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kCornerRadius));
    layer()->SetIsFastRoundedCorner(true);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    presence_.SetSlideDuration(kPresenceDuration);
    presence_.SetTweenType(gfx::Tween::EASE_OUT);

    details_ = AddChildView(std::make_unique<views::View>());
    details_->SetPaintToLayer();
    details_->layer()->SetFillsBoundsOpaquely(false);
    title_ = details_->AddChildView(std::make_unique<views::Label>());
    title_->SetFontList(gfx::FontList().DeriveWithSizeDelta(1).DeriveWithWeight(
        gfx::Font::Weight::SEMIBOLD));
    artist_ = details_->AddChildView(std::make_unique<views::Label>());
    artist_->SetFontList(gfx::FontList().DeriveWithSizeDelta(-1));
    for (views::Label* label : {title_.get(), artist_.get()}) {
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetElideBehavior(gfx::FADE_TAIL);
      label->SetAutoColorReadabilityEnabled(false);
      // On a layer of its own, which isn't opaque.
      label->SetSubpixelRenderingEnabled(false);
    }
    pip_ = details_->AddChildView(std::make_unique<CardButton>(
        Act(&Session::EnterPictureInPicture), kSmallButtonSize));
    pip_->SetIcon(vector_icons::kPictureInPictureAltIcon, 16, "media.pip");
    close_ = details_->AddChildView(std::make_unique<CardButton>(
        Act(&Session::Dismiss), kSmallButtonSize));
    close_->SetIcon(vector_icons::kCloseIcon, 16, "media.close");
    progress_ = details_->AddChildView(std::make_unique<Progress>(
        base::BindRepeating(&MediaCard::SeekTo, base::Unretained(this))));

    previous_ = AddChildView(std::make_unique<CardButton>(
        Act(&Session::PreviousTrack), kButtonSize));
    previous_->SetIcon(vector_icons::kSkipPreviousFilledIcon, 20,
                       "media.previous");
    play_ = AddChildView(
        std::make_unique<CardButton>(Act(&Session::PlayPause), kButtonSize));
    next_ = AddChildView(
        std::make_unique<CardButton>(Act(&Session::NextTrack), kButtonSize));
    next_->SetIcon(vector_icons::kSkipNextFilledIcon, 20, "media.next");
    mute_ = AddChildView(
        std::make_unique<CardButton>(Act(&Session::ToggleMute), kButtonSize));

    Update(session);
  }
  MediaCard(const MediaCard&) = delete;
  MediaCard& operator=(const MediaCard&) = delete;
  ~MediaCard() override = default;

  // Its session's tab; null once the session is gone (as the card goes too).
  const tabs::TabInterface* tab() const { return tab_; }
  int64_t order() const { return order_; }
  // How far it has come in (1) or gone (0).
  gfx::SlideAnimation& presence() { return presence_; }

  // Shows what `session` plays; null once it's gone.
  void Update(Session* session) {
    if (!session) {
      session_.reset();
      tab_ = nullptr;
      ticker_.Stop();
      return;
    }
    session_ = session->GetWeakPtr();
    tab_ = session->tab();
    order_ = session->order();
    title_->SetText(session->title());
    artist_->SetText(session->subtitle());
    GetViewAccessibility().SetName(session->title());
    const bool playing = session->playing();
    play_->SetIcon(playing ? vector_icons::kPauseFilledIcon
                           : vector_icons::kPlayArrowFilledFlippableIcon,
                   20, playing ? "media.pause" : "media.play");
    const bool controllable = session->controllable();
    play_->SetEnabled(controllable &&
                      session->Can(playing ? MediaSessionAction::kPause
                                           : MediaSessionAction::kPlay));
    previous_->SetEnabled(controllable &&
                          session->Can(MediaSessionAction::kPreviousTrack));
    next_->SetEnabled(controllable &&
                      session->Can(MediaSessionAction::kNextTrack));
    const bool muted = session->muted();
    mute_->SetIcon(muted ? vector_icons::kVolumeOffIcon
                         : vector_icons::kVolumeUpFilledIcon,
                   muted ? 18 : 16, muted ? "media.unmute" : "media.mute");
    pip_->SetVisible(controllable &&
                     session->Can(MediaSessionAction::kEnterPictureInPicture));
    progress_->SetPosition(session->position(),
                           controllable &&
                               session->Can(MediaSessionAction::kSeekTo));
    UpdateTicker();
    InvalidateLayout();
  }

  // How spread out the cards are (see MediaControls::expansion_): what's playing
  // shows as much.
  void SetExpansion(double expansion) {
    details_->SetVisible(expansion > 0);
    details_->layer()->SetOpacity(expansion);
    UpdateTicker();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(GetColorProvider());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.background);
    gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, kCornerRadius, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(palette.border);
    bounds.Inset(0.5f);
    canvas->DrawRoundRect(bounds, kCornerRadius - 0.5f, flags);
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    const Palette palette = GetPalette(GetColorProvider());
    title_->SetEnabledColor(palette.text);
    artist_->SetEnabledColor(palette.secondary);
    SchedulePaint();
  }

  void Layout(PassKey) override {
    // The row of buttons at the bottom, spread over the card's width.
    const int row_y = height() - kCollapsedHeight;
    const std::array<views::View*, 4> row = {previous_, play_, next_, mute_};
    const int span = width() - 2 * kRowPadding - kButtonSize;
    for (size_t i = 0; i < row.size(); ++i) {
      row[i]->SetBounds(
          kRowPadding + span * static_cast<int>(i) /
                            static_cast<int>(row.size() - 1),
          row_y + (kCollapsedHeight - kButtonSize) / 2, kButtonSize,
          kButtonSize);
    }

    // What's playing above it, uncovered as the card grows.
    details_->SetBounds(0, height() - kExpandedHeight, width(),
                        kExpandedHeight - kCollapsedHeight);
    const int top = 7;
    close_->SetBounds(width() - kRowPadding - kSmallButtonSize, top,
                      kSmallButtonSize, kSmallButtonSize);
    pip_->SetBounds(close_->x() - 2 - kSmallButtonSize, top, kSmallButtonSize,
                    kSmallButtonSize);
    const int title_end = (pip_->GetVisible() ? pip_->x() : close_->x()) - 4;
    title_->SetBounds(kPadding, top, std::max(0, title_end - kPadding),
                      kSmallButtonSize);
    artist_->SetBounds(kPadding, top + kSmallButtonSize,
                       std::max(0, width() - 2 * kPadding), 16);
    progress_->SetBounds(kPadding, 54, std::max(0, width() - 2 * kPadding), 20);
  }

  // Clicking it (but its buttons) shows its tab.
  bool OnMousePressed(const ui::MouseEvent& event) override {
    return event.IsOnlyLeftMouseButton();
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (session_ && HitTestPoint(event.location())) {
      session_->ShowTab();
    }
  }

 private:
  // A button's press: `action` on the session, while there's one.
  views::Button::PressedCallback Act(void (Session::*action)()) {
    return base::BindRepeating(
        [](MediaCard* card, void (Session::*action)()) {
          if (card->session_) {
            (card->session_.get()->*action)();
          }
        },
        base::Unretained(this), action);
  }

  void SeekTo(base::TimeDelta time) {
    if (session_) {
      session_->SeekTo(time);
    }
  }

  // While the time playing shows and moves on, it's redrawn every second.
  void UpdateTicker() {
    if (session_ && session_->playing() && details_->GetVisible()) {
      if (!ticker_.IsRunning()) {
        ticker_.Start(FROM_HERE, kTick,
                      base::BindRepeating(&views::View::SchedulePaint,
                                          base::Unretained(progress_.get())));
      }
    } else {
      ticker_.Stop();
    }
  }

  base::WeakPtr<Session> session_;
  raw_ptr<const tabs::TabInterface> tab_ = nullptr;
  int64_t order_ = 0;
  gfx::SlideAnimation presence_;
  base::RepeatingTimer ticker_;
  raw_ptr<views::View> details_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> artist_ = nullptr;
  raw_ptr<CardButton> pip_ = nullptr;
  raw_ptr<CardButton> close_ = nullptr;
  raw_ptr<Progress> progress_ = nullptr;
  raw_ptr<CardButton> previous_ = nullptr;
  raw_ptr<CardButton> play_ = nullptr;
  raw_ptr<CardButton> next_ = nullptr;
  raw_ptr<CardButton> mute_ = nullptr;
};

BEGIN_METADATA(MediaCard)
END_METADATA

MediaControls::MediaControls(BrowserWindowInterface* browser)
    : browser_(browser) {
  // The cards have layers, which this one keeps within its bounds.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetMasksToBounds(true);
  SetNotifyEnterExitOnChild(true);
  expansion_.SetSlideDuration(kExpandDuration);
  expansion_.SetTweenType(gfx::Tween::ACCEL_5_70_DECEL_90);
  browser_->GetTabStripModel()->AddObserver(this);
  SyncSessions();
  Update();
}

MediaControls::~MediaControls() {
  // The cards first: their animations call this one.
  RemoveAllChildViews();
}

void MediaControls::SetCompact(bool compact) {
  if (compact_ == compact) {
    return;
  }
  compact_ = compact;
  if (compact_) {
    ResetExpansion();
  }
  SetVisible(!compact_ && !children().empty());
}

void MediaControls::ResetExpansion() {
  expansion_.Reset(0);
  for (MediaCard* card : Cards()) {
    card->SetExpansion(0);
  }
}

gfx::Size MediaControls::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, HeightAt(expansion_.GetCurrentValue()));
}

int MediaControls::HeightAt(double expansion) const {
  double total = 0;
  for (MediaCard* card : Cards()) {
    total += card->presence().GetCurrentValue();
  }
  if (total <= 0) {
    return 0;
  }
  // Stacked: the front card, and those peeking out above it.
  const double stacked =
      kCollapsedHeight * std::min(total, 1.0) +
      kPeek * std::clamp(total - 1, 0.0, static_cast<double>(kMaxBehind));
  // Spread out: one under the other.
  const double spread =
      total * (kExpandedHeight + kGap) - kGap * std::min(total, 1.0);
  return base::ClampRound(Lerp(stacked, spread, expansion) +
                          kBottomGap * std::min(total, 1.0));
}

void MediaControls::Layout(PassKey) {
  const std::vector<MediaCard*> cards = Cards();
  const double expansion = expansion_.GetCurrentValue();
  std::vector<double> presence;
  double total = 0;
  for (MediaCard* card : cards) {
    presence.push_back(card->presence().GetCurrentValue());
    total += presence.back();
  }
  const double stacked_front =
      kPeek * std::clamp(total - 1, 0.0, static_cast<double>(kMaxBehind)) +
      kCollapsedHeight * (std::min(total, 1.0) - 1);
  // From the bottom up, should the height not be the one asked for.
  const int offset = height() - HeightAt(expansion);
  const int card_height =
      base::ClampRound(Lerp(kCollapsedHeight, kExpandedHeight, expansion));

  double above = 0;  // how many cards are above this one, spread out
  double behind = total;  // how many are in front of it, stacked
  for (size_t i = 0; i < cards.size(); ++i) {
    MediaCard* card = cards[i];
    behind -= presence[i];
    const double level = std::min(behind, static_cast<double>(kMaxBehind));
    const double stacked_y = stacked_front - kPeek * level;
    const double spread_y = above * (kExpandedHeight + kGap);
    above += presence[i];
    card->SetBounds(
        0, offset + base::ClampRound(Lerp(stacked_y, spread_y, expansion)),
        width(), card_height);

    const double scale = Lerp(1 - kBehindScale * level, 1, expansion);
    gfx::Transform transform;
    transform.Translate(width() / 2.0, 0);
    transform.Scale(scale, scale);
    transform.Translate(-width() / 2.0, 0);
    card->layer()->SetTransform(transform);
    // Stacked, those past the last to peek out fade away.
    const double stacked_opacity =
        (1 - kBehindFade * level) * std::clamp(kMaxBehind + 1 - behind, 0.0, 1.0);
    card->layer()->SetOpacity(presence[i] * Lerp(stacked_opacity, 1, expansion));
  }
}

void MediaControls::OnMouseEntered(const ui::MouseEvent& event) {
  expansion_.Show();
}

void MediaControls::OnMouseExited(const ui::MouseEvent& event) {
  expansion_.Hide();
}

void MediaControls::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  SyncSessions();
  if (selection.active_tab_changed() && selection.old_tab) {
    for (const auto& session : sessions_) {
      if (session->tab() == selection.old_tab) {
        session->set_reveal_at(base::TimeTicks::Now() + kRevealDelay);
        reveal_timer_.Start(FROM_HERE, kRevealDelay,
                            base::BindOnce(&MediaControls::Update,
                                           base::Unretained(this)));
      }
    }
  }
  Update();
}

void MediaControls::AnimationProgressed(const gfx::Animation* animation) {
  if (animation == &expansion_) {
    for (MediaCard* card : Cards()) {
      card->SetExpansion(expansion_.GetCurrentValue());
    }
  }
  PreferredSizeChanged();
}

void MediaControls::AnimationEnded(const gfx::Animation* animation) {
  AnimationProgressed(animation);
  if (animation == &expansion_) {
    return;
  }
  // The cards gone, once out of sight (later: it's their animation that ends).
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MediaControls::RemoveGoneCards,
                                weak_factory_.GetWeakPtr()));
}

void MediaControls::RemoveGoneCards() {
  for (MediaCard* card : Cards()) {
    if (!card->presence().is_animating() &&
        card->presence().GetCurrentValue() == 0) {
      RemoveChildViewT(card);
    }
  }
  if (children().empty()) {
    ResetExpansion();
    SetVisible(false);
  }
  PreferredSizeChanged();
}

void MediaControls::SyncSessions() {
  TabStripModel* model = browser_->GetTabStripModel();
  std::vector<std::unique_ptr<Session>> sessions;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    auto it = std::ranges::find_if(sessions_, [tab](const auto& session) {
      return session && session->tab() == tab &&
             session->web_contents() == tab->GetContents();
    });
    if (it != sessions_.end()) {
      sessions.push_back(std::move(*it));
    } else {
      sessions.push_back(std::make_unique<Session>(
          tab, base::BindRepeating(&MediaControls::Update,
                                   base::Unretained(this))));
    }
  }
  // The others' tabs are gone (closed, or moved to another window).
  for (const auto& session : sessions_) {
    if (!session) {
      continue;
    }
    for (MediaCard* card : Cards()) {
      if (card->tab() == session->tab()) {
        card->Update(nullptr);
        card->presence().Hide();
      }
    }
  }
  sessions_ = std::move(sessions);
}

void MediaControls::Update() {
  const tabs::TabInterface* active = browser_->GetTabStripModel()->GetActiveTab();
  const base::TimeTicks now = base::TimeTicks::Now();
  for (const auto& session : sessions_) {
    MediaCard* card = CardFor(session->tab());
    const bool wanted = session->WantsCard() && session->tab() != active &&
                        session->reveal_at() <= now;
    if (wanted && !card) {
      card = AddChildView(std::make_unique<MediaCard>(session.get(), this));
      card->SetExpansion(expansion_.GetCurrentValue());
    } else if (card) {
      card->Update(session.get());
    }
    if (card && wanted) {
      card->presence().Show();
    } else if (card) {
      card->presence().Hide();
    }
  }
  // Oldest first, for the newest to be in front of the others.
  std::vector<MediaCard*> cards = Cards();
  std::ranges::stable_sort(cards, {}, &MediaCard::order);
  for (size_t i = 0; i < cards.size(); ++i) {
    ReorderChildView(cards[i], i);
  }
  SetVisible(!compact_ && !cards.empty());
  PreferredSizeChanged();
}

MediaCard* MediaControls::CardFor(const tabs::TabInterface* tab) const {
  for (MediaCard* card : Cards()) {
    if (card->tab() == tab) {
      return card;
    }
  }
  return nullptr;
}

std::vector<MediaCard*> MediaControls::Cards() const {
  std::vector<MediaCard*> cards;
  for (views::View* child : children()) {
    cards.push_back(views::AsViewClass<MediaCard>(child));
  }
  return cards;
}

BEGIN_METADATA(MediaControls)
END_METADATA

}  // namespace oblada::media
