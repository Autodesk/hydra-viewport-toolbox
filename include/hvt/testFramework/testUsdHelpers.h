// Copyright 2025 Autodesk, Inc.
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

#include <hvt/api.h>

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <pxr/base/tf/diagnosticMgr.h>

#if __clang__
#pragma clang diagnostic pop
#endif

#include <iostream>

namespace HVT_NS
{

namespace TestFramework
{

/// Trap USD errors.
class DiagnosticDelegate : public pxr::TfDiagnosticMgr::Delegate
{
public:
    DiagnosticDelegate(std::string const& tag, std::string const& ident)
    {
        _prefix = "[" + ident + "]: " + (tag.empty() ? "" : "'" + tag + "' ");
    }
    virtual ~DiagnosticDelegate() {}

    static bool IsCodingError(pxr::TfError const& err)
    {
        const pxr::TfEnum code = err.GetErrorCode();
        return (code == pxr::TF_DIAGNOSTIC_CODING_ERROR_TYPE) ||
            (code == pxr::TF_DIAGNOSTIC_FATAL_CODING_ERROR_TYPE);
    }

    void IssueError(const pxr::TfError& err) override
    {
        if (IsCodingError(err))
        {
            std::cerr << _prefix << "Error issued : " << err.GetSourceFileName() << " - "
                      << err.GetSourceFunction() << "(" << err.GetSourceLineNumber() << "): \""
                      << err.GetCommentary() << "\"\n";
        }
    }

    void IssueFatalError(const pxr::TfCallContext& context, const std::string& msg) override
    {
        std::cerr << _prefix << "Fatal error issued : " << context.GetFile() << " - "
                  << context.GetFunction() << "(" << context.GetLine() << "): \"" << msg << "\"\n";
    }

    void IssueStatus(const pxr::TfStatus& status) override
    {
        std::cout << _prefix << "Status issued : " << status.GetCommentary() << "\n";
    }

    void IssueWarning(const pxr::TfWarning& warning) override
    {
        std::cout << _prefix << "Warning issued : " << warning.GetCommentary() << "\n";
    }

private:
    std::string _prefix {};
};

} // namespace TestFramework

} // namespace HVT_NS
