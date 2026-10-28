#include "oblada/browser/peek/peek.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tab_helpers.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/sidebar_layout.h"
#include "services/network/public/mojom/referrer_policy.mojom-shared.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_solid_color.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_shadow.h"
#include "ui/views/view_tracker.h"
#include "url/gurl.h"

namespace oblada::peek {

// Gives a peek's page a tab's helpers (TabHelpers lets only its friends do it).
class PeekTabHelpers {
 public:
  static void Attach(content::WebContents* contents) {
    TabHelpers::AttachTabHelpers(contents);
  }
};

namespace {

// The card the page shows in: its corners and shadow, and its distance from the
// page's edges.
constexpr int kCardRadius = 12;
constexpr int kCardElevation = 24;
constexpr int kMinMargin = 16;
constexpr int kMaxMargin = 48;

// The buttons next to it.
constexpr int kButtonSize = 32;
constexpr int kButtonGap = 8;

// How much the page around it darkens.
constexpr float kDim = 0.3f;

// The card grows from the link this long, and goes this long.
constexpr base::TimeDelta kOpening = base::Milliseconds(320);
constexpr base::TimeDelta kClosing = base::Milliseconds(200);
constexpr float kOpeningScale = 0.2f;
constexpr float kClosingScale = 0.95f;

bool CanPeekAt(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

// Scaling by `scale` around `center`.
gfx::Transform ScaledAround(const gfx::Point& center, float scale) {
  gfx::Transform transform;
  transform.Translate(center.x(), center.y());
  transform.Scale(scale, scale);
  transform.Translate(-center.x(), -center.y());
  return transform;
}

class PeekController;
PeekController* ControllerFor(content::WebContents* contents);

// Watches a tab for links to peek at: the one under the mouse, and Option-clicks
// on it (which would otherwise download it), swallowed by the browser.
class PeekTrigger : public content::WebContentsObserver,
                    public content::WebContentsUserData<PeekTrigger> {
 public:
  PeekTrigger(const PeekTrigger&) = delete;
  PeekTrigger& operator=(const PeekTrigger&) = delete;
  ~PeekTrigger() override = default;

  void set_hovered_link(const GURL& url) { hovered_link_ = url; }
  // Keeps the rest of the click going on (moves, the release) from the page.
  void SwallowUntilMouseUp() { swallowing_ = true; }
  const GURL& hovered_link() const { return hovered_link_; }
  gfx::Point last_mouse_point() const { return last_mouse_point_; }

  // content::WebContentsObserver:
  void RenderFrameCreated(content::RenderFrameHost* frame) override {
    Watch(frame->GetRenderWidgetHost());
  }

 private:
  friend class content::WebContentsUserData<PeekTrigger>;

  explicit PeekTrigger(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<PeekTrigger>(*contents),
        // The hosts it's added to go with the tab, before it.
        callback_(base::BindRepeating(&PeekTrigger::OnMouseEvent,
                                      base::Unretained(this))) {
    contents->ForEachRenderFrameHost([this](content::RenderFrameHost* frame) {
      if (frame->IsRenderFrameLive()) {
        Watch(frame->GetRenderWidgetHost());
      }
    });
  }

  // Each frame with a widget of its own (e.g. a cross-site iframe) gets its
  // events there.
  void Watch(content::RenderWidgetHost* host) {
    host->RemoveMouseEventCallback(callback_);
    host->AddMouseEventCallback(callback_);
  }

  bool OnMouseEvent(const blink::WebMouseEvent& event) {
    using Type = blink::WebInputEvent::Type;
    const Type type = event.GetType();
    if (type == Type::kMouseMove || type == Type::kMouseDown) {
      last_mouse_point_ = gfx::ToFlooredPoint(event.PositionInWidget());
    }
    // The rest of a click that peeked.
    if (swallowing_ && type != Type::kMouseDown) {
      swallowing_ = type != Type::kMouseUp;
      return true;
    }
    swallowing_ = false;
    constexpr int kOtherModifiers = blink::WebInputEvent::kShiftKey |
                                    blink::WebInputEvent::kControlKey |
                                    blink::WebInputEvent::kMetaKey;
    if (type != Type::kMouseDown ||
        event.button != blink::WebPointerProperties::Button::kLeft ||
        !(event.GetModifiers() & blink::WebInputEvent::kAltKey) ||
        (event.GetModifiers() & kOtherModifiers) || !CanPeekAt(hovered_link_)) {
      return false;
    }
    swallowing_ = true;
    // Not while the page's widget dispatches its events.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PeekTrigger::Peek, weak_factory_.GetWeakPtr(),
                                  hovered_link_));
    return true;
  }

  void Peek(const GURL& url);

  GURL hovered_link_;
  gfx::Point last_mouse_point_;
  bool swallowing_ = false;
  content::RenderWidgetHost::MouseEventCallback callback_;
  base::WeakPtrFactory<PeekTrigger> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PeekTrigger);

// The card over the page, the page dimmed around it, and the buttons next to it.
class PeekView : public views::View, public views::ViewObserver {
  METADATA_HEADER(PeekView, views::View)

