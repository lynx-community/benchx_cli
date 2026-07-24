#ifndef CLI_BENCHX_RESOURCE_LOADER
#define CLI_BENCHX_RESOURCE_LOADER

#include "public/lynx_resource_loader.h"

class ResourceLoaderDummy : public lynx::pub::LynxResourceLoader {
 public:
  ResourceLoaderDummy() = default;
  ~ResourceLoaderDummy() override = default;

  void LoadResourceInternal(
      const lynx::pub::LynxResourceRequest& request,
      lynx::base::MoveOnlyClosure<void, lynx::pub::LynxResourceResponse&>
          callback) override;
};

#endif  // CLI_BENCHX_RESOURCE_LOADER
