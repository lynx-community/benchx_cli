#include "cli/benchx/tasm_platform_invoker_dummy.h"

#include <memory>

#include "base/include/fml/task_runner.h"
#include "runtime/lepusng/quick_context.h"

namespace lynx {
namespace shell {

void TasmPlatformInvokerDummy::OnPageConfigDecoded(
    const std::shared_ptr<tasm::PageConfig>& config) {}

void TasmPlatformInvokerDummy::OnRunPipelineFinished() {}

std::string TasmPlatformInvokerDummy::TranslateResourceForTheme(
    const std::string& res_id, const std::string& theme_key) {
  return std::string();
}

lepus::Value TasmPlatformInvokerDummy::TriggerLepusMethod(
    const std::string& js_method_name, const lepus::Value& args) {
  return lepus::Value();
}

void TasmPlatformInvokerDummy::TriggerLepusMethodAsync(
    const std::string& method_name, const lepus::Value& args) {}

void TasmPlatformInvokerDummy::GetI18nResource(
    const std::string& channel, const std::string& fallback_url) {}

}  // namespace shell
}  // namespace lynx
