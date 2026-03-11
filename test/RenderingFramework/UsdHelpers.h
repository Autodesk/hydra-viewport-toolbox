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

/// Trap USD errors
class DiagnosticDelegate : public pxr::TfDiagnosticMgr::Delegate
{
public:
    DiagnosticDelegate(const std::string& ident) : _ident(ident) {}
    virtual ~DiagnosticDelegate() {}

    static bool IsCodingError(const pxr::TfError& err)
    {
        pxr::TfEnum code = err.GetErrorCode();
        return (code == pxr::TF_DIAGNOSTIC_CODING_ERROR_TYPE) ||
            (code == pxr::TF_DIAGNOSTIC_FATAL_CODING_ERROR_TYPE);
    }

    void IssueError(const pxr::TfError& err) override
    {
        if (err.GetQuiet())
            return;

        if (IsCodingError(err))
        {
            std::cerr << "[" << _ident << "]: Error issued : " << err.GetSourceFileName() << " - "
                      << err.GetSourceFunction() << "(" << err.GetSourceLineNumber() << "): \""
                      << err.GetCommentary() << "\"\n";
        }
    }

    void IssueFatalError(const pxr::TfCallContext& context, const std::string& msg) override
    {
        std::cerr << "[" << _ident << "]: Fatal error issued : " << context.GetFile() << " - "
                  << context.GetFunction() << "(" << context.GetLine() << "): \"" << msg << "\"\n";
    }

    void IssueStatus(const pxr::TfStatus& status) override
    {
        if (status.GetQuiet())
            return;

        std::cout << "[" << _ident << "]: Status issued : " << status.GetCommentary() << "\n";
    }

    void IssueWarning(const pxr::TfWarning& warning) override
    {
        if (warning.GetQuiet())
            return;

        std::cout << "[" << _ident << "]: Warning issued : " << warning.GetCommentary() << "\n";
    }

private:
    std::string _ident;
};

/// RAII guard that temporarily silences all USD diagnostic output.
/// The constructor calls TfDiagnosticMgr::SetQuiet(true); the
/// destructor restores it to false.
class ScopedDiagnosticQuiet
{
public:
    ScopedDiagnosticQuiet() { pxr::TfDiagnosticMgr::GetInstance().SetQuiet(true); }

    ~ScopedDiagnosticQuiet() { pxr::TfDiagnosticMgr::GetInstance().SetQuiet(false); }

    ScopedDiagnosticQuiet(const ScopedDiagnosticQuiet&)            = delete;
    ScopedDiagnosticQuiet& operator=(const ScopedDiagnosticQuiet&) = delete;
};
