#ifndef OBLADA_RENDERER_BARRIER_BARRIER_AGENT_H_
#define OBLADA_RENDERER_BARRIER_BARRIER_AGENT_H_

#include <map>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/containers/flat_map.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "oblada/common/barrier/barrier.mojom.h"
#include "v8/include/v8-forward.h"

namespace gin {
class Arguments;
}  // namespace gin

// The Barrier in a frame (oblada/browser/barrier/barrier.h), for the documents
// it commits, as the browser says before each commit
// (oblada.mojom.BarrierAgent):
// - the filter lists' scriptlets run in the page before its own scripts;
// - the elements their rules select are hidden by a user style sheet, which
//   pages can't override;
// - procedural filters and generic rules run in an isolated world of their
//   own (resources/cosmetic_filters.js), which asks the browser for the rules
//   the page's classes and ids call for;
// - the element picker (resources/element_picker.js), on demand;
// - fingerprinting protection, which Blink is given for each document (and
//   which documents without settings of their own, about:blank's, have from
//   their parent).
namespace oblada::barrier {

class BarrierAgent : public content::RenderFrameObserver,
                     public content::RenderFrameObserverTracker<BarrierAgent>,
                     public oblada::mojom::BarrierAgent {
 public:
  // chrome_content_renderer_client.cc, as frames are created.
  static void Create(content::RenderFrame* render_frame);

  // chrome_content_renderer_client.cc, once scripts may run in the frame's
  // new document (its document element is there): the scriptlets and the
  // cosmetic filters.
  static void RunScriptsAtDocumentStart(content::RenderFrame* render_frame);

  // What `render_frame`'s document got (fingerprinting protection); null if
  // nothing.
  static const oblada::mojom::BarrierPageSettings* SettingsFor(
      content::RenderFrame* render_frame);

  BarrierAgent(const BarrierAgent&) = delete;
  BarrierAgent& operator=(const BarrierAgent&) = delete;

  // oblada::mojom::BarrierAgent:
  void SetPageSettings(oblada::mojom::BarrierPageSettingsPtr settings) override;
  void StartElementPicker(
      const base::flat_map<std::string, std::string>& strings) override;

  // content::RenderFrameObserver:
  void DidCreateNewDocument() override;
  void DidCreateScriptContext(v8::Local<v8::Context> context,
                              int32_t world_id) override;
  void OnDestruct() override;

 private:
  explicit BarrierAgent(content::RenderFrame* render_frame);
  ~BarrierAgent() override;

  void Bind(
      mojo::PendingAssociatedReceiver<oblada::mojom::BarrierAgent> receiver);

  void RunAtDocumentStart();
  // Gives Blink the document's fingerprinting protection.
  void ApplyFarbling();
  // Hides what `selectors` select, with a user style sheet.
  void Hide(const std::vector<std::string>& selectors);
  // Runs `script` in the cosmetic filters' world, with `obladaBarrier`.
  void RunInWorld(const std::string& script);
  // Runs `script` in `world_id`.
  void Run(int32_t world_id, const std::string& script);
  // `obladaBarrier`'s functions.
  void HiddenSelectors(gin::Arguments* args);
  void HideFromScript(gin::Arguments* args);
  void SameSite(gin::Arguments* args);
  void PickerStrings(gin::Arguments* args);
  void AddFilter(gin::Arguments* args);
  void OnHiddenSelectors(int document,
                         v8::Global<v8::Function> callback,
                         const std::vector<std::string>& selectors);
  oblada::mojom::BarrierHost* host();

  mojo::AssociatedReceiver<oblada::mojom::BarrierAgent> receiver_{this};
  mojo::Remote<oblada::mojom::BarrierHost> host_;
  // For the next document, and for this one.
  oblada::mojom::BarrierPageSettingsPtr pending_;
  oblada::mojom::BarrierPageSettingsPtr settings_;
  // Which document this is (callbacks from the browser for an earlier one go
  // nowhere).
  int document_ = 0;
  // RunAtDocumentStart() ran for this document.
  bool started_ = false;
  base::flat_map<std::string, std::string> picker_strings_;
  base::WeakPtrFactory<BarrierAgent> weak_factory_{this};
};

}  // namespace oblada::barrier

#endif  // OBLADA_RENDERER_BARRIER_BARRIER_AGENT_H_