 public:
  struct Actions {
    base::RepeatingClosure close;
    base::RepeatingClosure open_as_tab;
    // Null if the page can't have another side by side.
    base::RepeatingClosure open_side_by_side;
  };

  PeekView(content::WebContents* contents,
           views::View* page,
           const gfx::Point& origin,
           Actions actions)
      : page_(page), origin_(origin), close_(actions.close) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    // As round as the page it covers.
    layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(sidebar::ContentCornerRadius()));

    dim_ = AddChildView(std::make_unique<views::View>());
    dim_->SetPaintToLayer(ui::LAYER_SOLID_COLOR);
    dim_->layer()->AsSolidColor()->SetColor(SkColors::kBlack);
    dim_->layer()->SetOpacity(0.f);
    dim_->SetCanProcessEventsWithinSubtree(false);

    // The card in a frame of its own, for its shadow to move and fade with it.
    frame_ = AddChildView(std::make_unique<views::View>());
    frame_->SetPaintToLayer();
    frame_->layer()->SetFillsBoundsOpaquely(false);
    card_ = frame_->AddChildView(std::make_unique<views::View>());
    card_->SetPaintToLayer();
    card_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kCardRadius));
    card_->layer()->SetIsFastRoundedCorner(true);
    card_->SetBackground(views::CreateSolidBackground(ui::kColorPrimaryBackground));
    card_->SetUseDefaultFillLayout(true);
    web_view_ = card_->AddChildView(std::make_unique<views::WebView>(nullptr));
    web_view_->SetWebContents(contents);
    shadow_ = std::make_unique<views::ViewShadow>(card_, kCardElevation);
    shadow_->SetRoundedCornerRadius(kCardRadius);

    buttons_ = AddChildView(std::make_unique<views::BoxLayoutView>());
    buttons_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    buttons_->SetBetweenChildSpacing(kButtonGap);
    buttons_->SetPaintToLayer();
    buttons_->layer()->SetFillsBoundsOpaquely(false);
    AddButton(vector_icons::kCloseIcon, "peek.close", actions.close);
    AddButton(kOpenInFullIcon, "peek.open_as_tab", actions.open_as_tab);
    if (actions.open_side_by_side) {
      AddButton(kSplitSceneIcon, "peek.open_side_by_side",
                actions.open_side_by_side);
    }

    page_observation_.Observe(page);
    SetBoundsRect(page->bounds());
  }
  PeekView(const PeekView&) = delete;
  PeekView& operator=(const PeekView&) = delete;
  ~PeekView() override = default;

  views::WebView* web_view() { return web_view_; }

  // Grows the card from the link, as the page around it dims.
  void AnimateIn() {
    LayOutCard();
    ui::Layer* frame = frame_->layer();
    frame->SetTransform(
        ScaledAround(origin_ - frame_->bounds().OffsetFromOrigin(),
                     kOpeningScale));
    frame->SetOpacity(0.f);
    buttons_->layer()->SetOpacity(0.f);
    views::AnimationBuilder()
        .Once()
        .SetDuration(kOpening)
        .SetTransform(frame, gfx::Transform(), gfx::Tween::FAST_OUT_SLOW_IN)
        .SetOpacity(dim_->layer(), kDim, gfx::Tween::EASE_IN_OUT)
        .At(base::TimeDelta())
        .SetDuration(kOpening / 3)
        .SetOpacity(frame, 1.f)
        .At(kOpening * 2 / 3)
        .SetDuration(kOpening / 3)
        .SetOpacity(buttons_->layer(), 1.f);
  }

  // Fades the card out (or, `to_page`, grows it over the whole page, which it
  // becomes), then runs `done`.
  void AnimateOut(bool to_page, base::OnceClosure done) {
    ui::Layer* frame = frame_->layer();
    gfx::Transform target;
    if (to_page) {
      const gfx::Rect card = frame_->bounds();
      target.Translate(-card.x(), -card.y());
      target.Scale(static_cast<float>(width()) / card.width(),
                   static_cast<float>(height()) / card.height());
    } else {
      target = ScaledAround(frame_->GetLocalBounds().CenterPoint(),
                            kClosingScale);
    }
    auto [ended, aborted] = base::SplitOnceCallback(std::move(done));
    views::AnimationBuilder builder;
    builder.OnEnded(std::move(ended)).OnAborted(std::move(aborted));
    auto& block = builder.Once()
                      .SetDuration(kClosing)
                      .SetTransform(frame, target, gfx::Tween::FAST_OUT_SLOW_IN)
                      .SetOpacity(dim_->layer(), 0.f, gfx::Tween::EASE_IN_OUT)
                      .SetOpacity(buttons_->layer(), 0.f);
    if (!to_page) {
      block.SetOpacity(frame, 0.f);
    }
  }

  // views::View:
  void Layout(PassKey) override { LayOutCard(); }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    // The page around the card.
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (HitTestPoint(event.location())) {
      close_.Run();
    }
  }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override {
    SetBoundsRect(observed_view->bounds());
  }

  void OnViewIsDeleting(views::View* observed_view) override {
    page_observation_.Reset();
  }

 private:
  void LayOutCard() {
    const gfx::Rect area = GetLocalBounds();
    dim_->SetBoundsRect(area);
    const int margin = std::clamp(area.width() / 25, kMinMargin, kMaxMargin);
    // The buttons on the card's right, as much room on its left.
    gfx::Rect card = area;
    card.Inset(gfx::Insets::VH(margin, margin + kButtonSize + kButtonGap));
    frame_->SetBoundsRect(card);
    card_->SetBoundsRect(frame_->GetLocalBounds());
    buttons_->SetBoundsRect(gfx::Rect(card.right() + kButtonGap, card.y(),
                                      kButtonSize,
                                      buttons_->GetPreferredSize().height()));
  }

  void AddButton(const gfx::VectorIcon& icon,
                 std::string_view name,
                 base::RepeatingClosure pressed) {
    auto* button = buttons_->AddChildView(
        std::make_unique<views::ImageButton>(std::move(pressed)));
    button->SetImageModel(views::Button::STATE_NORMAL,
                          ui::ImageModel::FromVectorIcon(icon, ui::kColorIcon, 16));
    button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    button->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysBaseContainerElevated, kButtonSize / 2));
    button->SetTooltipText(L10n(name));
    views::InstallCircleHighlightPathGenerator(button);
    views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::ON);
  }

  raw_ptr<views::View> page_;
  gfx::Point origin_;
  base::RepeatingClosure close_;
  raw_ptr<views::View> dim_ = nullptr;
  raw_ptr<views::View> frame_ = nullptr;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::BoxLayoutView> buttons_ = nullptr;
  std::unique_ptr<views::ViewShadow> shadow_;
  base::ScopedObservation<views::View, views::ViewObserver> page_observation_{
      this};
};

