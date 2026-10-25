#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include "../../ext/json/json.hpp"
#include "Offsets.hpp"
#include "FFlags.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

namespace OffsetLoader {

    inline std::string g_ExpectedVersion;
    inline bool g_LoadedFromJson = false;
    inline bool g_LoadedFromWeb = false;
    inline bool g_LoadedFFlagsFromWeb = false;

    inline void EnsureOffsetsFolder(const std::string& path = "data/offsets.json") {
        namespace fs = std::filesystem;
        fs::path jsonPath(path);
        fs::path dir = jsonPath.parent_path();

        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
            printf("Created %s folder\n", dir.string().c_str());
        }

        if (!fs::exists(jsonPath)) {
            std::ofstream f(jsonPath);
            f.close();
            printf("Created empty %s (place your offsets here)\n", path.c_str());
        }
    }

    inline void ApplyOffsetsFromJson(const nlohmann::json& root) {
        if (root.contains("ClientVersion") && root["ClientVersion"].is_string()) {
            g_ExpectedVersion = root["ClientVersion"].get<std::string>();
            Offsets::ClientVersion = g_ExpectedVersion;
        } else if (root.contains("Roblox Version") && root["Roblox Version"].is_string()) {
            g_ExpectedVersion = root["Roblox Version"].get<std::string>();
            Offsets::ClientVersion = g_ExpectedVersion;
        }

        if (root.contains("AirProperties")) {
            auto& ns = root["AirProperties"];
            if (ns.contains("AirDensity")) Offsets::AirProperties::AirDensity = ns["AirDensity"].get<uintptr_t>();
            if (ns.contains("GlobalWind")) Offsets::AirProperties::GlobalWind = ns["GlobalWind"].get<uintptr_t>();
        }
        if (root.contains("AnimationTrack")) {
            auto& ns = root["AnimationTrack"];
            if (ns.contains("Animation")) Offsets::AnimationTrack::Animation = ns["Animation"].get<uintptr_t>();
            if (ns.contains("Animator")) Offsets::AnimationTrack::Animator = ns["Animator"].get<uintptr_t>();
            if (ns.contains("IsPlaying")) Offsets::AnimationTrack::IsPlaying = ns["IsPlaying"].get<uintptr_t>();
            if (ns.contains("Looped")) Offsets::AnimationTrack::Looped = ns["Looped"].get<uintptr_t>();
            if (ns.contains("Speed")) Offsets::AnimationTrack::Speed = ns["Speed"].get<uintptr_t>();
            if (ns.contains("TimePosition")) Offsets::AnimationTrack::TimePosition = ns["TimePosition"].get<uintptr_t>();
        }
        if (root.contains("Animator")) {
            auto& ns = root["Animator"];
            if (ns.contains("ActiveAnimations")) Offsets::Animator::ActiveAnimations = ns["ActiveAnimations"].get<uintptr_t>();
        }
        if (root.contains("Atmosphere")) {
            auto& ns = root["Atmosphere"];
            if (ns.contains("Color")) Offsets::Atmosphere::Color = ns["Color"].get<uintptr_t>();
            if (ns.contains("Decay")) Offsets::Atmosphere::Decay = ns["Decay"].get<uintptr_t>();
            if (ns.contains("Density")) Offsets::Atmosphere::Density = ns["Density"].get<uintptr_t>();
            if (ns.contains("Glare")) Offsets::Atmosphere::Glare = ns["Glare"].get<uintptr_t>();
            if (ns.contains("Haze")) Offsets::Atmosphere::Haze = ns["Haze"].get<uintptr_t>();
            if (ns.contains("Offset")) Offsets::Atmosphere::Offset = ns["Offset"].get<uintptr_t>();
        }
        if (root.contains("Attachment")) {
            auto& ns = root["Attachment"];
            if (ns.contains("Position")) Offsets::Attachment::Position = ns["Position"].get<uintptr_t>();
        }
        if (root.contains("BasePart")) {
            auto& ns = root["BasePart"];
            if (ns.contains("CastShadow")) Offsets::BasePart::CastShadow = ns["CastShadow"].get<uintptr_t>();
            if (ns.contains("Color3")) Offsets::BasePart::Color3 = ns["Color3"].get<uintptr_t>();
            if (ns.contains("Locked")) Offsets::BasePart::Locked = ns["Locked"].get<uintptr_t>();
            if (ns.contains("Massless")) Offsets::BasePart::Massless = ns["Massless"].get<uintptr_t>();
            if (ns.contains("Primitive")) Offsets::BasePart::Primitive = ns["Primitive"].get<uintptr_t>();
            if (ns.contains("Reflectance")) Offsets::BasePart::Reflectance = ns["Reflectance"].get<uintptr_t>();
            if (ns.contains("Shape")) Offsets::BasePart::Shape = ns["Shape"].get<uintptr_t>();
            if (ns.contains("Transparency")) Offsets::BasePart::Transparency = ns["Transparency"].get<uintptr_t>();
        }
        if (root.contains("Beam")) {
            auto& ns = root["Beam"];
            if (ns.contains("Attachment0")) Offsets::Beam::Attachment0 = ns["Attachment0"].get<uintptr_t>();
            if (ns.contains("Attachment1")) Offsets::Beam::Attachment1 = ns["Attachment1"].get<uintptr_t>();
            if (ns.contains("Brightness")) Offsets::Beam::Brightness = ns["Brightness"].get<uintptr_t>();
            if (ns.contains("CurveSize0")) Offsets::Beam::CurveSize0 = ns["CurveSize0"].get<uintptr_t>();
            if (ns.contains("CurveSize1")) Offsets::Beam::CurveSize1 = ns["CurveSize1"].get<uintptr_t>();
            if (ns.contains("LightEmission")) Offsets::Beam::LightEmission = ns["LightEmission"].get<uintptr_t>();
            if (ns.contains("LightInfluence")) Offsets::Beam::LightInfluence = ns["LightInfluence"].get<uintptr_t>();
            if (ns.contains("Texture")) Offsets::Beam::Texture = ns["Texture"].get<uintptr_t>();
            if (ns.contains("TextureLength")) Offsets::Beam::TextureLength = ns["TextureLength"].get<uintptr_t>();
            if (ns.contains("TextureSpeed")) Offsets::Beam::TextureSpeed = ns["TextureSpeed"].get<uintptr_t>();
            if (ns.contains("Width0")) Offsets::Beam::Width0 = ns["Width0"].get<uintptr_t>();
            if (ns.contains("Width1")) Offsets::Beam::Width1 = ns["Width1"].get<uintptr_t>();
            if (ns.contains("ZOffset")) Offsets::Beam::ZOffset = ns["ZOffset"].get<uintptr_t>();
        }
        if (root.contains("BloomEffect")) {
            auto& ns = root["BloomEffect"];
            if (ns.contains("Enabled")) Offsets::BloomEffect::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("Intensity")) Offsets::BloomEffect::Intensity = ns["Intensity"].get<uintptr_t>();
            if (ns.contains("Size")) Offsets::BloomEffect::Size = ns["Size"].get<uintptr_t>();
            if (ns.contains("Threshold")) Offsets::BloomEffect::Threshold = ns["Threshold"].get<uintptr_t>();
        }
        if (root.contains("BlurEffect")) {
            auto& ns = root["BlurEffect"];
            if (ns.contains("Enabled")) Offsets::BlurEffect::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("Size")) Offsets::BlurEffect::Size = ns["Size"].get<uintptr_t>();
        }
        if (root.contains("ByteCode")) {
            auto& ns = root["ByteCode"];
            if (ns.contains("Pointer")) Offsets::ByteCode::Pointer = ns["Pointer"].get<uintptr_t>();
            if (ns.contains("Size")) Offsets::ByteCode::Size = ns["Size"].get<uintptr_t>();
        }
        if (root.contains("Camera")) {
            auto& ns = root["Camera"];
            if (ns.contains("CameraSubject")) Offsets::Camera::CameraSubject = ns["CameraSubject"].get<uintptr_t>();
            if (ns.contains("CameraType")) Offsets::Camera::CameraType = ns["CameraType"].get<uintptr_t>();
            if (ns.contains("FieldOfView")) Offsets::Camera::FieldOfView = ns["FieldOfView"].get<uintptr_t>();
            if (ns.contains("Position")) Offsets::Camera::Position = ns["Position"].get<uintptr_t>();
            if (ns.contains("Rotation")) Offsets::Camera::Rotation = ns["Rotation"].get<uintptr_t>();
            if (ns.contains("Viewport")) Offsets::Camera::Viewport = ns["Viewport"].get<uintptr_t>();
            if (ns.contains("ImagePlaneDepth")) Offsets::Camera::ImagePlaneDepth = ns["ImagePlaneDepth"].get<uintptr_t>();
            if (ns.contains("ViewportSize")) Offsets::Camera::ViewportSize = ns["ViewportSize"].get<uintptr_t>();
        }
        if (root.contains("CharacterMesh")) {
            auto& ns = root["CharacterMesh"];
            if (ns.contains("BaseTextureId")) Offsets::CharacterMesh::BaseTextureId = ns["BaseTextureId"].get<uintptr_t>();
            if (ns.contains("BodyPart")) Offsets::CharacterMesh::BodyPart = ns["BodyPart"].get<uintptr_t>();
            if (ns.contains("MeshId")) Offsets::CharacterMesh::MeshId = ns["MeshId"].get<uintptr_t>();
            if (ns.contains("OverlayTextureId")) Offsets::CharacterMesh::OverlayTextureId = ns["OverlayTextureId"].get<uintptr_t>();
        }
        if (root.contains("ClickDetector")) {
            auto& ns = root["ClickDetector"];
            if (ns.contains("MaxActivationDistance")) Offsets::ClickDetector::MaxActivationDistance = ns["MaxActivationDistance"].get<uintptr_t>();
            if (ns.contains("MouseIcon")) Offsets::ClickDetector::MouseIcon = ns["MouseIcon"].get<uintptr_t>();
        }
        if (root.contains("Clothing")) {
            auto& ns = root["Clothing"];
            if (ns.contains("Color3")) Offsets::Clothing::Color3 = ns["Color3"].get<uintptr_t>();
            if (ns.contains("Template")) Offsets::Clothing::Template = ns["Template"].get<uintptr_t>();
        }
        if (root.contains("ColorCorrectionEffect")) {
            auto& ns = root["ColorCorrectionEffect"];
            if (ns.contains("Brightness")) Offsets::ColorCorrectionEffect::Brightness = ns["Brightness"].get<uintptr_t>();
            if (ns.contains("Contrast")) Offsets::ColorCorrectionEffect::Contrast = ns["Contrast"].get<uintptr_t>();
            if (ns.contains("Enabled")) Offsets::ColorCorrectionEffect::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("TintColor")) Offsets::ColorCorrectionEffect::TintColor = ns["TintColor"].get<uintptr_t>();
        }
        if (root.contains("ColorGradingEffect")) {
            auto& ns = root["ColorGradingEffect"];
            if (ns.contains("Enabled")) Offsets::ColorGradingEffect::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("TonemapperPreset")) Offsets::ColorGradingEffect::TonemapperPreset = ns["TonemapperPreset"].get<uintptr_t>();
        }
        if (root.contains("DataModel")) {
            auto& ns = root["DataModel"];
            if (ns.contains("CreatorId")) Offsets::DataModel::CreatorId = ns["CreatorId"].get<uintptr_t>();
            if (ns.contains("GameId")) Offsets::DataModel::GameId = ns["GameId"].get<uintptr_t>();
            if (ns.contains("GameLoaded")) Offsets::DataModel::GameLoaded = ns["GameLoaded"].get<uintptr_t>();
            if (ns.contains("JobId")) Offsets::DataModel::JobId = ns["JobId"].get<uintptr_t>();
            if (ns.contains("PlaceId")) Offsets::DataModel::PlaceId = ns["PlaceId"].get<uintptr_t>();
            if (ns.contains("PlaceVersion")) Offsets::DataModel::PlaceVersion = ns["PlaceVersion"].get<uintptr_t>();
            if (ns.contains("PrimitiveCount")) Offsets::DataModel::PrimitiveCount = ns["PrimitiveCount"].get<uintptr_t>();
            if (ns.contains("ScriptContext")) Offsets::DataModel::ScriptContext = ns["ScriptContext"].get<uintptr_t>();
            if (ns.contains("ServerIP")) Offsets::DataModel::ServerIP = ns["ServerIP"].get<uintptr_t>();
            if (ns.contains("ToRenderView1")) Offsets::DataModel::ToRenderView1 = ns["ToRenderView1"].get<uintptr_t>();
            if (ns.contains("ToRenderView2")) Offsets::DataModel::ToRenderView2 = ns["ToRenderView2"].get<uintptr_t>();
            if (ns.contains("ToRenderView3")) Offsets::DataModel::ToRenderView3 = ns["ToRenderView3"].get<uintptr_t>();
            if (ns.contains("Workspace")) Offsets::DataModel::Workspace = ns["Workspace"].get<uintptr_t>();
        }
        if (root.contains("DepthOfFieldEffect")) {
            auto& ns = root["DepthOfFieldEffect"];
            if (ns.contains("Enabled")) Offsets::DepthOfFieldEffect::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("FarIntensity")) Offsets::DepthOfFieldEffect::FarIntensity = ns["FarIntensity"].get<uintptr_t>();
            if (ns.contains("FocusDistance")) Offsets::DepthOfFieldEffect::FocusDistance = ns["FocusDistance"].get<uintptr_t>();
            if (ns.contains("InFocusRadius")) Offsets::DepthOfFieldEffect::InFocusRadius = ns["InFocusRadius"].get<uintptr_t>();
            if (ns.contains("NearIntensity")) Offsets::DepthOfFieldEffect::NearIntensity = ns["NearIntensity"].get<uintptr_t>();
        }
        if (root.contains("DragDetector")) {
            auto& ns = root["DragDetector"];
            if (ns.contains("ActivatedCursorIcon")) Offsets::DragDetector::ActivatedCursorIcon = ns["ActivatedCursorIcon"].get<uintptr_t>();
            if (ns.contains("CursorIcon")) Offsets::DragDetector::CursorIcon = ns["CursorIcon"].get<uintptr_t>();
            if (ns.contains("MaxActivationDistance")) Offsets::DragDetector::MaxActivationDistance = ns["MaxActivationDistance"].get<uintptr_t>();
            if (ns.contains("MaxDragAngle")) Offsets::DragDetector::MaxDragAngle = ns["MaxDragAngle"].get<uintptr_t>();
            if (ns.contains("MaxDragTranslation")) Offsets::DragDetector::MaxDragTranslation = ns["MaxDragTranslation"].get<uintptr_t>();
            if (ns.contains("MaxForce")) Offsets::DragDetector::MaxForce = ns["MaxForce"].get<uintptr_t>();
            if (ns.contains("MaxTorque")) Offsets::DragDetector::MaxTorque = ns["MaxTorque"].get<uintptr_t>();
            if (ns.contains("MinDragAngle")) Offsets::DragDetector::MinDragAngle = ns["MinDragAngle"].get<uintptr_t>();
            if (ns.contains("MinDragTranslation")) Offsets::DragDetector::MinDragTranslation = ns["MinDragTranslation"].get<uintptr_t>();
            if (ns.contains("ReferenceInstance")) Offsets::DragDetector::ReferenceInstance = ns["ReferenceInstance"].get<uintptr_t>();
            if (ns.contains("Responsiveness")) Offsets::DragDetector::Responsiveness = ns["Responsiveness"].get<uintptr_t>();
        }
        if (root.contains("FakeDataModel")) {
            auto& ns = root["FakeDataModel"];
            if (ns.contains("Pointer")) Offsets::FakeDataModel::Pointer = ns["Pointer"].get<uintptr_t>();
            if (ns.contains("RealDataModel")) Offsets::FakeDataModel::RealDataModel = ns["RealDataModel"].get<uintptr_t>();
        }
        if (root.contains("GuiBase2D")) {
            auto& ns = root["GuiBase2D"];
            if (ns.contains("AbsolutePosition")) Offsets::GuiBase2D::AbsolutePosition = ns["AbsolutePosition"].get<uintptr_t>();
            if (ns.contains("AbsoluteRotation")) Offsets::GuiBase2D::AbsoluteRotation = ns["AbsoluteRotation"].get<uintptr_t>();
            if (ns.contains("AbsoluteSize")) Offsets::GuiBase2D::AbsoluteSize = ns["AbsoluteSize"].get<uintptr_t>();
        }
        if (root.contains("GuiObject")) {
            auto& ns = root["GuiObject"];
            if (ns.contains("BackgroundColor3")) Offsets::GuiObject::BackgroundColor3 = ns["BackgroundColor3"].get<uintptr_t>();
            if (ns.contains("BackgroundTransparency")) Offsets::GuiObject::BackgroundTransparency = ns["BackgroundTransparency"].get<uintptr_t>();
            if (ns.contains("BorderColor3")) Offsets::GuiObject::BorderColor3 = ns["BorderColor3"].get<uintptr_t>();
            if (ns.contains("Image")) Offsets::GuiObject::Image = ns["Image"].get<uintptr_t>();
            if (ns.contains("LayoutOrder")) Offsets::GuiObject::LayoutOrder = ns["LayoutOrder"].get<uintptr_t>();
            if (ns.contains("Position")) Offsets::GuiObject::Position = ns["Position"].get<uintptr_t>();
            if (ns.contains("RichText")) Offsets::GuiObject::RichText = ns["RichText"].get<uintptr_t>();
            if (ns.contains("Rotation")) Offsets::GuiObject::Rotation = ns["Rotation"].get<uintptr_t>();
            if (ns.contains("ScreenGui_Enabled")) Offsets::GuiObject::ScreenGui_Enabled = ns["ScreenGui_Enabled"].get<uintptr_t>();
            if (ns.contains("Size")) Offsets::GuiObject::Size = ns["Size"].get<uintptr_t>();
            if (ns.contains("Text")) Offsets::GuiObject::Text = ns["Text"].get<uintptr_t>();
            if (ns.contains("TextColor3")) Offsets::GuiObject::TextColor3 = ns["TextColor3"].get<uintptr_t>();
            if (ns.contains("Visible")) Offsets::GuiObject::Visible = ns["Visible"].get<uintptr_t>();
            if (ns.contains("ZIndex")) Offsets::GuiObject::ZIndex = ns["ZIndex"].get<uintptr_t>();
        }
        if (root.contains("Humanoid")) {
            auto& ns = root["Humanoid"];
            if (ns.contains("AutoJumpEnabled")) Offsets::Humanoid::AutoJumpEnabled = ns["AutoJumpEnabled"].get<uintptr_t>();
            if (ns.contains("AutoRotate")) Offsets::Humanoid::AutoRotate = ns["AutoRotate"].get<uintptr_t>();
            if (ns.contains("AutomaticScalingEnabled")) Offsets::Humanoid::AutomaticScalingEnabled = ns["AutomaticScalingEnabled"].get<uintptr_t>();
            if (ns.contains("BreakJointsOnDeath")) Offsets::Humanoid::BreakJointsOnDeath = ns["BreakJointsOnDeath"].get<uintptr_t>();
            if (ns.contains("CameraOffset")) Offsets::Humanoid::CameraOffset = ns["CameraOffset"].get<uintptr_t>();
            if (ns.contains("DisplayDistanceType")) Offsets::Humanoid::DisplayDistanceType = ns["DisplayDistanceType"].get<uintptr_t>();
            if (ns.contains("DisplayName")) Offsets::Humanoid::DisplayName = ns["DisplayName"].get<uintptr_t>();
            if (ns.contains("EvaluateStateMachine")) Offsets::Humanoid::EvaluateStateMachine = ns["EvaluateStateMachine"].get<uintptr_t>();
            if (ns.contains("FloorMaterial")) Offsets::Humanoid::FloorMaterial = ns["FloorMaterial"].get<uintptr_t>();
            if (ns.contains("Health")) Offsets::Humanoid::Health = ns["Health"].get<uintptr_t>();
            if (ns.contains("HealthDisplayDistance")) Offsets::Humanoid::HealthDisplayDistance = ns["HealthDisplayDistance"].get<uintptr_t>();
            if (ns.contains("HealthDisplayType")) Offsets::Humanoid::HealthDisplayType = ns["HealthDisplayType"].get<uintptr_t>();
            if (ns.contains("HipHeight")) Offsets::Humanoid::HipHeight = ns["HipHeight"].get<uintptr_t>();
            if (ns.contains("HumanoidRootPart")) Offsets::Humanoid::HumanoidRootPart = ns["HumanoidRootPart"].get<uintptr_t>();
            if (ns.contains("HumanoidState")) Offsets::Humanoid::HumanoidState = ns["HumanoidState"].get<uintptr_t>();
            if (ns.contains("HumanoidStateID")) Offsets::Humanoid::HumanoidStateID = ns["HumanoidStateID"].get<uintptr_t>();
            if (ns.contains("IsWalking")) Offsets::Humanoid::IsWalking = ns["IsWalking"].get<uintptr_t>();
            if (ns.contains("Jump")) Offsets::Humanoid::Jump = ns["Jump"].get<uintptr_t>();
            if (ns.contains("JumpHeight")) Offsets::Humanoid::JumpHeight = ns["JumpHeight"].get<uintptr_t>();
            if (ns.contains("JumpPower")) Offsets::Humanoid::JumpPower = ns["JumpPower"].get<uintptr_t>();
            if (ns.contains("MaxHealth")) Offsets::Humanoid::MaxHealth = ns["MaxHealth"].get<uintptr_t>();
            if (ns.contains("MaxSlopeAngle")) Offsets::Humanoid::MaxSlopeAngle = ns["MaxSlopeAngle"].get<uintptr_t>();
            if (ns.contains("MoveDirection")) Offsets::Humanoid::MoveDirection = ns["MoveDirection"].get<uintptr_t>();
            if (ns.contains("MoveToPart")) Offsets::Humanoid::MoveToPart = ns["MoveToPart"].get<uintptr_t>();
            if (ns.contains("MoveToPoint")) Offsets::Humanoid::MoveToPoint = ns["MoveToPoint"].get<uintptr_t>();
            if (ns.contains("NameDisplayDistance")) Offsets::Humanoid::NameDisplayDistance = ns["NameDisplayDistance"].get<uintptr_t>();
            if (ns.contains("NameOcclusion")) Offsets::Humanoid::NameOcclusion = ns["NameOcclusion"].get<uintptr_t>();
            if (ns.contains("PlatformStand")) Offsets::Humanoid::PlatformStand = ns["PlatformStand"].get<uintptr_t>();
            if (ns.contains("RequiresNeck")) Offsets::Humanoid::RequiresNeck = ns["RequiresNeck"].get<uintptr_t>();
            if (ns.contains("RigType")) Offsets::Humanoid::RigType = ns["RigType"].get<uintptr_t>();
            if (ns.contains("SeatPart")) Offsets::Humanoid::SeatPart = ns["SeatPart"].get<uintptr_t>();
            if (ns.contains("Sit")) Offsets::Humanoid::Sit = ns["Sit"].get<uintptr_t>();
            if (ns.contains("TargetPoint")) Offsets::Humanoid::TargetPoint = ns["TargetPoint"].get<uintptr_t>();
            if (ns.contains("UseJumpPower")) Offsets::Humanoid::UseJumpPower = ns["UseJumpPower"].get<uintptr_t>();
            if (ns.contains("WalkTimer")) Offsets::Humanoid::WalkTimer = ns["WalkTimer"].get<uintptr_t>();
            if (ns.contains("Walkspeed")) Offsets::Humanoid::Walkspeed = ns["Walkspeed"].get<uintptr_t>();
            if (ns.contains("WalkspeedCheck")) Offsets::Humanoid::WalkspeedCheck = ns["WalkspeedCheck"].get<uintptr_t>();
        }
        if (root.contains("Instance")) {
            auto& ns = root["Instance"];
            if (ns.contains("ChildrenEnd")) Offsets::Instance::ChildrenEnd = ns["ChildrenEnd"].get<uintptr_t>();
            if (ns.contains("ChildrenStart")) Offsets::Instance::ChildrenStart = ns["ChildrenStart"].get<uintptr_t>();
            if (ns.contains("ClassBase")) Offsets::Instance::ClassBase = ns["ClassBase"].get<uintptr_t>();
            if (ns.contains("ClassDescriptor")) Offsets::Instance::ClassDescriptor = ns["ClassDescriptor"].get<uintptr_t>();
            if (ns.contains("ClassName")) Offsets::Instance::ClassName = ns["ClassName"].get<uintptr_t>();
            if (ns.contains("Name")) Offsets::Instance::Name = ns["Name"].get<uintptr_t>();
            if (ns.contains("NameContainer")) Offsets::Instance::NameContainer = ns["NameContainer"].get<uintptr_t>();
            if (ns.contains("Parent")) Offsets::Instance::Parent = ns["Parent"].get<uintptr_t>();
            if (ns.contains("This")) Offsets::Instance::This = ns["This"].get<uintptr_t>();
        }
        if (root.contains("Lighting")) {
            auto& ns = root["Lighting"];
            if (ns.contains("Ambient")) Offsets::Lighting::Ambient = ns["Ambient"].get<uintptr_t>();
            if (ns.contains("Brightness")) Offsets::Lighting::Brightness = ns["Brightness"].get<uintptr_t>();
            if (ns.contains("ClockTime")) Offsets::Lighting::ClockTime = ns["ClockTime"].get<uintptr_t>();
            if (ns.contains("ColorShift_Bottom")) Offsets::Lighting::ColorShift_Bottom = ns["ColorShift_Bottom"].get<uintptr_t>();
            if (ns.contains("ColorShift_Top")) Offsets::Lighting::ColorShift_Top = ns["ColorShift_Top"].get<uintptr_t>();
            if (ns.contains("EnvironmentDiffuseScale")) Offsets::Lighting::EnvironmentDiffuseScale = ns["EnvironmentDiffuseScale"].get<uintptr_t>();
            if (ns.contains("EnvironmentSpecularScale")) Offsets::Lighting::EnvironmentSpecularScale = ns["EnvironmentSpecularScale"].get<uintptr_t>();
            if (ns.contains("ExposureCompensation")) Offsets::Lighting::ExposureCompensation = ns["ExposureCompensation"].get<uintptr_t>();
            if (ns.contains("FogColor")) Offsets::Lighting::FogColor = ns["FogColor"].get<uintptr_t>();
            if (ns.contains("FogEnd")) Offsets::Lighting::FogEnd = ns["FogEnd"].get<uintptr_t>();
            if (ns.contains("FogStart")) Offsets::Lighting::FogStart = ns["FogStart"].get<uintptr_t>();
            if (ns.contains("GeographicLatitude")) Offsets::Lighting::GeographicLatitude = ns["GeographicLatitude"].get<uintptr_t>();
            if (ns.contains("GlobalShadows")) Offsets::Lighting::GlobalShadows = ns["GlobalShadows"].get<uintptr_t>();
            if (ns.contains("GradientBottom")) Offsets::Lighting::GradientBottom = ns["GradientBottom"].get<uintptr_t>();
            if (ns.contains("GradientTop")) Offsets::Lighting::GradientTop = ns["GradientTop"].get<uintptr_t>();
            if (ns.contains("LightColor")) Offsets::Lighting::LightColor = ns["LightColor"].get<uintptr_t>();
            if (ns.contains("LightDirection")) Offsets::Lighting::LightDirection = ns["LightDirection"].get<uintptr_t>();
            if (ns.contains("MoonPosition")) Offsets::Lighting::MoonPosition = ns["MoonPosition"].get<uintptr_t>();
            if (ns.contains("OutdoorAmbient")) Offsets::Lighting::OutdoorAmbient = ns["OutdoorAmbient"].get<uintptr_t>();
            if (ns.contains("Sky")) Offsets::Lighting::Sky = ns["Sky"].get<uintptr_t>();
            if (ns.contains("Source")) Offsets::Lighting::Source = ns["Source"].get<uintptr_t>();
            if (ns.contains("SunPosition")) Offsets::Lighting::SunPosition = ns["SunPosition"].get<uintptr_t>();
        }
        if (root.contains("LocalScript")) {
            auto& ns = root["LocalScript"];
            if (ns.contains("ByteCode")) Offsets::LocalScript::ByteCode = ns["ByteCode"].get<uintptr_t>();
            if (ns.contains("GUID")) Offsets::LocalScript::GUID = ns["GUID"].get<uintptr_t>();
            if (ns.contains("Hash")) Offsets::LocalScript::Hash = ns["Hash"].get<uintptr_t>();
        }
        if (root.contains("MaterialColors")) {
            auto& ns = root["MaterialColors"];
            if (ns.contains("Asphalt")) Offsets::MaterialColors::Asphalt = ns["Asphalt"].get<uintptr_t>();
            if (ns.contains("Basalt")) Offsets::MaterialColors::Basalt = ns["Basalt"].get<uintptr_t>();
            if (ns.contains("Brick")) Offsets::MaterialColors::Brick = ns["Brick"].get<uintptr_t>();
            if (ns.contains("Cobblestone")) Offsets::MaterialColors::Cobblestone = ns["Cobblestone"].get<uintptr_t>();
            if (ns.contains("Concrete")) Offsets::MaterialColors::Concrete = ns["Concrete"].get<uintptr_t>();
            if (ns.contains("CrackedLava")) Offsets::MaterialColors::CrackedLava = ns["CrackedLava"].get<uintptr_t>();
            if (ns.contains("Glacier")) Offsets::MaterialColors::Glacier = ns["Glacier"].get<uintptr_t>();
            if (ns.contains("Grass")) Offsets::MaterialColors::Grass = ns["Grass"].get<uintptr_t>();
            if (ns.contains("Ground")) Offsets::MaterialColors::Ground = ns["Ground"].get<uintptr_t>();
            if (ns.contains("Ice")) Offsets::MaterialColors::Ice = ns["Ice"].get<uintptr_t>();
            if (ns.contains("LeafyGrass")) Offsets::MaterialColors::LeafyGrass = ns["LeafyGrass"].get<uintptr_t>();
            if (ns.contains("Limestone")) Offsets::MaterialColors::Limestone = ns["Limestone"].get<uintptr_t>();
            if (ns.contains("Mud")) Offsets::MaterialColors::Mud = ns["Mud"].get<uintptr_t>();
            if (ns.contains("Pavement")) Offsets::MaterialColors::Pavement = ns["Pavement"].get<uintptr_t>();
            if (ns.contains("Rock")) Offsets::MaterialColors::Rock = ns["Rock"].get<uintptr_t>();
            if (ns.contains("Salt")) Offsets::MaterialColors::Salt = ns["Salt"].get<uintptr_t>();
            if (ns.contains("Sand")) Offsets::MaterialColors::Sand = ns["Sand"].get<uintptr_t>();
            if (ns.contains("Sandstone")) Offsets::MaterialColors::Sandstone = ns["Sandstone"].get<uintptr_t>();
            if (ns.contains("Slate")) Offsets::MaterialColors::Slate = ns["Slate"].get<uintptr_t>();
            if (ns.contains("Snow")) Offsets::MaterialColors::Snow = ns["Snow"].get<uintptr_t>();
            if (ns.contains("WoodPlanks")) Offsets::MaterialColors::WoodPlanks = ns["WoodPlanks"].get<uintptr_t>();
        }
        if (root.contains("MeshContentProvider")) {
            auto& ns = root["MeshContentProvider"];
            if (ns.contains("AssetID")) Offsets::MeshContentProvider::AssetID = ns["AssetID"].get<uintptr_t>();
            if (ns.contains("Cache")) Offsets::MeshContentProvider::Cache = ns["Cache"].get<uintptr_t>();
            if (ns.contains("LRUCache")) Offsets::MeshContentProvider::LRUCache = ns["LRUCache"].get<uintptr_t>();
            if (ns.contains("MeshData")) Offsets::MeshContentProvider::MeshData = ns["MeshData"].get<uintptr_t>();
            if (ns.contains("ToMeshData")) Offsets::MeshContentProvider::ToMeshData = ns["ToMeshData"].get<uintptr_t>();
        }
        if (root.contains("MeshData")) {
            auto& ns = root["MeshData"];
            if (ns.contains("FaceEnd")) Offsets::MeshData::FaceEnd = ns["FaceEnd"].get<uintptr_t>();
            if (ns.contains("FaceStart")) Offsets::MeshData::FaceStart = ns["FaceStart"].get<uintptr_t>();
            if (ns.contains("VertexEnd")) Offsets::MeshData::VertexEnd = ns["VertexEnd"].get<uintptr_t>();
            if (ns.contains("VertexStart")) Offsets::MeshData::VertexStart = ns["VertexStart"].get<uintptr_t>();
        }
        if (root.contains("MeshPart")) {
            auto& ns = root["MeshPart"];
            if (ns.contains("MeshId")) Offsets::MeshPart::MeshId = ns["MeshId"].get<uintptr_t>();
            if (ns.contains("Texture")) Offsets::MeshPart::Texture = ns["Texture"].get<uintptr_t>();
        }
        if (root.contains("Misc")) {
            auto& ns = root["Misc"];
            if (ns.contains("Adornee")) Offsets::Misc::Adornee = ns["Adornee"].get<uintptr_t>();
            if (ns.contains("AnimationId")) Offsets::Misc::AnimationId = ns["AnimationId"].get<uintptr_t>();
            if (ns.contains("StringLength")) Offsets::Misc::StringLength = ns["StringLength"].get<uintptr_t>();
            if (ns.contains("Value")) Offsets::Misc::Value = ns["Value"].get<uintptr_t>();
        }
        if (root.contains("Model")) {
            auto& ns = root["Model"];
            if (ns.contains("PrimaryPart")) Offsets::Model::PrimaryPart = ns["PrimaryPart"].get<uintptr_t>();
            if (ns.contains("Scale")) Offsets::Model::Scale = ns["Scale"].get<uintptr_t>();
        }
        if (root.contains("ModuleScript")) {
            auto& ns = root["ModuleScript"];
            if (ns.contains("ByteCode")) Offsets::ModuleScript::ByteCode = ns["ByteCode"].get<uintptr_t>();
            if (ns.contains("GUID")) Offsets::ModuleScript::GUID = ns["GUID"].get<uintptr_t>();
            if (ns.contains("Hash")) Offsets::ModuleScript::Hash = ns["Hash"].get<uintptr_t>();
            if (ns.contains("IsCoreScript")) Offsets::ModuleScript::IsCoreScript = ns["IsCoreScript"].get<uintptr_t>();
        }
        if (root.contains("MouseService")) {
            auto& ns = root["MouseService"];
            if (ns.contains("InputObject")) Offsets::MouseService::InputObject = ns["InputObject"].get<uintptr_t>();
            if (ns.contains("InputObject2")) Offsets::MouseService::InputObject2 = ns["InputObject2"].get<uintptr_t>();
            if (ns.contains("MousePosition")) Offsets::MouseService::MousePosition = ns["MousePosition"].get<uintptr_t>();
            if (ns.contains("SensitivityPointer")) Offsets::MouseService::SensitivityPointer = ns["SensitivityPointer"].get<uintptr_t>();
        }
        if (root.contains("ParticleEmitter")) {
            auto& ns = root["ParticleEmitter"];
            if (ns.contains("Acceleration")) Offsets::ParticleEmitter::Acceleration = ns["Acceleration"].get<uintptr_t>();
            if (ns.contains("Brightness")) Offsets::ParticleEmitter::Brightness = ns["Brightness"].get<uintptr_t>();
            if (ns.contains("Drag")) Offsets::ParticleEmitter::Drag = ns["Drag"].get<uintptr_t>();
            if (ns.contains("Lifetime")) Offsets::ParticleEmitter::Lifetime = ns["Lifetime"].get<uintptr_t>();
            if (ns.contains("LightEmission")) Offsets::ParticleEmitter::LightEmission = ns["LightEmission"].get<uintptr_t>();
            if (ns.contains("LightInfluence")) Offsets::ParticleEmitter::LightInfluence = ns["LightInfluence"].get<uintptr_t>();
            if (ns.contains("Rate")) Offsets::ParticleEmitter::Rate = ns["Rate"].get<uintptr_t>();
            if (ns.contains("RotSpeed")) Offsets::ParticleEmitter::RotSpeed = ns["RotSpeed"].get<uintptr_t>();
            if (ns.contains("Rotation")) Offsets::ParticleEmitter::Rotation = ns["Rotation"].get<uintptr_t>();
            if (ns.contains("Speed")) Offsets::ParticleEmitter::Speed = ns["Speed"].get<uintptr_t>();
            if (ns.contains("SpreadAngle")) Offsets::ParticleEmitter::SpreadAngle = ns["SpreadAngle"].get<uintptr_t>();
            if (ns.contains("Texture")) Offsets::ParticleEmitter::Texture = ns["Texture"].get<uintptr_t>();
            if (ns.contains("TimeScale")) Offsets::ParticleEmitter::TimeScale = ns["TimeScale"].get<uintptr_t>();
            if (ns.contains("VelocityInheritance")) Offsets::ParticleEmitter::VelocityInheritance = ns["VelocityInheritance"].get<uintptr_t>();
            if (ns.contains("ZOffset")) Offsets::ParticleEmitter::ZOffset = ns["ZOffset"].get<uintptr_t>();
        }
        if (root.contains("Player")) {
            auto& ns = root["Player"];
            if (ns.contains("AccountAge")) Offsets::Player::AccountAge = ns["AccountAge"].get<uintptr_t>();
            if (ns.contains("CameraMode")) Offsets::Player::CameraMode = ns["CameraMode"].get<uintptr_t>();
            if (ns.contains("DisplayName")) Offsets::Player::DisplayName = ns["DisplayName"].get<uintptr_t>();
            if (ns.contains("HealthDisplayDistance")) Offsets::Player::HealthDisplayDistance = ns["HealthDisplayDistance"].get<uintptr_t>();
            if (ns.contains("LocalPlayer")) Offsets::Player::LocalPlayer = ns["LocalPlayer"].get<uintptr_t>();
            if (ns.contains("LocaleId")) Offsets::Player::LocaleId = ns["LocaleId"].get<uintptr_t>();
            if (ns.contains("MaxZoomDistance")) Offsets::Player::MaxZoomDistance = ns["MaxZoomDistance"].get<uintptr_t>();
            if (ns.contains("MinZoomDistance")) Offsets::Player::MinZoomDistance = ns["MinZoomDistance"].get<uintptr_t>();
            if (ns.contains("ModelInstance")) Offsets::Player::ModelInstance = ns["ModelInstance"].get<uintptr_t>();
            if (ns.contains("Mouse")) Offsets::Player::Mouse = ns["Mouse"].get<uintptr_t>();
            if (ns.contains("NameDisplayDistance")) Offsets::Player::NameDisplayDistance = ns["NameDisplayDistance"].get<uintptr_t>();
            if (ns.contains("Team")) Offsets::Player::Team = ns["Team"].get<uintptr_t>();
            if (ns.contains("TeamColor")) Offsets::Player::TeamColor = ns["TeamColor"].get<uintptr_t>();
            if (ns.contains("UserId")) Offsets::Player::UserId = ns["UserId"].get<uintptr_t>();
        }
        if (root.contains("PlayerConfigurer")) {
            auto& ns = root["PlayerConfigurer"];
            if (ns.contains("Pointer")) Offsets::PlayerConfigurer::Pointer = ns["Pointer"].get<uintptr_t>();
        }
        if (root.contains("PlayerMouse")) {
            auto& ns = root["PlayerMouse"];
            if (ns.contains("Icon")) Offsets::PlayerMouse::Icon = ns["Icon"].get<uintptr_t>();
            if (ns.contains("Workspace")) Offsets::PlayerMouse::Workspace = ns["Workspace"].get<uintptr_t>();
        }
        if (root.contains("Primitive")) {
            auto& ns = root["Primitive"];
            if (ns.contains("AssemblyAngularVelocity")) Offsets::Primitive::AssemblyAngularVelocity = ns["AssemblyAngularVelocity"].get<uintptr_t>();
            if (ns.contains("AssemblyLinearVelocity")) Offsets::Primitive::AssemblyLinearVelocity = ns["AssemblyLinearVelocity"].get<uintptr_t>();
            if (ns.contains("Flags")) Offsets::Primitive::Flags = ns["Flags"].get<uintptr_t>();
            if (ns.contains("Material")) Offsets::Primitive::Material = ns["Material"].get<uintptr_t>();
            if (ns.contains("Owner")) Offsets::Primitive::Owner = ns["Owner"].get<uintptr_t>();
            if (ns.contains("Position")) Offsets::Primitive::Position = ns["Position"].get<uintptr_t>();
            if (ns.contains("Rotation")) Offsets::Primitive::Rotation = ns["Rotation"].get<uintptr_t>();
            if (ns.contains("Size")) Offsets::Primitive::Size = ns["Size"].get<uintptr_t>();
            if (ns.contains("Validate")) Offsets::Primitive::Validate = ns["Validate"].get<uintptr_t>();
        }
        if (root.contains("PrimitiveFlags")) {
            auto& ns = root["PrimitiveFlags"];
            if (ns.contains("Anchored")) Offsets::PrimitiveFlags::Anchored = ns["Anchored"].get<uintptr_t>();
            if (ns.contains("CanCollide")) Offsets::PrimitiveFlags::CanCollide = ns["CanCollide"].get<uintptr_t>();
            if (ns.contains("CanQuery")) Offsets::PrimitiveFlags::CanQuery = ns["CanQuery"].get<uintptr_t>();
            if (ns.contains("CanTouch")) Offsets::PrimitiveFlags::CanTouch = ns["CanTouch"].get<uintptr_t>();
        }
        if (root.contains("ProximityPrompt")) {
            auto& ns = root["ProximityPrompt"];
            if (ns.contains("ActionText")) Offsets::ProximityPrompt::ActionText = ns["ActionText"].get<uintptr_t>();
            if (ns.contains("Enabled")) Offsets::ProximityPrompt::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("GamepadKeyCode")) Offsets::ProximityPrompt::GamepadKeyCode = ns["GamepadKeyCode"].get<uintptr_t>();
            if (ns.contains("HoldDuration")) Offsets::ProximityPrompt::HoldDuration = ns["HoldDuration"].get<uintptr_t>();
            if (ns.contains("KeyCode")) Offsets::ProximityPrompt::KeyCode = ns["KeyCode"].get<uintptr_t>();
            if (ns.contains("MaxActivationDistance")) Offsets::ProximityPrompt::MaxActivationDistance = ns["MaxActivationDistance"].get<uintptr_t>();
            if (ns.contains("ObjectText")) Offsets::ProximityPrompt::ObjectText = ns["ObjectText"].get<uintptr_t>();
            if (ns.contains("RequiresLineOfSight")) Offsets::ProximityPrompt::RequiresLineOfSight = ns["RequiresLineOfSight"].get<uintptr_t>();
        }
        if (root.contains("RenderJob")) {
            auto& ns = root["RenderJob"];
            if (ns.contains("FakeDataModel")) Offsets::RenderJob::FakeDataModel = ns["FakeDataModel"].get<uintptr_t>();
            if (ns.contains("RealDataModel")) Offsets::RenderJob::RealDataModel = ns["RealDataModel"].get<uintptr_t>();
            if (ns.contains("RenderView")) Offsets::RenderJob::RenderView = ns["RenderView"].get<uintptr_t>();
        }
        if (root.contains("RenderView")) {
            auto& ns = root["RenderView"];
            if (ns.contains("DeviceD3D11")) Offsets::RenderView::DeviceD3D11 = ns["DeviceD3D11"].get<uintptr_t>();
            if (ns.contains("LightingValid")) Offsets::RenderView::LightingValid = ns["LightingValid"].get<uintptr_t>();
            if (ns.contains("SkyValid")) Offsets::RenderView::SkyValid = ns["SkyValid"].get<uintptr_t>();
            if (ns.contains("VisualEngine")) Offsets::RenderView::VisualEngine = ns["VisualEngine"].get<uintptr_t>();
        }
        if (root.contains("RunService")) {
            auto& ns = root["RunService"];
            if (ns.contains("HeartbeatFPS")) Offsets::RunService::HeartbeatFPS = ns["HeartbeatFPS"].get<uintptr_t>();
            if (ns.contains("HeartbeatTask")) Offsets::RunService::HeartbeatTask = ns["HeartbeatTask"].get<uintptr_t>();
        }
        if (root.contains("Script")) {
            auto& ns = root["Script"];
            if (ns.contains("ByteCode")) Offsets::Script::ByteCode = ns["ByteCode"].get<uintptr_t>();
            if (ns.contains("GUID")) Offsets::Script::GUID = ns["GUID"].get<uintptr_t>();
            if (ns.contains("Hash")) Offsets::Script::Hash = ns["Hash"].get<uintptr_t>();
        }
        if (root.contains("ScriptContext")) {
            auto& ns = root["ScriptContext"];
            if (ns.contains("RequireBypass")) Offsets::ScriptContext::RequireBypass = ns["RequireBypass"].get<uintptr_t>();
        }
        if (root.contains("Seat")) {
            auto& ns = root["Seat"];
            if (ns.contains("Occupant")) Offsets::Seat::Occupant = ns["Occupant"].get<uintptr_t>();
        }
        if (root.contains("Sky")) {
            auto& ns = root["Sky"];
            if (ns.contains("MoonAngularSize")) Offsets::Sky::MoonAngularSize = ns["MoonAngularSize"].get<uintptr_t>();
            if (ns.contains("MoonTextureId")) Offsets::Sky::MoonTextureId = ns["MoonTextureId"].get<uintptr_t>();
            if (ns.contains("SkyboxBk")) Offsets::Sky::SkyboxBk = ns["SkyboxBk"].get<uintptr_t>();
            if (ns.contains("SkyboxDn")) Offsets::Sky::SkyboxDn = ns["SkyboxDn"].get<uintptr_t>();
            if (ns.contains("SkyboxFt")) Offsets::Sky::SkyboxFt = ns["SkyboxFt"].get<uintptr_t>();
            if (ns.contains("SkyboxLf")) Offsets::Sky::SkyboxLf = ns["SkyboxLf"].get<uintptr_t>();
            if (ns.contains("SkyboxOrientation")) Offsets::Sky::SkyboxOrientation = ns["SkyboxOrientation"].get<uintptr_t>();
            if (ns.contains("SkyboxRt")) Offsets::Sky::SkyboxRt = ns["SkyboxRt"].get<uintptr_t>();
            if (ns.contains("SkyboxUp")) Offsets::Sky::SkyboxUp = ns["SkyboxUp"].get<uintptr_t>();
            if (ns.contains("StarCount")) Offsets::Sky::StarCount = ns["StarCount"].get<uintptr_t>();
            if (ns.contains("SunAngularSize")) Offsets::Sky::SunAngularSize = ns["SunAngularSize"].get<uintptr_t>();
            if (ns.contains("SunTextureId")) Offsets::Sky::SunTextureId = ns["SunTextureId"].get<uintptr_t>();
        }
        if (root.contains("Sound")) {
            auto& ns = root["Sound"];
            if (ns.contains("Looped")) Offsets::Sound::Looped = ns["Looped"].get<uintptr_t>();
            if (ns.contains("PlaybackSpeed")) Offsets::Sound::PlaybackSpeed = ns["PlaybackSpeed"].get<uintptr_t>();
            if (ns.contains("RollOffMaxDistance")) Offsets::Sound::RollOffMaxDistance = ns["RollOffMaxDistance"].get<uintptr_t>();
            if (ns.contains("RollOffMinDistance")) Offsets::Sound::RollOffMinDistance = ns["RollOffMinDistance"].get<uintptr_t>();
            if (ns.contains("SoundGroup")) Offsets::Sound::SoundGroup = ns["SoundGroup"].get<uintptr_t>();
            if (ns.contains("SoundId")) Offsets::Sound::SoundId = ns["SoundId"].get<uintptr_t>();
            if (ns.contains("Volume")) Offsets::Sound::Volume = ns["Volume"].get<uintptr_t>();
        }
        if (root.contains("SpawnLocation")) {
            auto& ns = root["SpawnLocation"];
            if (ns.contains("AllowTeamChangeOnTouch")) Offsets::SpawnLocation::AllowTeamChangeOnTouch = ns["AllowTeamChangeOnTouch"].get<uintptr_t>();
            if (ns.contains("Enabled")) Offsets::SpawnLocation::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("ForcefieldDuration")) Offsets::SpawnLocation::ForcefieldDuration = ns["ForcefieldDuration"].get<uintptr_t>();
            if (ns.contains("Neutral")) Offsets::SpawnLocation::Neutral = ns["Neutral"].get<uintptr_t>();
            if (ns.contains("TeamColor")) Offsets::SpawnLocation::TeamColor = ns["TeamColor"].get<uintptr_t>();
        }
        if (root.contains("SpecialMesh")) {
            auto& ns = root["SpecialMesh"];
            if (ns.contains("MeshId")) Offsets::SpecialMesh::MeshId = ns["MeshId"].get<uintptr_t>();
            if (ns.contains("Scale")) Offsets::SpecialMesh::Scale = ns["Scale"].get<uintptr_t>();
        }
        if (root.contains("StatsItem")) {
            auto& ns = root["StatsItem"];
            if (ns.contains("Value")) Offsets::StatsItem::Value = ns["Value"].get<uintptr_t>();
        }
        if (root.contains("SunRaysEffect")) {
            auto& ns = root["SunRaysEffect"];
            if (ns.contains("Enabled")) Offsets::SunRaysEffect::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("Intensity")) Offsets::SunRaysEffect::Intensity = ns["Intensity"].get<uintptr_t>();
            if (ns.contains("Spread")) Offsets::SunRaysEffect::Spread = ns["Spread"].get<uintptr_t>();
        }
        if (root.contains("SurfaceAppearance")) {
            auto& ns = root["SurfaceAppearance"];
            if (ns.contains("AlphaMode")) Offsets::SurfaceAppearance::AlphaMode = ns["AlphaMode"].get<uintptr_t>();
            if (ns.contains("Color")) Offsets::SurfaceAppearance::Color = ns["Color"].get<uintptr_t>();
            if (ns.contains("ColorMap")) Offsets::SurfaceAppearance::ColorMap = ns["ColorMap"].get<uintptr_t>();
            if (ns.contains("EmissiveMaskContent")) Offsets::SurfaceAppearance::EmissiveMaskContent = ns["EmissiveMaskContent"].get<uintptr_t>();
            if (ns.contains("EmissiveStrength")) Offsets::SurfaceAppearance::EmissiveStrength = ns["EmissiveStrength"].get<uintptr_t>();
            if (ns.contains("EmissiveTint")) Offsets::SurfaceAppearance::EmissiveTint = ns["EmissiveTint"].get<uintptr_t>();
            if (ns.contains("MetalnessMap")) Offsets::SurfaceAppearance::MetalnessMap = ns["MetalnessMap"].get<uintptr_t>();
            if (ns.contains("NormalMap")) Offsets::SurfaceAppearance::NormalMap = ns["NormalMap"].get<uintptr_t>();
            if (ns.contains("RoughnessMap")) Offsets::SurfaceAppearance::RoughnessMap = ns["RoughnessMap"].get<uintptr_t>();
        }
        if (root.contains("TaskScheduler")) {
            auto& ns = root["TaskScheduler"];
            if (ns.contains("JobEnd")) Offsets::TaskScheduler::JobEnd = ns["JobEnd"].get<uintptr_t>();
            if (ns.contains("JobName")) Offsets::TaskScheduler::JobName = ns["JobName"].get<uintptr_t>();
            if (ns.contains("JobStart")) Offsets::TaskScheduler::JobStart = ns["JobStart"].get<uintptr_t>();
            if (ns.contains("MaxFPS")) Offsets::TaskScheduler::MaxFPS = ns["MaxFPS"].get<uintptr_t>();
            if (ns.contains("Pointer")) Offsets::TaskScheduler::Pointer = ns["Pointer"].get<uintptr_t>();
        }
        if (root.contains("Team")) {
            auto& ns = root["Team"];
            if (ns.contains("BrickColor")) Offsets::Team::BrickColor = ns["BrickColor"].get<uintptr_t>();
        }
        if (root.contains("Terrain")) {
            auto& ns = root["Terrain"];
            if (ns.contains("GrassLength")) Offsets::Terrain::GrassLength = ns["GrassLength"].get<uintptr_t>();
            if (ns.contains("MaterialColors")) Offsets::Terrain::MaterialColors = ns["MaterialColors"].get<uintptr_t>();
            if (ns.contains("WaterColor")) Offsets::Terrain::WaterColor = ns["WaterColor"].get<uintptr_t>();
            if (ns.contains("WaterReflectance")) Offsets::Terrain::WaterReflectance = ns["WaterReflectance"].get<uintptr_t>();
            if (ns.contains("WaterTransparency")) Offsets::Terrain::WaterTransparency = ns["WaterTransparency"].get<uintptr_t>();
            if (ns.contains("WaterWaveSize")) Offsets::Terrain::WaterWaveSize = ns["WaterWaveSize"].get<uintptr_t>();
            if (ns.contains("WaterWaveSpeed")) Offsets::Terrain::WaterWaveSpeed = ns["WaterWaveSpeed"].get<uintptr_t>();
        }
        if (root.contains("Textures")) {
            auto& ns = root["Textures"];
            if (ns.contains("Decal_Texture")) Offsets::Textures::Decal_Texture = ns["Decal_Texture"].get<uintptr_t>();
            if (ns.contains("Texture_Texture")) Offsets::Textures::Texture_Texture = ns["Texture_Texture"].get<uintptr_t>();
        }
        if (root.contains("Tool")) {
            auto& ns = root["Tool"];
            if (ns.contains("CanBeDropped")) Offsets::Tool::CanBeDropped = ns["CanBeDropped"].get<uintptr_t>();
            if (ns.contains("Enabled")) Offsets::Tool::Enabled = ns["Enabled"].get<uintptr_t>();
            if (ns.contains("Grip")) Offsets::Tool::Grip = ns["Grip"].get<uintptr_t>();
            if (ns.contains("ManualActivationOnly")) Offsets::Tool::ManualActivationOnly = ns["ManualActivationOnly"].get<uintptr_t>();
            if (ns.contains("RequiresHandle")) Offsets::Tool::RequiresHandle = ns["RequiresHandle"].get<uintptr_t>();
            if (ns.contains("TextureId")) Offsets::Tool::TextureId = ns["TextureId"].get<uintptr_t>();
            if (ns.contains("Tooltip")) Offsets::Tool::Tooltip = ns["Tooltip"].get<uintptr_t>();
        }
        if (root.contains("UnionOperation")) {
            auto& ns = root["UnionOperation"];
            if (ns.contains("AssetId")) Offsets::UnionOperation::AssetId = ns["AssetId"].get<uintptr_t>();
        }
        if (root.contains("UserInputService")) {
            auto& ns = root["UserInputService"];
            if (ns.contains("WindowInputState")) Offsets::UserInputService::WindowInputState = ns["WindowInputState"].get<uintptr_t>();
        }
        if (root.contains("VehicleSeat")) {
            auto& ns = root["VehicleSeat"];
            if (ns.contains("MaxSpeed")) Offsets::VehicleSeat::MaxSpeed = ns["MaxSpeed"].get<uintptr_t>();
            if (ns.contains("SteerFloat")) Offsets::VehicleSeat::SteerFloat = ns["SteerFloat"].get<uintptr_t>();
            if (ns.contains("ThrottleFloat")) Offsets::VehicleSeat::ThrottleFloat = ns["ThrottleFloat"].get<uintptr_t>();
            if (ns.contains("Torque")) Offsets::VehicleSeat::Torque = ns["Torque"].get<uintptr_t>();
            if (ns.contains("TurnSpeed")) Offsets::VehicleSeat::TurnSpeed = ns["TurnSpeed"].get<uintptr_t>();
        }
        if (root.contains("VisualEngine")) {
            auto& ns = root["VisualEngine"];
            if (ns.contains("Dimensions")) Offsets::VisualEngine::Dimensions = ns["Dimensions"].get<uintptr_t>();
            if (ns.contains("FakeDataModel")) Offsets::VisualEngine::FakeDataModel = ns["FakeDataModel"].get<uintptr_t>();
            if (ns.contains("Pointer")) Offsets::VisualEngine::Pointer = ns["Pointer"].get<uintptr_t>();
            if (ns.contains("RenderView")) Offsets::VisualEngine::RenderView = ns["RenderView"].get<uintptr_t>();
            if (ns.contains("ViewMatrix")) Offsets::VisualEngine::ViewMatrix = ns["ViewMatrix"].get<uintptr_t>();
        }
        if (root.contains("Weld")) {
            auto& ns = root["Weld"];
            if (ns.contains("Part0")) Offsets::Weld::Part0 = ns["Part0"].get<uintptr_t>();
            if (ns.contains("Part1")) Offsets::Weld::Part1 = ns["Part1"].get<uintptr_t>();
        }
        if (root.contains("WeldConstraint")) {
            auto& ns = root["WeldConstraint"];
            if (ns.contains("Part0")) Offsets::WeldConstraint::Part0 = ns["Part0"].get<uintptr_t>();
            if (ns.contains("Part1")) Offsets::WeldConstraint::Part1 = ns["Part1"].get<uintptr_t>();
        }
        if (root.contains("WindowInputState")) {
            auto& ns = root["WindowInputState"];
            if (ns.contains("CapsLock")) Offsets::WindowInputState::CapsLock = ns["CapsLock"].get<uintptr_t>();
            if (ns.contains("CurrentTextBox")) Offsets::WindowInputState::CurrentTextBox = ns["CurrentTextBox"].get<uintptr_t>();
        }
        if (root.contains("Workspace")) {
            auto& ns = root["Workspace"];
            if (ns.contains("CurrentCamera")) Offsets::Workspace::CurrentCamera = ns["CurrentCamera"].get<uintptr_t>();
            if (ns.contains("DistributedGameTime")) Offsets::Workspace::DistributedGameTime = ns["DistributedGameTime"].get<uintptr_t>();
            if (ns.contains("ReadOnlyGravity")) Offsets::Workspace::ReadOnlyGravity = ns["ReadOnlyGravity"].get<uintptr_t>();
            if (ns.contains("World")) Offsets::Workspace::World = ns["World"].get<uintptr_t>();
        }
        if (root.contains("World")) {
            auto& ns = root["World"];
            if (ns.contains("AirProperties")) Offsets::World::AirProperties = ns["AirProperties"].get<uintptr_t>();
            if (ns.contains("FallenPartsDestroyHeight")) Offsets::World::FallenPartsDestroyHeight = ns["FallenPartsDestroyHeight"].get<uintptr_t>();
            if (ns.contains("Gravity")) Offsets::World::Gravity = ns["Gravity"].get<uintptr_t>();
            if (ns.contains("Primitives")) Offsets::World::Primitives = ns["Primitives"].get<uintptr_t>();
            if (ns.contains("worldStepsPerSec")) Offsets::World::worldStepsPerSec = ns["worldStepsPerSec"].get<uintptr_t>();
        }
    }

    // Returns: 0 = no JSON (using defaults), 1 = loaded from JSON, -1 = JSON exists but parse error
    inline int LoadOffsets(const std::string& path = "data/offsets.json") {
        EnsureOffsetsFolder(path);

        std::ifstream file(path);
        if (!file.is_open()) {
            return 0;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        if (content.empty() || content.find_first_not_of(" \t\n\r") == std::string::npos) {
            return 0;
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(content);
        } catch (const std::exception& e) {
            printf("OffsetLoader: JSON parse error in %s: %s\n", path.c_str(), e.what());
            return -1;
        }

        if (j.empty() || !j.is_object()) {
            return 0;
        }

        nlohmann::json& root = j.contains("Offsets") && j["Offsets"].is_object() ? j["Offsets"] : j;

        ApplyOffsetsFromJson(root);

        g_LoadedFromJson = true;

        int jsonOffsetCount = 0;
        for (auto& [key, val] : root.items()) {
            if (val.is_object()) {
                jsonOffsetCount += static_cast<int>(val.size());
            }
        }
        printf("Offsets loaded from %s (%d/%d offsets found)\n", path.c_str(), jsonOffsetCount, 392);
        return 1;
    }

    inline nlohmann::json ParseHppContent(const std::string& content) {
        nlohmann::json result;
        std::string currentNamespace;
        int braceDepth = 0;

        std::istringstream stream(content);
        std::string line;

        while (std::getline(stream, line)) {
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            std::string trimmed = line.substr(start);

            if (trimmed.find("inline std::string ClientVersion") != std::string::npos ||
                trimmed.find("inline string ClientVersion") != std::string::npos) {
                size_t q1 = trimmed.find('"');
                size_t q2 = trimmed.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    result["ClientVersion"] = trimmed.substr(q1 + 1, q2 - q1 - 1);
                }
                continue;
            }

            if (trimmed.substr(0, 10) == "namespace " && trimmed.find('{') != std::string::npos) {
                size_t nsEnd = trimmed.find_first_of(" {", 10);
                std::string ns = trimmed.substr(10, nsEnd - 10);
                if (ns != "Offsets") {
                    currentNamespace = ns;
                    braceDepth++;
                }
                continue;
            }

            if (trimmed[0] == '}') {
                if (braceDepth > 0) {
                    braceDepth--;
                    if (braceDepth == 0) currentNamespace.clear();
                }
                continue;
            }

            if (!currentNamespace.empty() && trimmed.find("uintptr_t") != std::string::npos) {
                size_t utPos = trimmed.find("uintptr_t ");
                if (utPos == std::string::npos) continue;
                size_t nameStart = utPos + 10;
                size_t eqPos = trimmed.find(" = ", nameStart);
                size_t semiPos = trimmed.find(';', eqPos);
                if (eqPos == std::string::npos || semiPos == std::string::npos) continue;

                std::string name = trimmed.substr(nameStart, eqPos - nameStart);
                std::string valueStr = trimmed.substr(eqPos + 3, semiPos - (eqPos + 3));

                try {
                    uintptr_t value = 0;
                    if (valueStr.find("0x") == 0 || valueStr.find("0X") == 0) {
                        value = std::stoull(valueStr, nullptr, 16);
                    } else {
                        value = std::stoull(valueStr, nullptr, 10);
                    }
                    result[currentNamespace][name] = value;
                } catch (...) {}
            }
        }

        return result;
    }

