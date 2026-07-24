#include <cli/benchx/painting_context_platform_impl.h>

#include <cstdint>

#include "renderer/ui_wrapper/common/testing/prop_bundle_mock.h"

void PaintingContextPlatformImpl::InvokeUIMethod(
    int32_t view_id, const std::string& method,
    fml::RefPtr<lynx::tasm::PropBundle> prop_bundle, int32_t callback_id) {
  enum LynxUIMethodErrorCode {
    kUIMethodSuccess = 0,
    kUIMethodUnknown,
    kUIMethodNodeNotFound,
    kUIMethodMethodNotFound,
    kUIMethodParamInvalid,
    kUIMethodSelectorNotSupported,
    kUIMethodNoUiForNode,
    kUIMethodInvalidStateError,
    kUIMethodOperationError
  };

  auto& args = (static_cast<lynx::tasm::PropBundleMock*>(prop_bundle.get()))
                   ->GetPropsMap();

  if (method == "scrollToPosition") {
    if (shell_->GetListNode(view_id) != nullptr) {
      int32_t position = 0;
      float offset = 0;
      int32_t align_to = 0;
      bool smooth = false;
      for (const auto& [key, value] : args) {
        if (key == "position") {
          position = static_cast<int32_t>(value.Number());
        } else if (key == "offset") {
          offset = value.Double();
        } else if (key == "alignTo") {
          auto s = value.StringView();
          if (s == "top") {
            align_to = 0;
          } else if (s == "middle") {
            align_to = 1;
          } else if (s == "bottom") {
            align_to = 2;
          }
        } else if (key == "smooth") {
          smooth = value.Bool();
        }
      }
      shell_->ScrollToPosition(view_id, position, offset, align_to, smooth);
      shell_->GetTasm()->GetDelegate().CallJSApiCallbackWithValue(
          callback_id, []() {
            auto ret = lynx::lepus::Dictionary::Create();
            ret->SetValue("code", LynxUIMethodErrorCode::kUIMethodSuccess);
            ret->SetValue("data", lynx::lepus::Dictionary::Create());
            return lynx::lepus::Value(ret);
          }());
      return;
    }
  }

  shell_->GetTasm()->GetDelegate().CallJSApiCallbackWithValue(
      callback_id, []() {
        auto ret = lynx::lepus::Dictionary::Create();
        ret->SetValue("code", LynxUIMethodErrorCode::kUIMethodMethodNotFound);
        ret->SetValue("data", lynx::lepus::Dictionary::Create());
        return lynx::lepus::Value(ret);
      }());
}

void PaintingContextPlatformImpl::SetShellPtr(lynx::shell::LynxShell* shell) {
  shell_ = shell;
}
