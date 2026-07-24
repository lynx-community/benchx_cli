#ifndef CLI_BENCHX_TASM_PLATFORM_INVOKER_DUMMY_H
#define CLI_BENCHX_TASM_PLATFORM_INVOKER_DUMMY_H

#include "base/include/fml/task_runner.h"
#include "renderer/template_assembler.h"
#include "shell/tasm_platform_invoker.h"

namespace lynx {
namespace shell {

class TasmPlatformInvokerDummy : public TasmPlatformInvoker {
 public:
  explicit TasmPlatformInvokerDummy() = default;
  ~TasmPlatformInvokerDummy() override = default;

  void OnPageConfigDecoded(
      const std::shared_ptr<tasm::PageConfig>& config) override;

  void OnRunPipelineFinished() override;

  std::string TranslateResourceForTheme(const std::string& res_id,
                                        const std::string& theme_key) override;

  lepus::Value TriggerLepusMethod(const std::string& method_name,
                                  const lepus::Value& args) override;

  void TriggerLepusMethodAsync(const std::string& method_name,
                               const lepus::Value& args) override;

  void GetI18nResource(const std::string& channel,
                       const std::string& fallback_url) override;

  void SetUITaskRunner(const fml::RefPtr<fml::TaskRunner>& task_runner) {
    ui_task_runner_ = task_runner;
  };

  void SetTASMTaskRunner(const fml::RefPtr<fml::TaskRunner>& task_runner) {
    tasm_task_runner_ = task_runner;
  };

  void SetTASM(tasm::TemplateAssembler* tasm) { tasm_ = tasm; }

 private:
  fml::RefPtr<fml::TaskRunner> ui_task_runner_;
  fml::RefPtr<fml::TaskRunner> tasm_task_runner_;
  tasm::TemplateAssembler* tasm_;
};

}  // namespace shell
}  // namespace lynx

#endif  // CLI_BENCHX_TASM_PLATFORM_INVOKER_DUMMY_H
