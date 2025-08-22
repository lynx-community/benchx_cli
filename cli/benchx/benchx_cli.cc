#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "argparse/argparse.hpp"
#include "base/include/log/logging.h"
#include "base/threading/task_runner_manufactor.h"
#include "base/trace/native/trace_controller.h"
#include "cli/benchx/painting_context_platform_impl.h"
#include "cli/benchx/resource_loader.h"
#include "cli/benchx/tasm_platform_invoker_dummy.h"
#include "core/shell/lynx_shell_builder.h"
#include "renderer/ui_wrapper/layout/empty/layout_context_empty_implementation.h"
#include "renderer/utils/lynx_env.h"
#include "shell/lynx_runtime_proxy_impl.h"
#include "shell/lynx_shell.h"
#include "shell/module_delegate_impl.h"

static std::ofstream g_logfile;
static std::mutex g_logfile_mutex;

int main(int argc, char** argv) {
  argparse::ArgumentParser program("benchx_cli");
  program.add_argument("--logfile")
      .action([](const std::string& value) {
        g_logfile = std::ofstream(value, std::ios::app);
      })
      .help("The path to file Lynx Native will log to");

  lynx::base::logging::EnableLogOutputByPlatform();
  lynx::base::logging::InitLynxLogging(
      nullptr,
      [](lynx::base::logging::LogMessage* msg, const char* tag) {
        if (msg->source() == lynx::base::logging::LOG_SOURCE_NATIVE) {
          if (g_logfile) {
            std::lock_guard<std::mutex> lock(g_logfile_mutex);
            g_logfile << msg->stream() << std::endl;
          }
        } else {
          std::cout << msg->stream().str().substr(msg->messageStart());
        }
      },
      false);

  std::shared_ptr<int> g_session_id;
  program.add_argument("-o", "--out-trace-file")
      .action([&g_session_id](const std::string& out_trace_file) {
        auto instance = lynx::trace::TraceController::Instance();
        auto trace_config = std::make_shared<lynx::trace::TraceConfig>();
        trace_config->buffer_size = 65536;
        trace_config->file_path = out_trace_file;
        trace_config->included_categories = {"*"};
        trace_config->excluded_categories = {"*"};
        auto session_id = instance->StartTracing(trace_config);
        g_session_id =
            std::shared_ptr<int>(new int(session_id), [=](int* session_id) {
              instance->StopTracing(*session_id);
              delete session_id;
              // give perfetto some time to flush buffers
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
            });
      })
      .help("The path to the output trace file");

  argparse::ArgumentParser run_command("run");
  run_command.add_argument("bundle")
      .help("The path to '.lynx.bundle' file to be benchmarked")
      .required();
  run_command.add_argument("--repeat")
      .help("The number of times to repeat the benchmark")
      .default_value(1)
      .scan<'i', int32_t>();
  run_command.add_argument("--wait-for-id")
      .help("Wait for ui with specified id");

  program.add_subparser(run_command);
  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  if (program.is_subcommand_used(run_command)) {
    auto filename = run_command.get<std::string>("bundle");
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
      std::cerr << "Error: Could not open file " << filename << std::endl;
      return 1;
    }

    auto source = std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());

    auto repeat = run_command.get<int32_t>("--repeat");

    auto wait_for_id = run_command.present("--wait-for-id");

    auto run = [&]() {
      auto config = lynx::tasm::LynxEnvConfig(1080, 2060, 1.0f, 1.0f);
      // Fake enviornment which is never used.
      lynx::tasm::Config::InitializeVersion("Benchx");
      lynx::tasm::LynxEnv::GetInstance().SetBoolLocalEnv("enable_quickjs_cache",
                                                         false);
      auto invoker = std::make_unique<lynx::shell::TasmPlatformInvokerDummy>();
      auto* invoker_ptr = invoker.get();
      auto painting_context = std::make_unique<PaintingContextPlatformImpl>();
      auto* painting_context_ptr = painting_context.get();

      auto shell =
          lynx::shell::LynxShellBuilder()
              // .SetNativeFacade(std::move(native_facade))
              .SetUseInvokeUIMethodFunction(true)
              .SetPaintingContextPlatformImpl(std::move(painting_context))
              .SetLynxEnvConfig(config)
              .SetEnableElementManagerVsyncMonitor(true)
              .SetEnableNewAnimator(
                  /* settings_.enable_new_animator */ true)
              .SetEnableNativeList(/* settings_.enable_native_list */ true)
              // .SetLazyBundleLoader(loader)
              // .SetEnablePreUpdateData(enable_pre_update_data_)
              .SetLayoutContextPlatformImpl(
                  std::make_unique<lynx::tasm::PlatformImplEmptyImpl>())
              .SetStrategy(lynx::base::ThreadStrategyForRendering::ALL_ON_UI)
              // .SetPropBundleCreator(ui_delegate_->CreatePropBundleCreator())
              .SetEngineActor([](auto& actor) {})
              .SetShellOption(lynx::shell::ShellOption{
                  .enable_js_ = true,
              })
              .SetTasmPlatformInvoker(std::move(invoker))
              // .SetPerformanceControllerPlatform(std::move(perf_controller_ptr_))
              // .SetPerfControllerActor()
              .build();
      invoker_ptr->SetUITaskRunner(shell->GetRunners()->GetUITaskRunner());
      invoker_ptr->SetTASMTaskRunner(shell->GetRunners()->GetTASMTaskRunner());
      invoker_ptr->SetTASM(shell->GetTasm());
      painting_context_ptr->SetShellPtr(shell);

      shell->UpdateViewport(config.ScreenWidth(), 1, config.ScreenHeight(), 1);

      std::shared_ptr<lynx::shell::LynxRuntimeProxy> runtime_proxy;
      auto module_manager = std::make_shared<lynx::piper::LynxModuleManager>();
      module_manager->SetModuleFactory(nullptr);
      auto on_runtime_actor_created = [&](auto& actor) {
        auto module_delegate =
            std::make_shared<lynx::shell::ModuleDelegateImpl>(
                shell->GetRuntimeActor(), shell->GetFacadeActor());
        module_manager->initBindingPtr(module_manager, module_delegate);
        runtime_proxy =
            std::make_shared<lynx::shell::LynxRuntimeProxyImpl>(actor);
        module_manager->runtime_proxy = runtime_proxy;
      };

      auto runtime_flags =
          lynx::runtime::CalcRuntimeFlags(false, true, false, false);
      shell->InitRuntime("1", std::make_shared<ResourceLoaderDummy>(),
                         module_manager, std::move(on_runtime_actor_created),
                         std::vector<std::string>{}, runtime_flags,
                         std::string{});

      // if (settings.global_props) {
      //   shell->UpdateGlobalProps(*settings.global_props);
      // }

      auto pipeline_options = std::make_shared<lynx::tasm::PipelineOptions>();
      shell->LoadTemplate(filename, source, pipeline_options, nullptr);

      return shell;
    };

    lynx::base::UIThread::Init();
    auto runner = lynx::base::UIThread::GetRunner();

    std::thread([&]() {
      for (int i = 0; i < repeat; ++i) {
        lynx::shell::LynxShell* shell;
        runner->PostSyncTask([&]() { shell = run(); });

        if (wait_for_id.has_value() && !wait_for_id->empty()) {
          // wait for id selector
          while (true) {
            bool found = false;
            runner->PostSyncTask([&]() {
              auto root = shell->GetTasm()
                              ->page_proxy()
                              ->element_manager()
                              ->catalyzer()
                              ->get_root();

              std::function<void(lynx::tasm::Element * node)>
                  search_id_selector = [&](lynx::tasm::Element* node) {
                    if (node->data_model()->idSelector() == *wait_for_id) {
                      found = true;
                    }
                    for (auto child : node->GetChildren()) {
                      search_id_selector(child);
                    }
                  };

              search_id_selector(root);
            });
            if (found) {
              break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
        }

        runner->PostSyncTask([&]() { delete shell; });
      }

      runner->PostSyncTask([&]() { runner->GetLoop()->Terminate(); });
    }).detach();

    runner->GetLoop()->Run();
  }

  return 0;
}
