#pragma once

#include <string>
#include <initializer_list>
#include <utility>
#include <vector>
#include "Common/Types.h"

// 스크립트 필드 리플렉션
// ---------------------------------------------------------------------------
// 한 번 RegisterFields()로 등록하면 직렬화(저장/로드)와 인스펙터 UI가
// 동일한 메타데이터를 공유한다. imgui에 의존하지 않으므로 런타임 빌드에서도
// 직렬화에 그대로 사용된다.

// 살아있는 씬 GameObject 참조. 안정적인 id로 직렬화되고, 런타임에는
// 소유 스크립트의 World를 통해 포인터로 해석된다. Instantiate/Prefab 복제
// 시 같은 서브트리 내부 참조는 자동으로 새 id로 리매핑된다.
struct ObjectRef {
    unsigned int targetId = 0;  // 0 = 참조 없음(null)
    bool IsSet() const { return targetId != 0; }
    void Clear() { targetId = 0; }
};

// 프리팹 에셋 참조. GUID 문자열로 직렬화된다(리매핑 불필요).
struct PrefabRef {
    std::string guid;
    bool IsSet() const { return !guid.empty(); }
    void Clear() { guid.clear(); }
};

enum class ScriptFieldType {
    Float,
    Int,
    Enum,
    Bool,
    String,
    Vector2,
    Color,
    ObjectRef,   // 씬 오브젝트 참조 (id)
    PrefabRef,   // 프리팹 에셋 참조 (guid)
};

struct ScriptFieldRef {
    std::string name;
    ScriptFieldType type;
    void* ptr = nullptr;

    // 인스펙터 UI 힌트 (Float 전용). 0,0이면 제한 없음.
    float uiSpeed = 1.0f;
    float uiMin = 0.0f;
    float uiMax = 0.0f;
    std::vector<std::string> enumLabels;
};

// 스크립트 인스턴스가 자신의 멤버 포인터를 등록하는 컨테이너.
// 멤버 주소를 담으므로 해당 인스턴스 수명 동안만 유효하다(스크립트는
// unique_ptr로 힙에 고정되어 이동되지 않으므로 안전).
class ScriptFieldRegistry {
public:
    ScriptFieldRegistry& Float(const std::string& name, float* p,
                               float speed = 1.0f, float min = 0.0f, float max = 0.0f) {
        fields_.push_back({name, ScriptFieldType::Float, p, speed, min, max, {}});
        return *this;
    }
    ScriptFieldRegistry& Int(const std::string& name, int* p) {
        fields_.push_back({name, ScriptFieldType::Int, p, 1.0f, 0.0f, 0.0f, {}});
        return *this;
    }
    ScriptFieldRegistry& Enum(const std::string& name, int* p,
                              std::vector<std::string> labels) {
        fields_.push_back({name, ScriptFieldType::Enum, p, 1.0f, 0.0f, 0.0f,
                           std::move(labels)});
        return *this;
    }
    ScriptFieldRegistry& Enum(const std::string& name, int* p,
                              std::initializer_list<const char*> labels) {
        std::vector<std::string> owned;
        owned.reserve(labels.size());
        for (const char* label : labels) owned.emplace_back(label ? label : "");
        return Enum(name, p, std::move(owned));
    }
    ScriptFieldRegistry& Bool(const std::string& name, bool* p) {
        fields_.push_back({name, ScriptFieldType::Bool, p, 1.0f, 0.0f, 0.0f, {}});
        return *this;
    }
    ScriptFieldRegistry& String(const std::string& name, std::string* p) {
        fields_.push_back({name, ScriptFieldType::String, p, 1.0f, 0.0f, 0.0f, {}});
        return *this;
    }
    ScriptFieldRegistry& Vec2(const std::string& name, ::Vector2* p) {
        fields_.push_back({name, ScriptFieldType::Vector2, p, 1.0f, 0.0f, 0.0f, {}});
        return *this;
    }
    ScriptFieldRegistry& Color(const std::string& name, ::Color* p) {
        fields_.push_back({name, ScriptFieldType::Color, p, 1.0f, 0.0f, 0.0f, {}});
        return *this;
    }
    ScriptFieldRegistry& Object(const std::string& name, ::ObjectRef* p) {
        fields_.push_back({name, ScriptFieldType::ObjectRef, p, 1.0f, 0.0f, 0.0f, {}});
        return *this;
    }
    ScriptFieldRegistry& Prefab(const std::string& name, ::PrefabRef* p) {
        fields_.push_back({name, ScriptFieldType::PrefabRef, p, 1.0f, 0.0f, 0.0f, {}});
        return *this;
    }

    const std::vector<ScriptFieldRef>& Fields() const { return fields_; }
    bool Empty() const { return fields_.empty(); }

private:
    std::vector<ScriptFieldRef> fields_;
};
