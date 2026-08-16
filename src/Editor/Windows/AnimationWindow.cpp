#include "AnimationWindow.h"
#include "Editor/ImGuiTextureBridge.h"

#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Core/Guid.h"
#include "Core/SpriteResolver.h"
#include "Core/TextureImportSettings.h"
#include "Editor/Commands/ProjectFileCommands.h"
#include "Editor/Editor.h"
#include "Editor/EditorConstants.h"
#include "Rendering/Texture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <sstream>

namespace {

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

bool AcceptGuid(const char* importer, std::string& guidOut) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool accepted = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
        const char* data = static_cast<const char*>(payload->Data);
        const std::string guid = data ? data : "";
        const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
        if (record && record->importer == importer) {
            guidOut = guid;
            accepted = true;
        }
    }
    ImGui::EndDragDropTarget();
    return accepted;
}

std::vector<molga::SpriteSlice> SlicesForTexture(const std::string& textureGuid) {
    const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(textureGuid);
    if (!record || record->importer != "TextureImporter") return {};
    return molga::DeserializeTextureImportSettings(record->settings, true).slices;
}

molga::AnimatorParameterValue2D DefaultValue(molga::AnimatorParameterType2D type) {
    if (type == molga::AnimatorParameterType2D::Int) return 0;
    if (type == molga::AnimatorParameterType2D::Float) return 0.0f;
    return false;
}

molga::AnimatorConditionOperator2D DefaultOperator(
    molga::AnimatorParameterType2D type) {
    if (type == molga::AnimatorParameterType2D::Bool ||
        type == molga::AnimatorParameterType2D::Trigger) {
        return molga::AnimatorConditionOperator2D::IsTrue;
    }
    return molga::AnimatorConditionOperator2D::Equals;
}

} // namespace

AnimationWindow::AnimationWindow()
    : EditorWindow(EditorConstants::WIN_ANIMATION) {
    isOpen = false;
}

void AnimationWindow::SetAsset(const std::string& path) {
    const std::filesystem::path selected(path);
    std::string extension = selected.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".animclip" && extension != ".animator") return;
    assetPath_ = selected;
    assetGuid_ = molga::AssetDatabase::Get().GuidForAbsolutePath(selected);
    kind_ = extension == ".animclip" ? AssetKind::Clip : AssetKind::Controller;
    previewPlaying_ = false;
    previewTime_ = 0.0f;
    Reload();
    isOpen = true;
}

void AnimationWindow::Reload() {
    error_.clear();
    dirty_ = false;
    if (kind_ == AssetKind::Clip) {
        clip_ = molga::AnimationClip2D{};
        if (!clip_.LoadFromFile(assetPath_, &error_)) return;
    } else if (kind_ == AssetKind::Controller) {
        controller_ = molga::AnimatorController2D{};
        if (!controller_.LoadFromFile(assetPath_, &error_)) return;
    }
}

void AnimationWindow::Save() {
    error_.clear();
    if (assetPath_.empty()) return;
    nlohmann::json document;
    if (kind_ == AssetKind::Clip) {
        if (!clip_.Validate(&error_)) return;
        document = clip_.ToJson();
    } else if (kind_ == AssetKind::Controller) {
        if (!controller_.Validate(&error_)) return;
        document = controller_.ToJson();
    } else {
        return;
    }
    const std::string before = ReadText(assetPath_);
    const std::string after = document.dump(2) + '\n';
    if (before != after) {
        Editor::Get().GetAssetCommandHistory().Execute(
            std::make_unique<molga::AssetContentCommand>(
                assetPath_, before, after, assetGuid_));
    }
    dirty_ = false;
}

void AnimationWindow::DrawToolbar() {
    if (ImGui::Button("Save")) Save();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) Reload();
    ImGui::SameLine();
    auto& history = Editor::Get().GetAssetCommandHistory();
    const bool canUndo = history.CanUndo();
    if (!canUndo) ImGui::BeginDisabled();
    if (ImGui::Button("Undo Asset")) {
        history.Undo();
        Reload();
    }
    if (!canUndo) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canRedo = history.CanRedo();
    if (!canRedo) ImGui::BeginDisabled();
    if (ImGui::Button("Redo Asset")) {
        history.Redo();
        Reload();
    }
    if (!canRedo) ImGui::EndDisabled();
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Unsaved");
    }
}

