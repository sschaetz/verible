// Copyright 2017-2020 The Verible Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common/util/init_command_line.h"

#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/flags/usage_config.h"
#include "absl/time/time.h"
#include "common/util/generated_verible_build_version.h"
#include "common/util/logging.h"

namespace verible {

static std::string GetBuildVersion() {
  std::string result;
  // Build a version string with as much as possible info.
#ifdef VERIBLE_GIT_DESCRIBE
  result.append(VERIBLE_GIT_DESCRIBE).append("\n");
#endif
#ifdef VERIBLE_GIT_DATE
  result.append("Commit\t").append(VERIBLE_GIT_DATE).append("\n");
#endif
#ifdef VERIBLE_BUILD_TIMESTAMP
  result.append("Built\t")
      .append(absl::FormatTime("%Y-%m-%dT%H:%M:%SZ",
                               absl::FromTimeT(VERIBLE_BUILD_TIMESTAMP),
                               absl::UTCTimeZone()))
      .append("\n");
#endif
  return result;
}

// We might want to have argc edited in the future, hence non-const param.
std::vector<absl::string_view> InitCommandLine(
    absl::string_view usage,
    int* argc,  // NOLINT(readability-non-const-parameter)
    char*** argv) {
  absl::FlagsUsageConfig usage_config;
  usage_config.version_string = GetBuildVersion;
  absl::SetFlagsUsageConfig(usage_config);
  absl::SetProgramUsageMessage(usage);  // copies usage string
  google::InitGoogleLogging(**argv);

  // Print stacktrace on issue, but not if --config=asan
  // which comes with its own stacktrace handling.
#if !defined(__SANITIZE_ADDRESS__)
  google::InstallFailureSignalHandler();
#endif

  const auto positional_parameters = absl::ParseCommandLine(*argc, *argv);
  return {positional_parameters.cbegin(), positional_parameters.cend()};
}

}  // namespace verible
