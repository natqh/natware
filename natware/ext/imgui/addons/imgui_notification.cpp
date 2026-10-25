#include "imgui_notification.h"
#include "imgui_addons.h"
#include <cstdarg>
#include <cstdio>

void ImNotify::Print(NotifyLevel type/*, ImTextureID icon*/, const char* text, ...)
{
    va_list args;
    va_start(args, text);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), text, args);
    va_end(args);
    
    mNotifications.emplace_back(buffer);
    mNotificationsType.push_back(type);
    mNotificationTimers.push_back(fWaitTime);
}

void ImNotify::Update()
{
    if (!mNotifications.empty())
    {
        for (size_t i = 0; i < mNotificationTimers.size(); i++)
        {
            mNotificationTimers[i] -= ImGui::GetIO().DeltaTime;
            if (mNotificationTimers[i] <= 0.0f)
            {
                mNotifications.erase(mNotifications.begin() + i);
                //mNotificationsIcon.erase(mNotificationsIcon.begin() + i);
                mNotificationsType.erase(mNotificationsType.begin() + i);
                mNotificationTimers.erase(mNotificationTimers.begin() + i);
                i--;
            }
        }
    }
}

void ImNotify::Render()
{
    if (mNotifications.empty())
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiIO& io = g.IO;

    float windowHeight = ImGui::GetFontSize() + style.ChildPadding.y * 2.0f;
    float spacing = 8.0f;
    float windowPosY = spacing;

    for (int i = static_cast<int>(mNotifications.size()) - 1; i >= 0; i--)
    {
        if (i >= static_cast<int>(mNotifications.size()) || i < 0)
            continue;

        const auto& notification = mNotifications[i];
        NotifyLevel type = mNotificationsType[i];
        ImVec4 accentColor;
        ImVec4 textColor;

        switch (type)
        {
        case (NotifyLevel::Info):
        {
            accentColor = ImAdd::HexToColorVec4(0x40A2ED, 1.0f);
            textColor = ImAdd::HexToColorVec4(0xffffff, 1.0f);
            break;
        }
        case (NotifyLevel::Success):
        {
            accentColor = ImAdd::HexToColorVec4(0x15961e, 1.0f);
            textColor = ImAdd::HexToColorVec4(0xffffff, 1.0f);
            break;
        }
        case (NotifyLevel::Warn):
        {
            accentColor = ImAdd::HexToColorVec4(0xED9B40, 1.0f);
            textColor = ImAdd::HexToColorVec4(0xffffff, 1.0f);
            break;
        }
        case (NotifyLevel::Error):
        {
            accentColor = ImAdd::HexToColorVec4(0xD64550, 1.0f);
            textColor = ImAdd::HexToColorVec4(0xffffff, 1.0f);
            break;
        }
        default:
        {
            accentColor = ImAdd::HexToColorVec4(0xFFE8ED, 1.0f);
            textColor = ImAdd::HexToColorVec4(0xffffff, 1.0f);
            break;
        }
        }

        ImVec2 notification_size = ImVec2(ImGui::CalcTextSize(notification.c_str()).x + style.ChildPadding.x * 2.0f + 4.0f, windowHeight);

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - notification_size.x - spacing, io.DisplaySize.y - windowPosY - notification_size.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(notification_size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::SetNextWindowBgAlpha(1.0f);
        
        char window_id[64];
        snprintf(window_id, sizeof(window_id), "Notification##%d", i);
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImAdd::HexToColorVec4(0x181818, 1.0f));
        if (ImGui::Begin(window_id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            ImDrawList* pDrawList = window->DrawList;
            
            auto pos = window->Pos;
            auto size = window->Size;
            
            float Delta = (mNotificationTimers[i] / fWaitTime);

            ImU32 bgColor = ImGui::GetColorU32(ImAdd::HexToColorVec4(0x181818, 1.0f));
            ImU32 borderColor = ImGui::GetColorU32(ImAdd::HexToColorVec4(0x000000, 1.0f));
            ImU32 accentColorFaded = ImGui::GetColorU32(ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.2f));
            ImU32 textColorU32 = ImGui::GetColorU32(textColor);

            pDrawList->AddRectFilled(pos, pos + size, bgColor, style.ChildRounding);
            
            if (style.WindowBorderSize > 0)
            {
                pDrawList->AddRect(pos, pos + size, borderColor, style.ChildRounding);
            }

            float progressWidth = size.x * (1.0f - Delta);
            if (progressWidth > 0.0f)
            {
                pDrawList->AddRectFilled(pos, pos + ImVec2(progressWidth, size.y), accentColorFaded, style.ChildRounding);
            }

            ImVec2 textPos = pos + ImVec2(style.ChildPadding.x + 2.0f, size.y / 2.0f - ImGui::GetFontSize() / 2.0f);
            pDrawList->AddText(textPos, textColorU32, notification.c_str());
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        windowPosY += windowHeight + spacing;
    }
}