BEGIN_METADATA(PeekView)
END_METADATA

// A window's peek, if one is open, and the delegate of the page in it: its links
// opening elsewhere and its keys go to its tab's window.
class PeekController : public TabStripModelObserver,
                       public content::WebContentsDelegate,
                       public views::ViewObserver {
 public:
  explicit PeekController(BrowserWindowInterface* browser) : browser_(browser) {
    TabStripModel* model = browser->GetTabStripModel();
    model->AddObserver(this);
    for (int i = 0; i < model->count(); ++i) {
      PeekTrigger::CreateForWebContents(model->GetWebContentsAt(i));
    }
  }
  PeekController(const PeekController&) = delete;
  PeekController& operator=(const PeekController&) = delete;
  ~PeekController() override { CloseNow(); }

  bool IsPeekedFrom(const content::WebContents* contents) const {
    return view_ && source_ == contents;
  }

  // The page in the peek, while it's open.
  content::WebContents* contents() const {
    return view_ ? contents_.get() : nullptr;
  }

  void Open(content::WebContents* source,
            const GURL& url,
            const gfx::Point& origin) {
    CloseNow();
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
    views::View* page =
        browser_view ? browser_view->multi_contents_view() : nullptr;
    tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(source);
    if (!page || !tab || !tab->IsActivated()) {
      return;
    }
    // In the page's container.
    Profile* profile = Profile::FromBrowserContext(source->GetBrowserContext());
    const std::string container = containers::GetContainerId(source);
    content::WebContents::CreateParams params(
        profile, container.empty()
                     ? nullptr
                     : content::SiteInstance::CreateForFixedStoragePartition(
                           profile, url,
                           containers::PartitionFor(profile, container)));
    contents_ = content::WebContents::Create(params);
    // A tab's helpers from the start, as for a page prerendered for a new tab:
    // the page loads as it would in a tab (e.g. extensions see its requests and
    // its scripts as a tab's), which it becomes if the user makes it one.
    PeekTabHelpers::Attach(contents_.get());
    contents_->SetDelegate(this);
    content::NavigationController::LoadURLParams load(url);
    load.transition_type = ui::PAGE_TRANSITION_LINK;
    load.referrer = content::Referrer(
        source->GetLastCommittedURL(),
        network::mojom::ReferrerPolicy::kStrictOriginWhenCrossOrigin);
    // A click on the page's link: the site gets what it would from the page
    // (not its SameSite=Strict cookies when it's another site's).
    load.initiator_origin =
        source->GetPrimaryMainFrame()->GetLastCommittedOrigin();
    load.is_renderer_initiated = true;
    load.has_user_gesture = true;
    contents_->GetController().LoadURLWithParams(load);
    source_ = source;

    PeekView::Actions actions;
    actions.close = base::BindRepeating(&PeekController::Close,
                                        weak_factory_.GetWeakPtr());
    actions.open_as_tab =
        base::BindRepeating(&PeekController::OpenAsTab,
                            weak_factory_.GetWeakPtr(), /*side_by_side=*/false);
    if (!tab->IsSplit()) {
      actions.open_side_by_side =
          base::BindRepeating(&PeekController::OpenAsTab,
                              weak_factory_.GetWeakPtr(), /*side_by_side=*/true);
    }
    view_ = browser_view->AddChildView(std::make_unique<PeekView>(
        contents_.get(), page, origin, std::move(actions)));
    view_observation_.Observe(view_);
    view_->AnimateIn();
    view_->web_view()->RequestFocus();
  }

  // Closes the peek, giving its page the focus back.
  void Close() {
    if (!view_) {
      return;
    }
    view_observation_.Reset();
    PeekView* view = std::exchange(view_, nullptr);
    FocusSource();
    // The card keeps its page until it's out of sight.
    view->AnimateOut(
        /*to_page=*/false,
        base::BindOnce(&Discard, std::make_unique<views::ViewTracker>(view),
                       std::move(contents_), base::WeakPtr<PeekController>(),
                       /*side_by_side=*/false));
  }

  // Makes the peeked page a tab of its own, after its page's (`side_by_side`:
  // shown with it, in a split).
  void OpenAsTab(bool side_by_side) {
    if (!view_ || !source_) {
      return;
    }
    view_observation_.Reset();
    PeekView* view = std::exchange(view_, nullptr);
    // Not left to the card, which goes.
    FocusSource();
    view->AnimateOut(
        /*to_page=*/!side_by_side,
        base::BindOnce(&Discard, std::make_unique<views::ViewTracker>(view),
                       std::move(contents_), weak_factory_.GetWeakPtr(),
                       side_by_side));
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    if (change.type() == TabStripModelChange::kInserted) {
      for (const auto& inserted : change.GetInsert()->contents) {
        PeekTrigger::CreateForWebContents(inserted.contents);
      }
    }
    if (change.type() == TabStripModelChange::kRemoved) {
      for (const auto& removed : change.GetRemove()->contents) {
        if (removed.contents == source_) {
          source_ = nullptr;
          CloseNow();
        }
      }
    }
    // Another tab shown: the peek was its page's.
    if (selection.active_tab_changed()) {
      Close();
    }
  }

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override {
    if (params.disposition == WindowOpenDisposition::CURRENT_TAB) {
      base::WeakPtr<content::NavigationHandle> handle =
          source->GetController().LoadURLWithParams(
              content::NavigationController::LoadURLParams(params));
      if (handle && navigation_handle_callback) {
        std::move(navigation_handle_callback).Run(*handle);
      }
      return source;
    }
    return source_ && source_->GetDelegate()
               ? source_->GetDelegate()->OpenURLFromTab(
                     source_, params, std::move(navigation_handle_callback))
               : nullptr;
  }

  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override {
    return source_ && source_->GetDelegate()
               ? source_->GetDelegate()->AddNewContents(
                     source_, std::move(new_contents), target_url, disposition,
                     window_features, user_gesture, was_blocked)
               : nullptr;
  }

  void CloseContents(content::WebContents* source) override { Close(); }

  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    // Escape and Command-W close the peek, not its page.
    const bool command = event.GetModifiers() & blink::WebInputEvent::kMetaKey;
    if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
        (event.windows_key_code == ui::VKEY_ESCAPE ||
         (command && event.windows_key_code == ui::VKEY_W))) {
      Close();
      return content::KeyboardEventProcessingResult::HANDLED;
    }
    return source_ && source_->GetDelegate()
               ? source_->GetDelegate()->PreHandleKeyboardEvent(source_, event)
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override {
    return source_ && source_->GetDelegate() &&
           source_->GetDelegate()->HandleKeyboardEvent(source_, event);
  }

  // Fullscreen is for the window's tabs: the page asking for it (a video's
  // button) becomes a tab at once, which the window then shows fullscreen.
  // Left unanswered, the request would keep the page from ever going fullscreen.
  bool CanEnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame) override {
    tabs::TabInterface* tab =
        source_ ? tabs::TabInterface::MaybeGetFromContents(source_) : nullptr;
    return view_ && tab && tab->IsActivated();
  }

  void EnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame,
      const blink::mojom::FullscreenOptions& options) override {
    CloseNow(/*keep_page=*/true);
    content::WebContents* page =
        MakeTab(std::move(contents_), /*side_by_side=*/false);
    if (!page) {
      return;
    }
    if (content::WebContentsDelegate* tab = page->GetDelegate();
        tab && tab->CanEnterFullscreenModeForTab(requesting_frame)) {
      tab->EnterFullscreenModeForTab(requesting_frame, options);
    } else {
      // Refused after all: the page is told, rather than left waiting.
      page->ExitFullscreen(/*will_cause_resize=*/false);
    }
  }

  // views::ViewObserver:
  void OnViewIsDeleting(views::View* observed_view) override {
    // Gone with its window.
    view_observation_.Reset();
    view_ = nullptr;
  }

 private:
  // Removes a peek's card once it's out of sight (`view`, unless gone with its
  // window), then makes its page a tab for `controller`, or lets it go.
  static void Discard(std::unique_ptr<views::ViewTracker> view,
                      std::unique_ptr<content::WebContents> contents,
                      base::WeakPtr<PeekController> controller,
                      bool side_by_side) {
    // Not from within the card's animation.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::unique_ptr<views::ViewTracker> view,
               std::unique_ptr<content::WebContents> contents,
               base::WeakPtr<PeekController> controller, bool side_by_side) {
              if (auto* card = static_cast<PeekView*>(view->view())) {
                card->web_view()->SetWebContents(nullptr);
                card->parent()->RemoveChildViewT(card);
              }
              if (controller && contents) {
                controller->MakeTab(std::move(contents), side_by_side);
              }
            },
            std::move(view), std::move(contents), std::move(controller),
            side_by_side));
  }

  // Gives the focus back to the peek's page, if it's still the tab shown.
  void FocusSource() {
    tabs::TabInterface* tab =
        source_ ? tabs::TabInterface::MaybeGetFromContents(source_) : nullptr;
    if (tab && tab->IsActivated()) {
      source_->Focus();
    }
  }

  // Removes the peek's card at once, and its page unless `keep_page`.
  void CloseNow(bool keep_page = false) {
    view_observation_.Reset();
    if (PeekView* view = std::exchange(view_, nullptr)) {
      view->web_view()->SetWebContents(nullptr);
      view->parent()->RemoveChildViewT(view);
    }
    if (!keep_page) {
      contents_.reset();
    }
  }

  // Returns the page, now a tab, or none if its page's tab is gone (and the
  // page with it).
  content::WebContents* MakeTab(std::unique_ptr<content::WebContents> contents,
                                bool side_by_side) {
    TabStripModel* model = browser_->GetTabStripModel();
    const int index =
        source_ ? model->GetIndexOfWebContents(source_) : TabStripModel::kNoTab;
    if (index == TabStripModel::kNoTab) {
      return nullptr;
    }
    source_ = nullptr;
    contents->SetDelegate(nullptr);
    content::WebContents* page = contents.get();
    const int inserted = model->InsertWebContentsAt(
        index + 1, std::move(contents),
        side_by_side ? AddTabTypes::ADD_NONE : AddTabTypes::ADD_ACTIVE,
        model->GetTabGroupForTab(index));
    if (side_by_side) {
      model->ExecuteAddToNewSplitCommand(inserted,
                                         split_tabs::SplitTabLayout::kSideBySide);
      // The page the user was on.
      page->Focus();
    }
    return page;
  }

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<content::WebContents> source_ = nullptr;
  std::unique_ptr<content::WebContents> contents_;
  raw_ptr<PeekView> view_ = nullptr;
  base::ScopedObservation<views::View, views::ViewObserver> view_observation_{
      this};
  base::WeakPtrFactory<PeekController> weak_factory_{this};
};

