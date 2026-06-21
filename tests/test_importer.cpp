#include "Core/Importers/Importer.h"
#include "Core/Importers/TextureImporter.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

using molga::TextureImporter;
using molga::ImportResult;
namespace fs = std::filesystem;

TEST_CASE("TextureImporter reports its name and a positive version") {
    TextureImporter imp;
    CHECK(imp.Name() == std::string("TextureImporter"));
    CHECK(imp.Version() >= 1);
}

TEST_CASE("TextureImporter accepts image extensions only") {
    TextureImporter imp;
    CHECK(imp.CanImport(".png"));
    CHECK(imp.CanImport(".jpg"));
    CHECK_FALSE(imp.CanImport(".wav"));
}

TEST_CASE("Import of a missing file fails gracefully") {
    TextureImporter imp;
    ImportResult r = imp.Import("does_not_exist.png");
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.error.empty());
}
