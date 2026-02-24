// Copyright 2026 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <pxr/base/tf/getenv.h>
#include <pxr/base/trace/collector.h>
#include <pxr/base/trace/reporter.h>

#include <fstream>

namespace RenderingUtils
{

/// RAII helper to collect traces when PXR_ENABLE_GLOBAL_TRACE environment variable is set.
/// On destruction, writes a Chrome-compatible trace report to "report.json".
struct CollectTraces
{
    CollectTraces() : _enabled(PXR_NS::TfGetenvBool("PXR_ENABLE_GLOBAL_TRACE", false))
    {
        PXR_NS::TraceCollector::GetInstance().SetEnabled(_enabled);
    }
    ~CollectTraces() 
    { 
        PXR_NS::TraceCollector::GetInstance().SetEnabled(false); 
        if (_enabled) 
        {
            std::ofstream chromeReportFile("report.json");
            PXR_NS::TraceReporter::GetGlobalReporter()->ReportChromeTracing(chromeReportFile);
        }
    }

private:
    const bool _enabled { false };
};

} // namespace RenderingUtils