// A PeekController for each window with tabs.
class Controllers : public BrowserCollectionObserver {
 public:
  static Controllers& Get() {
    static base::NoDestructor<Controllers> controllers;
    return *controllers;
  }

  void Start() {
    if (started_) {
      return;
    }
    started_ = true;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [this](BrowserWindowInterface* browser) {
          OnBrowserCreated(browser);
          return true;
        });
    observation_.Observe(GlobalBrowserCollection::GetInstance());
  }

  PeekController* For(BrowserWindowInterface* browser) {
    auto it = controllers_.find(browser);
    return it != controllers_.end() ? it->second.get() : nullptr;
  }

  // The page in a peek whose tab id is `tab_id`, if one's open.
  content::WebContents* FindPage(int tab_id) {
    for (const auto& [browser, controller] : controllers_) {
      content::WebContents* contents = controller->contents();
      if (contents &&
          sessions::SessionTabHelper::IdForTab(contents).id() == tab_id) {
        return contents;
      }
    }
    return nullptr;
  }

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    if (browser->GetType() == BrowserWindowInterface::Type::TYPE_NORMAL &&
        browser->GetTabStripModel() && !controllers_.contains(browser)) {
      controllers_[browser] = std::make_unique<PeekController>(browser);
    }
  }

  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    controllers_.erase(browser);
  }

 private:
  bool started_ = false;
  std::map<BrowserWindowInterface*, std::unique_ptr<PeekController>>
      controllers_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation_{this};
};

