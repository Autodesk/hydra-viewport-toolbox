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

#include <hvt/dataSource/dataSource.h>

#include <string>
#include <vector>

namespace HVT_NS
{

class DataSourceRegistryImp : public DataSourceRegistry
{
public:
    // Overrides from DataSourceRegistry.
    size_t fileTypesDescCount() const override { return _fileTypesDesc.size(); }
    const FileTypesDesc& getFileTypesDesc(size_t index) const override
    {
        return _fileTypesDesc[index];
    }
    bool getFileTypesDesc(const std::string& fileType, FileTypesDesc& desc) const override;
    bool isSupportedFileType(const std::string& fileType) const override;
    bool registerFileTypes(const FileTypesDesc& desc) override;

private:
    std::vector<FileTypesDesc> _fileTypesDesc;
};

} // namespace HVT_NS