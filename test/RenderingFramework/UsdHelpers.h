//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

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
        if (IsCodingError(err))
        {
            std::cerr << "[" << _ident << "]: Error issued : " << err.GetSourceFileName() << " - "
                << err.GetSourceFunction() << "(" << err.GetSourceLineNumber()
                << "): \"" << err.GetCommentary() << "\"\n";
        }
    }

    void IssueFatalError(const pxr::TfCallContext& context, const std::string& msg) override
    {
        std::cerr << "[" << _ident << "]: Fatal error issued : " << context.GetFile() << " - "
                  << context.GetFunction() << "(" << context.GetLine() << "): \"" << msg << "\"\n";
    }

    void IssueStatus(const pxr::TfStatus& status) override
    {
        std::cout << "[" << _ident << "]: Status issued : " << status.GetCommentary() << "\n";
    }

    void IssueWarning(const pxr::TfWarning& warning) override
    {
        std::cout << "[" << _ident << "]: Warning issued : " << warning.GetCommentary() << "\n";
    }

private:
    std::string _ident;
};