PeekController* ControllerFor(content::WebContents* contents) {
  tabs::TabInterface* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  return tab && tab->GetBrowserWindowInterface()
             ? Controllers::Get().For(tab->GetBrowserWindowInterface())
             : nullptr;
}

void PeekTrigger::Peek(const GURL& url) {
  if (PeekController* controller = ControllerFor(web_contents())) {
    controller->Open(web_contents(), url, last_mouse_point_);
  }
}

}  // namespace

void Init() {
  Controllers::Get().Start();
}

void OpenPeekAsTab(BrowserWindowInterface* browser) {
  if (PeekController* controller = Controllers::Get().For(browser)) {
    controller->OpenAsTab(/*side_by_side=*/false);
  }
}

void SetHoveredLink(content::WebContents* source, const GURL& url) {
  if (PeekTrigger* trigger = PeekTrigger::FromWebContents(source)) {
    trigger->set_hovered_link(url);
  }
}

bool PeekAtHoveredLink(content::WebContents* source) {
  PeekTrigger* trigger = PeekTrigger::FromWebContents(source);
  PeekController* controller = ControllerFor(source);
  if (!trigger || !controller || !CanPeekAt(trigger->hovered_link())) {
    return false;
  }
  trigger->SwallowUntilMouseUp();
  controller->Open(source, trigger->hovered_link(),
                   trigger->last_mouse_point());
  return true;
}

bool IsPeekedFrom(content::WebContents* source) {
  PeekController* controller = ControllerFor(source);
  return controller && controller->IsPeekedFrom(source);
}

content::WebContents* FindPeekByTabId(int tab_id) {
  return Controllers::Get().FindPage(tab_id);
}

}  // namespace oblada::peek
