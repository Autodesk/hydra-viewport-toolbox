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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/dataSource.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <ostream>

namespace HVT_NS
{

inline void PrintTokenVector(std::ostream& out, PXR_NS::TfTokenVector const& tokens)
{
    if (tokens.empty())
    {
        return;
    }

    out << "[";
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (i > 0)
        {
            out << ", ";
        }
        out << tokens[i];
    }
    out << "]";
}

inline void PrintSampledValue(std::ostream& out, PXR_NS::HdSampledDataSourceHandle const& sampled)
{
    PXR_NS::VtValue value = sampled->GetValue(0.0f);
    if (value.IsHolding<PXR_NS::TfTokenVector>())
    {
        PrintTokenVector(out, value.UncheckedGet<PXR_NS::TfTokenVector>());
    }
    else
    {
        out << value;
    }
}

inline void PrintDataSource(std::ostream& out, PXR_NS::HdDataSourceBaseHandle const& ds)
{
    if (!ds)
    {
        return;
    }

    if (auto container = PXR_NS::HdContainerDataSource::Cast(ds))
    {
        for (auto const& name : container->GetNames())
        {
            auto child = container->Get(name);
            if (!child)
                continue;

            if (PXR_NS::HdContainerDataSource::Cast(child))
            {
                out << "{ " << name << ":\n";
                PrintDataSource(out, child);
                out << "}\n";
            }
            else if (auto sampled = PXR_NS::HdSampledDataSource::Cast(child))
            {
                out << "{ " << name << ":\n";
                PrintSampledValue(out, sampled);
                out << "}\n";
            }
        }
    }
    else if (auto sampled = PXR_NS::HdSampledDataSource::Cast(ds))
    {
        PrintSampledValue(out, sampled);
        out << "\n";
    }
}

} // namespace HVT_NS
