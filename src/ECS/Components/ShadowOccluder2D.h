#pragma once

#include "Common/Types.h"
#include "ECS/Component.h"

#include <vector>

enum class ShadowOccluderShape2D {
    Box,
    Polygon,
};

class ShadowOccluder2D final : public Component {
public:
    COMPONENT_TYPE(ShadowOccluder2D)

    static constexpr std::size_t MinPolygonVertices = 3;
    static constexpr std::size_t MaxPolygonVertices = 8;

    ShadowOccluder2D();

    ShadowOccluderShape2D GetShape() const noexcept { return shape_; }
    bool SetShape(ShadowOccluderShape2D shape) noexcept;

    const Vector2& GetOffset() const noexcept { return offset_; }
    const Vector2& GetSize() const noexcept { return size_; }
    const std::vector<Vector2>& GetVertices() const noexcept { return vertices_; }
    bool IsShapeValid() const noexcept { return shapeValid_; }

    bool SetBox(const Vector2& offset, const Vector2& size) noexcept;
    bool SetPolygon(const std::vector<Vector2>& vertices) noexcept;
    void ResetToDefaultBox() noexcept;

    std::vector<Vector2> GetWorldVertices() const;

    void Serialize(nlohmann::json& json) const override;
    void Deserialize(const nlohmann::json& json) override;

    static bool ValidateAndNormalizePolygon(
        const std::vector<Vector2>& input,
        std::vector<Vector2>& normalized) noexcept;

private:
    ShadowOccluderShape2D shape_ = ShadowOccluderShape2D::Box;
    Vector2 offset_ = Vector2::Zero();
    Vector2 size_{100.0f, 100.0f};
    std::vector<Vector2> vertices_;
    bool shapeValid_ = true;
};