void AnimationWindow::DrawClip() {
    char textureGuid[128];
    std::strncpy(textureGuid, clip_.GetTextureGuid().c_str(), sizeof(textureGuid) - 1);
    textureGuid[sizeof(textureGuid) - 1] = '\0';
    if (ImGui::InputText("Texture GUID", textureGuid, sizeof(textureGuid))) {
        clip_.SetTextureGuid(textureGuid);
        dirty_ = true;
    }
    std::string droppedGuid;
    if (AcceptGuid("TextureImporter", droppedGuid)) {
        clip_.SetTextureGuid(droppedGuid);
        dirty_ = true;
    }
    bool looping = clip_.IsLooping();
    if (ImGui::Checkbox("Loop", &looping)) {
        clip_.SetLooping(looping);
        dirty_ = true;
    }

    const auto slices = SlicesForTexture(clip_.GetTextureGuid());
    auto& frames = clip_.GetFrames();
    if (ImGui::BeginTable("ClipFrames", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Slice");
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < frames.size();) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", index);
            ImGui::TableSetColumnIndex(1);
            const auto named = std::find_if(slices.begin(), slices.end(),
                [&](const molga::SpriteSlice& slice) { return slice.id == frames[index].sliceId; });
            const std::string preview = named == slices.end()
                ? (frames[index].sliceId.empty() ? "(select slice)" : frames[index].sliceId)
                : named->name + "  [" + named->id.substr(0, 8) + "]";
            if (ImGui::BeginCombo("##slice", preview.c_str())) {
                for (const auto& slice : slices) {
                    if (ImGui::Selectable((slice.name + "##" + slice.id).c_str(),
                                          slice.id == frames[index].sliceId)) {
                        frames[index].sliceId = slice.id;
                        dirty_ = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (slices.empty()) {
                char sliceId[128];
                std::strncpy(sliceId, frames[index].sliceId.c_str(), sizeof(sliceId) - 1);
                sliceId[sizeof(sliceId) - 1] = '\0';
                if (ImGui::InputText("##sliceId", sliceId, sizeof(sliceId))) {
                    frames[index].sliceId = sliceId;
                    dirty_ = true;
                }
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::DragFloat("##duration", &frames[index].durationSeconds,
                                 0.005f, 0.001f, 60.0f, "%.3f s")) {
                dirty_ = true;
            }
            ImGui::TableSetColumnIndex(3);
            bool removed = false;
            if (ImGui::SmallButton("Up") && index > 0) {
                std::swap(frames[index], frames[index - 1]);
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && index + 1 < frames.size()) {
                std::swap(frames[index], frames[index + 1]);
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                frames.erase(frames.begin() + static_cast<std::ptrdiff_t>(index));
                dirty_ = true;
                removed = true;
            }
            ImGui::PopID();
            if (!removed) ++index;
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Add Frame")) {
        frames.push_back({slices.empty() ? std::string{} : slices.front().id, 1.0f / 12.0f});
        dirty_ = true;
    }

    ImGui::SeparatorText("Preview");
    const float duration = clip_.GetDurationSeconds();
    if (previewPlaying_ && duration > 0.0f) {
        previewTime_ += ImGui::GetIO().DeltaTime;
        if (clip_.IsLooping()) previewTime_ = std::fmod(previewTime_, duration);
        else if (previewTime_ >= duration) {
            previewTime_ = duration;
            previewPlaying_ = false;
        }
    }
    if (ImGui::Button(previewPlaying_ ? "Pause Preview" : "Play Preview")) {
        previewPlaying_ = !previewPlaying_;
        if (previewTime_ >= duration) previewTime_ = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop Preview")) {
        previewPlaying_ = false;
        previewTime_ = 0.0f;
    }
    const float scrubMax = std::max(duration, 0.001f);
    ImGui::SliderFloat("Time", &previewTime_, 0.0f, scrubMax, "%.3f s");
    if (!frames.empty()) {
        const std::size_t frameIndex = clip_.GetFrameIndexAt(previewTime_);
        const molga::ResolvedSprite sprite =
            molga::SpriteResolver::Resolve(clip_.GetSpriteRef(frameIndex));
        ImGui::Text("Frame %zu / %zu", frameIndex + 1, frames.size());
        if (sprite.valid && sprite.texture) {
            const float maxSide = 192.0f;
            const float scale = std::min(maxSide / std::max(sprite.nativeSize.x, 1.0f),
                                         maxSide / std::max(sprite.nativeSize.y, 1.0f));
            const ImVec2 size(std::max(1.0f, sprite.nativeSize.x * scale),
                              std::max(1.0f, sprite.nativeSize.y * scale));
            ImGui::Image(ImGuiTextureBridge::From(sprite.texture->Handle()),
                         size, ImVec2(sprite.uv.u0, sprite.uv.v0),
                         ImVec2(sprite.uv.u1, sprite.uv.v1));
        } else {
            ImGui::TextDisabled("The current slice cannot be resolved.");
        }
    }
}

void AnimationWindow::DrawController() {
    auto& parameters = controller_.GetParameters();
    auto& states = controller_.GetStates();
    auto& transitions = controller_.GetTransitions();

    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t index = 0; index < parameters.size();) {
            auto& parameter = parameters[index];
            ImGui::PushID(static_cast<int>(index));
            char name[128];
            std::strncpy(name, parameter.name.c_str(), sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (ImGui::InputText("Name", name, sizeof(name))) {
                const std::string previousName = parameter.name;
                parameter.name = name;
                for (auto& transition : transitions) {
                    for (auto& condition : transition.conditions) {
                        if (condition.parameter == previousName) {
                            condition.parameter = parameter.name;
                        }
                    }
                }
                dirty_ = true;
            }
            int type = static_cast<int>(parameter.type);
            if (ImGui::Combo("Type", &type, "Bool\0Int\0Float\0Trigger\0")) {
                parameter.type = static_cast<molga::AnimatorParameterType2D>(type);
                parameter.defaultValue = DefaultValue(parameter.type);
                for (auto& transition : transitions) {
                    for (auto& condition : transition.conditions) {
                        if (condition.parameter == parameter.name) {
                            condition.op = DefaultOperator(parameter.type);
                            condition.value = DefaultValue(parameter.type);
                        }
                    }
                }
                dirty_ = true;
            }
            if (parameter.type == molga::AnimatorParameterType2D::Bool) {
                bool value = std::get<bool>(parameter.defaultValue);
                if (ImGui::Checkbox("Default", &value)) {
                    parameter.defaultValue = value;
                    dirty_ = true;
                }
            } else if (parameter.type == molga::AnimatorParameterType2D::Int) {
                int value = std::get<int>(parameter.defaultValue);
                if (ImGui::InputInt("Default", &value)) {
                    parameter.defaultValue = value;
                    dirty_ = true;
                }
            } else if (parameter.type == molga::AnimatorParameterType2D::Float) {
                float value = std::get<float>(parameter.defaultValue);
                if (ImGui::DragFloat("Default", &value, 0.05f)) {
                    parameter.defaultValue = value;
                    dirty_ = true;
                }
            } else {
                ImGui::TextDisabled("Triggers always default to reset.");
            }
            bool removed = ImGui::Button("Remove Parameter");
            ImGui::Separator();
            ImGui::PopID();
            if (removed) {
                const std::string removedName = parameter.name;
                parameters.erase(parameters.begin() + static_cast<std::ptrdiff_t>(index));
                for (auto& transition : transitions) {
                    transition.conditions.erase(
                        std::remove_if(transition.conditions.begin(),
                                       transition.conditions.end(),
                            [&](const auto& condition) {
                                return condition.parameter == removedName;
                            }),
                        transition.conditions.end());
                }
                dirty_ = true;
            } else {
                ++index;
            }
        }
        if (ImGui::Button("Add Parameter")) {
            parameters.push_back({"Parameter" + std::to_string(parameters.size() + 1),
                                  molga::AnimatorParameterType2D::Bool, false});
            dirty_ = true;
        }
    }

    if (ImGui::CollapsingHeader("States", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t index = 0; index < states.size();) {
            auto& state = states[index];
            ImGui::PushID(static_cast<int>(index));
            const bool isDefault = controller_.GetDefaultStateId() == state.id;
            if (ImGui::RadioButton("Default", isDefault)) {
                controller_.SetDefaultStateId(state.id);
                dirty_ = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("ID %s", state.id.c_str());
            char name[128];
            std::strncpy(name, state.name.c_str(), sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (ImGui::InputText("Name", name, sizeof(name))) {
                state.name = name;
                dirty_ = true;
            }
            char clipGuid[128];
            std::strncpy(clipGuid, state.clipGuid.c_str(), sizeof(clipGuid) - 1);
            clipGuid[sizeof(clipGuid) - 1] = '\0';
            if (ImGui::InputText("Clip GUID", clipGuid, sizeof(clipGuid))) {
                state.clipGuid = clipGuid;
                dirty_ = true;
            }
            std::string dropped;
            if (AcceptGuid("AnimationClipImporter", dropped)) {
                state.clipGuid = dropped;
                dirty_ = true;
            }
            if (ImGui::DragFloat("Speed", &state.speed, 0.05f, 0.0f, 1000.0f)) dirty_ = true;
            const bool removed = ImGui::Button("Remove State");
            ImGui::Separator();
            const std::string removedId = state.id;
            ImGui::PopID();
            if (removed) {
                states.erase(states.begin() + static_cast<std::ptrdiff_t>(index));
                transitions.erase(
                    std::remove_if(transitions.begin(), transitions.end(),
                        [&](const auto& transition) {
                            return transition.fromStateId == removedId ||
                                   transition.toStateId == removedId;
                        }),
                    transitions.end());
                if (controller_.GetDefaultStateId() == removedId) {
                    controller_.SetDefaultStateId(states.empty() ? "" : states.front().id);
                }
                dirty_ = true;
            } else {
                ++index;
            }
        }
        if (ImGui::Button("Add State")) {
            molga::AnimatorState2D state;
            state.id = molga::Guid::Generate();
            state.name = "State " + std::to_string(states.size() + 1);
            states.push_back(std::move(state));
            if (states.size() == 1) controller_.SetDefaultStateId(states.front().id);
            dirty_ = true;
        }
    }

    if (ImGui::CollapsingHeader("Transitions (serialized priority)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t index = 0; index < transitions.size();) {
            auto& transition = transitions[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("Priority %zu", index);
            auto drawStateCombo = [&](const char* label, std::string& selected) {
                const auto current = std::find_if(states.begin(), states.end(),
                    [&](const auto& state) { return state.id == selected; });
                const char* preview = current == states.end() ? "(missing)" : current->name.c_str();
                if (ImGui::BeginCombo(label, preview)) {
                    for (const auto& state : states) {
                        if (ImGui::Selectable((state.name + "##" + state.id).c_str(),
                                              state.id == selected)) {
                            selected = state.id;
                            dirty_ = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            };
            drawStateCombo("From", transition.fromStateId);
            drawStateCombo("To", transition.toStateId);
            if (ImGui::Checkbox("Has Exit Time", &transition.hasExitTime)) dirty_ = true;
            if (transition.hasExitTime &&
                ImGui::DragFloat("Exit Time", &transition.exitTime, 0.01f, 0.0f, 1000.0f)) {
                dirty_ = true;
            }
            if (ImGui::TreeNode("Conditions")) {
                for (std::size_t conditionIndex = 0;
                     conditionIndex < transition.conditions.size();) {
                    auto& condition = transition.conditions[conditionIndex];
                    ImGui::PushID(static_cast<int>(conditionIndex));
                    if (ImGui::BeginCombo("Parameter", condition.parameter.c_str())) {
                        for (const auto& parameter : parameters) {
                            if (ImGui::Selectable(parameter.name.c_str(),
                                                  parameter.name == condition.parameter)) {
                                condition.parameter = parameter.name;
                                condition.op = DefaultOperator(parameter.type);
                                condition.value = DefaultValue(parameter.type);
                                dirty_ = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                        [&](const auto& value) { return value.name == condition.parameter; });
                    const auto type = parameter == parameters.end()
                        ? molga::AnimatorParameterType2D::Bool : parameter->type;
                    const char* boolOps = "Equals\0Not Equal\0Is True\0Is False\0";
                    const char* numericOps =
                        "Equals\0Not Equal\0Greater\0Less\0Greater or Equal\0Less or Equal\0";
                    int opIndex = 0;
                    if (type == molga::AnimatorParameterType2D::Bool ||
                        type == molga::AnimatorParameterType2D::Trigger) {
                        switch (condition.op) {
                            case molga::AnimatorConditionOperator2D::NotEqual: opIndex = 1; break;
                            case molga::AnimatorConditionOperator2D::IsTrue: opIndex = 2; break;
                            case molga::AnimatorConditionOperator2D::IsFalse: opIndex = 3; break;
                            default: opIndex = 0; break;
                        }
                        if (ImGui::Combo("Operator", &opIndex, boolOps)) {
                            const auto ops = std::array{
                                molga::AnimatorConditionOperator2D::Equals,
                                molga::AnimatorConditionOperator2D::NotEqual,
                                molga::AnimatorConditionOperator2D::IsTrue,
                                molga::AnimatorConditionOperator2D::IsFalse};
                            condition.op = ops[static_cast<std::size_t>(opIndex)];
                            dirty_ = true;
                        }
                        bool value = std::get_if<bool>(&condition.value)
                            ? std::get<bool>(condition.value) : false;
                        if (condition.op != molga::AnimatorConditionOperator2D::IsTrue &&
                            condition.op != molga::AnimatorConditionOperator2D::IsFalse &&
                            ImGui::Checkbox("Value", &value)) {
                            condition.value = value;
                            dirty_ = true;
                        }
                    } else {
                        opIndex = static_cast<int>(condition.op);
                        opIndex = std::clamp(opIndex, 0, 5);
                        if (ImGui::Combo("Operator", &opIndex, numericOps)) {
                            condition.op = static_cast<molga::AnimatorConditionOperator2D>(opIndex);
                            dirty_ = true;
                        }
                        if (type == molga::AnimatorParameterType2D::Int) {
                            int value = std::get_if<int>(&condition.value)
                                ? std::get<int>(condition.value) : 0;
                            if (ImGui::InputInt("Value", &value)) {
                                condition.value = value;
                                dirty_ = true;
                            }
                        } else {
                            float value = std::get_if<float>(&condition.value)
                                ? std::get<float>(condition.value) : 0.0f;
                            if (ImGui::DragFloat("Value", &value, 0.05f)) {
                                condition.value = value;
                                dirty_ = true;
                            }
                        }
                    }
                    const bool removeCondition = ImGui::SmallButton("Remove Condition");
                    ImGui::Separator();
                    ImGui::PopID();
                    if (removeCondition) {
                        transition.conditions.erase(
                            transition.conditions.begin() +
                            static_cast<std::ptrdiff_t>(conditionIndex));
                        dirty_ = true;
                    } else {
                        ++conditionIndex;
                    }
                }
                if (!parameters.empty() && ImGui::Button("Add Condition")) {
                    const auto& parameter = parameters.front();
                    transition.conditions.push_back({
                        parameter.name, DefaultOperator(parameter.type),
                        DefaultValue(parameter.type)});
                    dirty_ = true;
                }
                ImGui::TreePop();
            }
            if (ImGui::Button("Priority Up") && index > 0) {
                std::swap(transitions[index], transitions[index - 1]);
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Priority Down") && index + 1 < transitions.size()) {
                std::swap(transitions[index], transitions[index + 1]);
                dirty_ = true;
            }
            ImGui::SameLine();
            const bool removed = ImGui::Button("Remove Transition");
            ImGui::Separator();
            ImGui::PopID();
            if (removed) {
                transitions.erase(transitions.begin() + static_cast<std::ptrdiff_t>(index));
                dirty_ = true;
            } else {
                ++index;
            }
        }
        if (states.size() >= 2 && ImGui::Button("Add Transition")) {
            transitions.push_back({states[0].id, states[1].id, false, 0.0f, {}});
            dirty_ = true;
        }
    }
}

void AnimationWindow::OnGUI() {
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }
    if (kind_ == AssetKind::None || assetPath_.empty()) {
        ImGui::TextDisabled("Select an .animclip or .animator asset.");
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", assetPath_.string().c_str());
    ImGui::TextDisabled("GUID %s", assetGuid_.c_str());
    DrawToolbar();
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "%s", error_.c_str());
    }
    ImGui::Separator();
    if (kind_ == AssetKind::Clip) DrawClip();
    else DrawController();
    ImGui::End();
}