#ifdef _WIN32
    inline bool FetchOffsetsFromWeb(const std::string& version = "") {
        std::string ver = version.empty() ? Offsets::ClientVersion : version;
        std::string url = "https://offsets.imtheo.lol/" + ver + "/offsets.hpp";

        printf("Fetching offsets from %s...\n", url.c_str());

        HINTERNET hInternet = InternetOpenA("natware/1.0", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
        if (!hInternet) {
            printf("Failed to initialize WinInet\n");
            return false;
        }

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
        if (!hUrl) {
            printf("Could not reach offsets server, using built-in defaults\n");
            InternetCloseHandle(hInternet);
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, nullptr);
        if (statusCode != 200) {
            printf("Offsets server returned %lu, using built-in defaults\n", statusCode);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }

        std::string content;
        char buffer[4096];
        DWORD bytesRead;
        while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            content += buffer;
        }

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (content.empty()) {
            printf("Received empty response, using built-in defaults\n");
            return false;
        }

        nlohmann::json parsed = ParseHppContent(content);
        if (parsed.empty()) {
            printf("Failed to parse remote offsets, using built-in defaults\n");
            return false;
        }

        ApplyOffsetsFromJson(parsed);

        int count = 0;
        for (auto& [key, val] : parsed.items()) {
            if (val.is_object()) count += static_cast<int>(val.size());
        }
        printf("Loaded %d offsets from web (version: %s)\n", count,
            parsed.contains("ClientVersion") ? parsed["ClientVersion"].get<std::string>().c_str() : ver.c_str());

        g_LoadedFromWeb = true;
        g_ExpectedVersion = Offsets::ClientVersion;
        return true;
    }
