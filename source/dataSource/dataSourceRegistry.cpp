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

#include "dataSourceRegistry.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

bool DataSourceRegistryImp::getFileTypesDesc(const std::string& fileType, FileTypesDesc& desc) const
{
    for (auto& ftype : _fileTypesDesc)
    {
        if (ftype.extensions.find(fileType) != ftype.extensions.end())
        {
            desc = ftype;
            return true;
        }
    }

    return false;
}

bool DataSourceRegistryImp::isSupportedFileType(const std::string& fileType) const
{
    FileTypesDesc desc;
    return getFileTypesDesc(fileType, desc);
}

bool DataSourceRegistryImp::registerFileTypes(const FileTypesDesc& desc)
{
    for (auto& ext : desc.extensions)
    {
        if (isSupportedFileType(ext))
        {
            return false; // File type is already registered.
        }
    }

    _fileTypesDesc.push_back(desc);
    return true;
}

DataSourceRegistry& DataSourceRegistry::registry()
{
    static DataSourceRegistryImp theRegistry;
    return theRegistry;
}

} // namespace hvt
