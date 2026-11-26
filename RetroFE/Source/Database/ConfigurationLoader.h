#pragma once
#include <functional>
#include <string>
#include <vector>
class Configuration;
namespace ConfigurationLoader {
bool load(Configuration& config, const std::vector<std::string>& cliSettings = {},
          const std::function<bool()>& afterSettings = {}, bool startLogging = true);
}
