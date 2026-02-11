#include "renderer/ui_wrapper/painting/empty/painting_context_implementation.h"
#include "shell/lynx_shell.h"

class PaintingContextPlatformImpl
    : public lynx::tasm::PaintingContextPlatformImpl {
 public:
  virtual void InvokeUIMethod(int32_t view_id, const std::string& method,
                              fml::RefPtr<lynx::tasm::PropBundle> prop_bundle,
                              int32_t callback_id) override;

  void SetShellPtr(lynx::shell::LynxShell* shell);

 private:
  lynx::shell::LynxShell* shell_;
};
