#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

// .prefab은 guid를 파일 본문에 보관한다(sidecar 없음). 이 importer는 본문 guid를
// 권위 있는 값으로 취급하므로, AssetDatabase가 .meta를 만들 때 동일 guid를 쓰게 한다.
class PrefabImporter : public IImporter {
public:
    std::string Name() const override { return "PrefabImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& ext) const override { return ext == ".prefab"; }
    ImportResult Import(const std::string& absSourcePath) const override;

    // .prefab 본문에서 "guid"를 읽어 반환(없으면 빈 문자열).
    static std::string ReadEmbeddedGuid(const std::string& absSourcePath);
};

} // namespace molga