#endif

    inline void ApplyFFlagsFromJson(const nlohmann::json& root) {
        if (!root.contains("FFlags") || !root["FFlags"].is_object()) return;
        auto& ns = root["FFlags"];
        if (ns.contains("NextGenReplicatorEnabledWrite4"))
            FFlagOffsets::FFlags::NextGenReplicatorEnabledWrite4 = ns["NextGenReplicatorEnabledWrite4"].get<uintptr_t>();
        if (ns.contains("TargetTimeDelayFacctorTenths"))
            FFlagOffsets::FFlags::TargetTimeDelayFacctorTenths = ns["TargetTimeDelayFacctorTenths"].get<uintptr_t>();
        if (ns.contains("PartyPlayerInactivityTimeoutInSeconds"))
            FFlagOffsets::FFlags::PartyPlayerInactivityTimeoutInSeconds = ns["PartyPlayerInactivityTimeoutInSeconds"].get<uintptr_t>();
        if (ns.contains("DebugDisableTimeoutDisconnect"))
            FFlagOffsets::FFlags::DebugDisableTimeoutDisconnect = ns["DebugDisableTimeoutDisconnect"].get<uintptr_t>();
    }

#ifdef _WIN32
    inline bool FetchFFlagsFromWeb(const std::string& version = "") {
        std::string ver = version.empty() ? Offsets::ClientVersion : version;
        std::string url = "https://offsets.imtheo.lol/" + ver + "/fflags.hpp";

        printf("Fetching FFlags from %s...\n", url.c_str());

        HINTERNET hInternet = InternetOpenA("natware/1.0", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
        if (!hInternet) return false;

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
        if (!hUrl) {
            printf("Could not reach FFlags server, using built-in defaults\n");
            InternetCloseHandle(hInternet);
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, nullptr);
        if (statusCode != 200) {
            printf("FFlags server returned %lu, using built-in defaults\n", statusCode);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }

        std::string content;
        char buffer[4096];
        DWORD bytesRead;
        while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            content += buffer;
        }

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (content.empty()) {
            printf("FFlags response empty, using built-in defaults\n");
            return false;
        }

        nlohmann::json parsed = ParseHppContent(content);
        if (parsed.empty()) {
            printf("Failed to parse remote FFlags, using built-in defaults\n");
            return false;
        }

        ApplyFFlagsFromJson(parsed);

        printf("FFlags updated from web (version: %s)\n", ver.c_str());
        g_LoadedFFlagsFromWeb = true;
        return true;
    }
#endif

    inline bool WasLoadedFromJson() {
        return g_LoadedFromJson;
    }

    inline bool WasLoadedFromWeb() {
        return g_LoadedFromWeb;
    }

    inline std::string GetExpectedVersion() {
        return g_ExpectedVersion;
    }

#ifdef _WIN32
    inline std::string GetRobloxVersionFromProcess(HANDLE processHandle, uint32_t processId) {
        wchar_t exePath[MAX_PATH] = { 0 };
        DWORD size = MAX_PATH;

        if (!QueryFullProcessImageNameW(processHandle, 0, exePath, &size)) {
            return "";
        }

        std::wstring pathW(exePath);
        std::string path;
        path.reserve(pathW.size());
        for (wchar_t wc : pathW) path += static_cast<char>(wc);

        std::string marker = "version-";
        size_t pos = path.find(marker);
        if (pos == std::string::npos) {
            return "";
        }

        size_t end = path.find('\\', pos);
        if (end == std::string::npos) {
            end = path.find('/', pos);
        }
        if (end == std::string::npos) {
            return "";
        }

        return path.substr(pos, end - pos);
    }
#endif

}
