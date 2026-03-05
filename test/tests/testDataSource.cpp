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

#include <hvt/dataSource/dataSource.h>

#include <pxr/pxr.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ===========================================================================
// SceneDataSource -- base class default behaviors
// ===========================================================================

namespace
{

class ConcreteDataSource : public hvt::SceneDataSource
{
};

} // anonymous namespace

TEST(TestDataSource, DefaultWorldMatrix_IsIdentity)
{
    ConcreteDataSource ds;
    const GfMatrix4d& m = ds.worldMatrix();
    EXPECT_EQ(m, GfMatrix4d(1.0));
}

TEST(TestDataSource, DefaultProperties_IsEmpty)
{
    ConcreteDataSource ds;
    const VtDictionary& props = ds.properties();
    EXPECT_TRUE(props.empty());
}

TEST(TestDataSource, Default_Is2D_IsFalse)
{
    ConcreteDataSource ds;
    EXPECT_FALSE(ds.is2D());
}

TEST(TestDataSource, Default_IsZAxisUp_IsFalse)
{
    ConcreteDataSource ds;
    EXPECT_FALSE(ds.isZAxisUp());
}

TEST(TestDataSource, Default_IsPrimitive_IsFalse)
{
    ConcreteDataSource ds;
    EXPECT_FALSE(ds.isPrimitive(SdfPath("/some/prim")));
}

TEST(TestDataSource, Default_TransformPrimitives_IsFalse)
{
    ConcreteDataSource ds;
    SdfPathSet paths;
    paths.insert(SdfPath("/prim"));
    EXPECT_FALSE(ds.transformPrimitives(paths, GfVec3d(0), GfRotation(), GfVec3d(1)));
}

TEST(TestDataSource, Default_ErasePrimitives_IsFalse)
{
    ConcreteDataSource ds;
    SdfPathSet paths;
    paths.insert(SdfPath("/prim"));
    EXPECT_FALSE(ds.erasePrimitives(paths));
}

TEST(TestDataSource, Default_FeatureFlags_IsNoFeatures)
{
    ConcreteDataSource ds;
    EXPECT_EQ(ds.featureFlags(), static_cast<unsigned int>(hvt::SceneDataSource::NoFeatures));
}

TEST(TestDataSource, Default_GetCameraSettings_IsFalse)
{
    ConcreteDataSource ds;
    hvt::CameraSettings settings;
    EXPECT_FALSE(ds.getCameraSettings(settings));
}

TEST(TestDataSource, Default_RefineLevelFallback)
{
    ConcreteDataSource ds;
    EXPECT_EQ(ds.refineLevelFallback(), 0);
    ds.setRefineLevelFallback(3);
    EXPECT_EQ(ds.refineLevelFallback(), 3);
}

TEST(TestDataSource, Default_GetSelectionDelegate_IsNull)
{
    ConcreteDataSource ds;
    EXPECT_EQ(ds.GetSelectionDelegate(), nullptr);
}

// ===========================================================================
// DataSourceRegistry
// ===========================================================================

TEST(TestDataSource, Registry_Singleton_IsConsistent)
{
    auto& reg1 = hvt::DataSourceRegistry::registry();
    auto& reg2 = hvt::DataSourceRegistry::registry();
    EXPECT_EQ(&reg1, &reg2);
}

TEST(TestDataSource, Registry_RegisterAndQuery)
{
    auto& reg = hvt::DataSourceRegistry::registry();
    const size_t initialCount = reg.fileTypesDescCount();

    hvt::DataSourceRegistry::FileTypesDesc desc;
    desc.extensions = { ".test_ext_a", ".test_ext_b" };
    desc.creator    = nullptr;

    EXPECT_TRUE(reg.registerFileTypes(desc));
    EXPECT_EQ(reg.fileTypesDescCount(), initialCount + 1);

    EXPECT_TRUE(reg.isSupportedFileType(".test_ext_a"));
    EXPECT_TRUE(reg.isSupportedFileType(".test_ext_b"));
    EXPECT_FALSE(reg.isSupportedFileType(".nonexistent_ext"));

    hvt::DataSourceRegistry::FileTypesDesc found;
    EXPECT_TRUE(reg.getFileTypesDesc(".test_ext_a", found));
    EXPECT_TRUE(found.extensions.count(".test_ext_a"));
    EXPECT_TRUE(found.extensions.count(".test_ext_b"));
}

TEST(TestDataSource, Registry_DuplicateRegistration_Fails)
{
    auto& reg = hvt::DataSourceRegistry::registry();

    hvt::DataSourceRegistry::FileTypesDesc desc;
    desc.extensions = { ".test_dup_ext" };
    desc.creator    = nullptr;

    EXPECT_TRUE(reg.registerFileTypes(desc));
    EXPECT_FALSE(reg.registerFileTypes(desc));
}

TEST(TestDataSource, Registry_GetByIndex)
{
    auto& reg   = hvt::DataSourceRegistry::registry();
    size_t count = reg.fileTypesDescCount();
    ASSERT_GT(count, 0u);

    const auto& desc = reg.getFileTypesDesc(count - 1);
    EXPECT_FALSE(desc.extensions.empty());
}

// ===========================================================================
// CameraSettings
// ===========================================================================

TEST(TestDataSource, CameraSettings_Equality)
{
    hvt::CameraSettings a;
    a.position     = GfVec3f(0, 0, 5);
    a.target       = GfVec3f(0, 0, 0);
    a.up           = GfVec3f(0, 1, 0);
    a.aspect       = 1.5f;
    a.fov          = 60.0f;
    a.orthoScale   = 1.0f;
    a.isPerspective = true;

    hvt::CameraSettings b = a;
    EXPECT_TRUE(a == b);

    b.fov = 90.0f;
    EXPECT_FALSE(a == b);
}
