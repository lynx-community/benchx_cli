#include "cli/benchx/resource_loader.h"

#include <cstdint>
#include <vector>

#include "js_libraries/lynx_core_dev.h"

void ResourceLoaderDummy::LoadResourceInternal(
    const lynx::pub::LynxResourceRequest& request,
    lynx::base::MoveOnlyClosure<void, lynx::pub::LynxResourceResponse&>
        callback) {
  if (request.type == lynx::pub::LynxResourceType::kAssets) {
    if (request.url == "assets://lynx_core.js" ||
        request.url == "assets://lynx_core_dev.js") {
      lynx::pub::LynxResourceResponse response{
          .data = std::vector<uint8_t>(
              lynx_core_dev_js, lynx_core_dev_js + lynx_core_dev_js_len)};
      callback(response);
      return;
    }
  }

  lynx::pub::LynxResourceResponse response{};
  callback(response);
}
