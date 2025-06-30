#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <map>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "d3d11.lib")

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global dashboard pointer for window proc
class GamingDashboard;
static GamingDashboard* g_dashboard = nullptr;

// Icon texture structure
struct IconTexture {
    ID3D11ShaderResourceView* texture = nullptr;
    int width = 0;
    int height = 0;
};

// Custom app structure
struct CustomApp {
    std::string name;
    std::string exePath;
    std::string iconPath;
    std::string windowTitle;
    int delaySeconds = 0;
    IconTexture icon;
};

// Function to load PNG files
bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
    // Load from disk into a raw RGBA buffer
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create texture
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = image_width;
    desc.Height = image_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();

    *out_width = image_width;
    *out_height = image_height;
    stbi_image_free(image_data);
    return true;
}

class GamingDashboard {
private:
    std::map<std::string, HWND> m_tabWindows;
    HWND m_currentWindow = nullptr;
    std::string m_currentTab = "";
    HWND m_dashboardHwnd = nullptr;
    HWND m_pendingDiscordWindow = nullptr;

    // Icon textures
    IconTexture m_chromeIcon;
    IconTexture m_steamIcon;
    IconTexture m_discordIcon;
    IconTexture m_controllerIcon;
    IconTexture m_settingsIcon;

    // Settings
    std::string m_chromePath = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    std::string m_steamPath = "C:\\Program Files (x86)\\Steam\\Steam.exe";
    std::string m_discordPath = "C:\\Users\\PC\\AppData\\Local\\Discord\\Update.exe --processStart Discord.exe";
    bool m_showSettings = false;
    char m_chromePathBuffer[512];
    char m_steamPathBuffer[512];
    char m_discordPathBuffer[512];

    // Custom apps
    std::vector<CustomApp> m_customApps;
    bool m_showAddApp = false;
    char m_newAppName[256] = "";
    char m_newAppPath[512] = "";
    char m_newAppIcon[512] = "";
    char m_newAppWindowTitle[256] = "";
    int m_newAppDelay = 0;

    // Sidebar width constant
    static const int SIDEBAR_WIDTH = 200;

    // Track launch states
    bool m_chromeLaunching = false;
    bool m_steamLaunching = false;
    bool m_discordLaunching = false;
    std::map<std::string, bool> m_customAppLaunching;
public:
    GamingDashboard() {
        LoadSettings();
        strcpy_s(m_chromePathBuffer, m_chromePath.c_str());
        strcpy_s(m_steamPathBuffer, m_steamPath.c_str());
        strcpy_s(m_discordPathBuffer, m_discordPath.c_str());
    }

    ~GamingDashboard() {
        // Clean up textures
        if (m_chromeIcon.texture) m_chromeIcon.texture->Release();
        if (m_steamIcon.texture) m_steamIcon.texture->Release();
        if (m_discordIcon.texture) m_discordIcon.texture->Release();
        if (m_controllerIcon.texture) m_controllerIcon.texture->Release();
        if (m_settingsIcon.texture) m_settingsIcon.texture->Release();

        // Clean up custom app icons
        for (auto& app : m_customApps) {
            if (app.icon.texture) app.icon.texture->Release();
        }
    }

    void LoadIcons() {
        // Load icon files from icons folder
        LoadTextureFromFile("icons/chrome.png", &m_chromeIcon.texture, &m_chromeIcon.width, &m_chromeIcon.height);
        LoadTextureFromFile("icons/steam.png", &m_steamIcon.texture, &m_steamIcon.width, &m_steamIcon.height);
        LoadTextureFromFile("icons/discord.png", &m_discordIcon.texture, &m_discordIcon.width, &m_discordIcon.height);
        LoadTextureFromFile("icons/controller.png", &m_controllerIcon.texture, &m_controllerIcon.width, &m_controllerIcon.height);
        LoadTextureFromFile("icons/settings.png", &m_settingsIcon.texture, &m_settingsIcon.width, &m_settingsIcon.height);

        // Load custom app icons
        for (auto& app : m_customApps) {
            if (!app.iconPath.empty()) {
                LoadTextureFromFile(app.iconPath.c_str(), &app.icon.texture, &app.icon.width, &app.icon.height);
            }
        }
    }

    void SetDashboardHwnd(HWND hwnd) { m_dashboardHwnd = hwnd; }

    // Handle window resize - only resize current window
    void OnWindowResize() {
        if (m_currentWindow && IsWindow(m_currentWindow)) {
            // Small delay to prevent resize conflicts
            Sleep(50);
            FormatWindowToFit(m_currentWindow);
        }
    }

    void LoadSettings() {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\GamingDashboard", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buffer[512];
            DWORD bufferSize = sizeof(buffer);
            if (RegQueryValueExA(hKey, "ChromePath", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
                m_chromePath = buffer;
            bufferSize = sizeof(buffer);
            if (RegQueryValueExA(hKey, "SteamPath", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
                m_steamPath = buffer;
            bufferSize = sizeof(buffer);
            if (RegQueryValueExA(hKey, "DiscordPath", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
                m_discordPath = buffer;

            // Load custom apps count
            DWORD customAppCount = 0;
            bufferSize = sizeof(DWORD);
            if (RegQueryValueExA(hKey, "CustomAppCount", NULL, NULL, (LPBYTE)&customAppCount, &bufferSize) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < customAppCount; i++) {
                    CustomApp app;
                    std::string keyName;

                    keyName = "CustomApp" + std::to_string(i) + "_Name";
                    bufferSize = sizeof(buffer);
                    if (RegQueryValueExA(hKey, keyName.c_str(), NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
                        app.name = buffer;

                    keyName = "CustomApp" + std::to_string(i) + "_Path";
                    bufferSize = sizeof(buffer);
                    if (RegQueryValueExA(hKey, keyName.c_str(), NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
                        app.exePath = buffer;

                    keyName = "CustomApp" + std::to_string(i) + "_Icon";
                    bufferSize = sizeof(buffer);
                    if (RegQueryValueExA(hKey, keyName.c_str(), NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
                        app.iconPath = buffer;

                    keyName = "CustomApp" + std::to_string(i) + "_WindowTitle";
                    bufferSize = sizeof(buffer);
                    if (RegQueryValueExA(hKey, keyName.c_str(), NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
                        app.windowTitle = buffer;

                    keyName = "CustomApp" + std::to_string(i) + "_Delay";
                    bufferSize = sizeof(DWORD);
                    RegQueryValueExA(hKey, keyName.c_str(), NULL, NULL, (LPBYTE)&app.delaySeconds, &bufferSize);

                    if (!app.name.empty() && !app.exePath.empty()) {
                        m_customApps.push_back(app);
                    }
                }
            }

            RegCloseKey(hKey);
        }
    }

    void SaveSettings() {
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\GamingDashboard", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, "ChromePath", 0, REG_SZ, (LPBYTE)m_chromePath.c_str(), m_chromePath.length() + 1);
            RegSetValueExA(hKey, "SteamPath", 0, REG_SZ, (LPBYTE)m_steamPath.c_str(), m_steamPath.length() + 1);
            RegSetValueExA(hKey, "DiscordPath", 0, REG_SZ, (LPBYTE)m_discordPath.c_str(), m_discordPath.length() + 1);

            // Save custom apps
            DWORD customAppCount = m_customApps.size();
            RegSetValueExA(hKey, "CustomAppCount", 0, REG_DWORD, (LPBYTE)&customAppCount, sizeof(DWORD));

            for (size_t i = 0; i < m_customApps.size(); i++) {
                const CustomApp& app = m_customApps[i];
                std::string keyName;

                keyName = "CustomApp" + std::to_string(i) + "_Name";
                RegSetValueExA(hKey, keyName.c_str(), 0, REG_SZ, (LPBYTE)app.name.c_str(), app.name.length() + 1);

                keyName = "CustomApp" + std::to_string(i) + "_Path";
                RegSetValueExA(hKey, keyName.c_str(), 0, REG_SZ, (LPBYTE)app.exePath.c_str(), app.exePath.length() + 1);

                keyName = "CustomApp" + std::to_string(i) + "_Icon";
                RegSetValueExA(hKey, keyName.c_str(), 0, REG_SZ, (LPBYTE)app.iconPath.c_str(), app.iconPath.length() + 1);

                keyName = "CustomApp" + std::to_string(i) + "_WindowTitle";
                RegSetValueExA(hKey, keyName.c_str(), 0, REG_SZ, (LPBYTE)app.windowTitle.c_str(), app.windowTitle.length() + 1);

                keyName = "CustomApp" + std::to_string(i) + "_Delay";
                RegSetValueExA(hKey, keyName.c_str(), 0, REG_DWORD, (LPBYTE)&app.delaySeconds, sizeof(DWORD));
            }

            RegCloseKey(hKey);
        }
    }

    HWND FindWindowByTitle(const std::string& titlePart) {
        struct FindWindowData {
            std::string titlePart;
            HWND foundWindow;
        };

        FindWindowData data;
        data.titlePart = titlePart;
        data.foundWindow = nullptr;

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            if (!IsWindowVisible(hwnd)) return TRUE;

            // Skip child windows and our own dashboard
            HWND parent = GetParent(hwnd);
            if (parent != nullptr) return TRUE;

            FindWindowData* data = (FindWindowData*)lParam;
            char title[256];
            GetWindowTextA(hwnd, title, 256);
            std::string windowTitle = title;

            if (windowTitle.find(data->titlePart) != std::string::npos) {
                data->foundWindow = hwnd;
                return FALSE;
            }
            return TRUE;
            }, (LPARAM)&data);

        return data.foundWindow;
    }

    void EmbedWindow(HWND childWindow, const std::string& tabName) {
        if (!childWindow || !m_dashboardHwnd) return;

        // Set parent and modify style
        SetParent(childWindow, m_dashboardHwnd);
        LONG style = GetWindowLong(childWindow, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
        style |= WS_CHILD;
        SetWindowLong(childWindow, GWL_STYLE, style);

        // Store the window
        m_tabWindows[tabName] = childWindow;

        // If this is the current tab, show it
        if (tabName == m_currentTab) {
            FormatWindowToFit(childWindow);
            ShowWindow(childWindow, SW_SHOW);
            m_currentWindow = childWindow;
        }
        else {
            ShowWindow(childWindow, SW_HIDE);
        }
    }

    void FormatWindowToFit(HWND window) {
        if (!window || !m_dashboardHwnd || !IsWindow(window)) return;

        RECT dashboardRect;
        GetClientRect(m_dashboardHwnd, &dashboardRect);

        int xPos = SIDEBAR_WIDTH;
        int yPos = 0;
        int width = dashboardRect.right - SIDEBAR_WIDTH;
        int height = dashboardRect.bottom;

        // Ensure minimum size
        if (width < 100) width = 100;
        if (height < 100) height = 100;

        // Position the window
        SetWindowPos(window, HWND_TOP, xPos, yPos, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Force redraw
        InvalidateRect(window, NULL, TRUE);
    }

    void SwitchToTab(const std::string& tabName) {
        // Hide current window
        if (m_currentWindow && IsWindow(m_currentWindow)) {
            ShowWindow(m_currentWindow, SW_HIDE);
        }

        m_currentTab = tabName;

        // Show new tab if it exists
        if (m_tabWindows.find(tabName) != m_tabWindows.end()) {
            HWND tabWindow = m_tabWindows[tabName];
            if (IsWindow(tabWindow)) {
                FormatWindowToFit(tabWindow);
                ShowWindow(tabWindow, SW_SHOW);
                m_currentWindow = tabWindow;
            }
        }
        else {
            m_currentWindow = nullptr;
        }
    }

    HWND LaunchAndWait(const std::string& exePath, const std::string& args, const std::string& windowTitle, int delaySeconds = 0) {
        // Check if already running first
        HWND existingWindow = FindWindowByTitle(windowTitle);
        if (existingWindow) return existingWindow;

        // Launch the application
        if (args.empty()) {
            ShellExecuteA(0, 0, exePath.c_str(), nullptr, 0, SW_SHOW);
        }
        else {
            ShellExecuteA(0, 0, exePath.c_str(), args.c_str(), 0, SW_SHOW);
        }

        // Special handling for Discord - wait for splash screen to disappear
        if (windowTitle == "Discord") {
            Sleep(4000); // Wait 4 seconds for splash screen to close
        }

        // Wait for window to appear
        for (int i = 0; i < 15; i++) {
            Sleep(1000);
            HWND window = FindWindowByTitle(windowTitle);
            if (window) {
                // Apply additional delay if specified
                if (delaySeconds > 0) {
                    Sleep(delaySeconds * 1000);
                }
                return window;
            }
        }
        return nullptr;
    }


    void Render() {
        ImGuiIO& io = ImGui::GetIO();

        // Handle pending Discord window on main thread
        if (m_pendingDiscordWindow) {
            SwitchToTab("Discord");
            EmbedWindow(m_pendingDiscordWindow, "Discord");
            m_pendingDiscordWindow = nullptr;
        }

        // Set up docking
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        // Sidebar
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(SIDEBAR_WIDTH, io.DisplaySize.y));
        ImGui::Begin("##Sidebar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));

        // Title with controller icon
        if (m_controllerIcon.texture) {
            ImGui::Image((void*)m_controllerIcon.texture, ImVec2(20, 20));
            ImGui::SameLine();
        }
        ImGui::Text("GAMING DASHBOARD");
        ImGui::Separator();
        ImGui::Spacing();

        // Chrome button with icon
        if (m_chromeIcon.texture) {
            ImGui::Image((void*)m_chromeIcon.texture, ImVec2(24, 24));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
        }
        // Show launching state or normal button
        if (m_chromeLaunching) {
            ImGui::Button("Chrome (Loading...)", ImVec2(-1, 40));
        }
        else if (ImGui::Button("Chrome", ImVec2(-1, 40))) {
            LaunchChrome();
        }

        // Steam button with icon
        if (m_steamIcon.texture) {
            ImGui::Image((void*)m_steamIcon.texture, ImVec2(24, 24));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
        }
        if (m_steamLaunching) {
            ImGui::Button("Steam (Loading...)", ImVec2(-1, 40));
        }
        else if (ImGui::Button("Steam", ImVec2(-1, 40))) {
            LaunchSteam();
        }

        // Discord button with icon
        if (m_discordIcon.texture) {
            ImGui::Image((void*)m_discordIcon.texture, ImVec2(24, 24));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
        }
        if (m_discordLaunching) {
            ImGui::Button("Discord (Loading...)", ImVec2(-1, 40));
        }
        else if (ImGui::Button("Discord", ImVec2(-1, 40))) {
            LaunchDiscord();
        }

        // Custom apps
        for (auto& app : m_customApps) {
            if (app.icon.texture) {
                ImGui::Image((void*)app.icon.texture, ImVec2(24, 24));
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
            }
            // Change this line:
            if (m_customAppLaunching[app.name]) { // <-- Use this instead of app.launching
                ImGui::Button((app.name + " (Loading...)").c_str(), ImVec2(-1, 40));
            }
            else if (ImGui::Button(app.name.c_str(), ImVec2(-1, 40))) {
                LaunchCustomApp(app);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Add App button
        if (ImGui::Button("+ Add App", ImVec2(-1, 35))) {
            m_showAddApp = true;
        }

        // Settings button with icon
        if (m_settingsIcon.texture) {
            ImGui::Image((void*)m_settingsIcon.texture, ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
        }
        if (ImGui::Button("Settings", ImVec2(-1, 35))) {
            m_showSettings = !m_showSettings;
        }

        ImGui::PopStyleColor(2);
        ImGui::End();

        // Add App Window
        if (m_showAddApp) {
            ImGui::SetNextWindowPos(ImVec2(220, 50));
            ImGui::SetNextWindowSize(ImVec2(600, 500));
            ImGui::Begin("Add Custom App", &m_showAddApp);

            ImGui::Text("App Name:");
            ImGui::InputText("##appname", m_newAppName, sizeof(m_newAppName));

            ImGui::Text("Executable Path:");
            ImGui::InputText("##apppath", m_newAppPath, sizeof(m_newAppPath));
            ImGui::SameLine();
            if (ImGui::Button("Browse##exe")) {
                // Simple file dialog would go here - for now user types path
            }

            ImGui::Text("Icon Path (PNG file):");
            ImGui::InputText("##appiconpath", m_newAppIcon, sizeof(m_newAppIcon));
            ImGui::SameLine();
            if (ImGui::Button("Browse##icon")) {
                // Simple file dialog would go here - for now user types path
            }

            ImGui::Text("Window Title (part of title to find window):");
            ImGui::InputText("##appwindowtitle", m_newAppWindowTitle, sizeof(m_newAppWindowTitle));

            ImGui::Text("Launch Delay (seconds):");
            ImGui::SliderInt("##appdelay", &m_newAppDelay, 0, 10);
            ImGui::TextWrapped("Delay Timer: Some applications need extra time after launching before they can be embedded properly. Discord, for example, shows a splash screen first. The delay gives the app time to fully initialize before embedding.");

            ImGui::Spacing();
            if (ImGui::Button("Add App")) {
                if (strlen(m_newAppName) > 0 && strlen(m_newAppPath) > 0 && strlen(m_newAppWindowTitle) > 0) {
                    CustomApp newApp;
                    newApp.name = m_newAppName;
                    newApp.exePath = m_newAppPath;
                    newApp.iconPath = m_newAppIcon;
                    newApp.windowTitle = m_newAppWindowTitle;
                    newApp.delaySeconds = m_newAppDelay;

                    // Load icon if provided
                    if (!newApp.iconPath.empty()) {
                        LoadTextureFromFile(newApp.iconPath.c_str(), &newApp.icon.texture, &newApp.icon.width, &newApp.icon.height);
                    }

                    m_customApps.push_back(newApp);
                    SaveSettings();

                    // Clear form
                    memset(m_newAppName, 0, sizeof(m_newAppName));
                    memset(m_newAppPath, 0, sizeof(m_newAppPath));
                    memset(m_newAppIcon, 0, sizeof(m_newAppIcon));
                    memset(m_newAppWindowTitle, 0, sizeof(m_newAppWindowTitle));
                    m_newAppDelay = 0;

                    m_showAddApp = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_showAddApp = false;
            }

            // Show existing custom apps with delete option
            if (!m_customApps.empty()) {
                ImGui::Separator();
                ImGui::Text("Existing Custom Apps:");
                for (size_t i = 0; i < m_customApps.size(); i++) {
                    ImGui::Text("%s", m_customApps[i].name.c_str());
                    ImGui::SameLine();
                    ImGui::PushID(i);
                    if (ImGui::Button("Delete")) {
                        if (m_customApps[i].icon.texture) {
                            m_customApps[i].icon.texture->Release();
                        }
                        m_customApps.erase(m_customApps.begin() + i);
                        SaveSettings();
                        i--; // Adjust index after deletion
                    }
                    ImGui::PopID();
                }
            }

            ImGui::End();
        }

        // Settings Window
        if (m_showSettings) {
            ImGui::SetNextWindowPos(ImVec2(220, 50));
            ImGui::SetNextWindowSize(ImVec2(600, 400));
            ImGui::Begin("Settings", &m_showSettings);

            ImGui::Text("Application Paths");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Chrome Path:");
            ImGui::InputText("##chrome", m_chromePathBuffer, sizeof(m_chromePathBuffer));

            ImGui::Text("Steam Path:");
            ImGui::InputText("##steam", m_steamPathBuffer, sizeof(m_steamPathBuffer));

            ImGui::Text("Discord Path (with args):");
            ImGui::InputText("##discord", m_discordPathBuffer, sizeof(m_discordPathBuffer));

            ImGui::Spacing();
            if (ImGui::Button("Save Settings")) {
                m_chromePath = m_chromePathBuffer;
                m_steamPath = m_steamPathBuffer;
                m_discordPath = m_discordPathBuffer;
                SaveSettings();
                m_showSettings = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                strcpy_s(m_chromePathBuffer, m_chromePath.c_str());
                strcpy_s(m_steamPathBuffer, m_steamPath.c_str());
                strcpy_s(m_discordPathBuffer, m_discordPath.c_str());
                m_showSettings = false;
            }

            ImGui::End();
        }

        // Main content area
        ImGui::SetNextWindowPos(ImVec2(SIDEBAR_WIDTH, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - SIDEBAR_WIDTH, io.DisplaySize.y));
        ImGui::Begin("##MainContent", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        if (m_currentTab.empty()) {
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x * 0.5f - 100, ImGui::GetWindowSize().y * 0.5f));
            ImGui::Text("Select an app to get started");
        }

        ImGui::End();
    }

private:
    void LaunchChrome() {
        // Check if Chrome is already embedded
        if (m_tabWindows.find("Chrome") != m_tabWindows.end()) {
            HWND chromeWindow = m_tabWindows["Chrome"];
            if (IsWindow(chromeWindow)) {
                SwitchToTab("Chrome");
                return;
            }
            else {
                // Window is dead, remove it
                m_tabWindows.erase("Chrome");
            }
        }

        // Check if Chrome is running but not embedded
        HWND existingChrome = FindWindowByTitle("Chrome");
        if (existingChrome) {
            SwitchToTab("Chrome");
            EmbedWindow(existingChrome, "Chrome");
            return;
        }

        // Launch new Chrome instance
        m_chromeLaunching = true;
        std::thread([this]() {
            HWND chromeWindow = LaunchAndWait(m_chromePath, "", "Chrome");
            m_chromeLaunching = false;
            if (chromeWindow) {
                SwitchToTab("Chrome");
                EmbedWindow(chromeWindow, "Chrome");
            }
            }).detach();
    }

    void LaunchSteam() {
        // Check if Steam is already embedded
        if (m_tabWindows.find("Steam") != m_tabWindows.end()) {
            HWND steamWindow = m_tabWindows["Steam"];
            if (IsWindow(steamWindow)) {
                SwitchToTab("Steam");
                return;
            }
            else {
                m_tabWindows.erase("Steam");
            }
        }

        // Check if Steam is running but not embedded
        HWND existingSteam = FindWindowByTitle("Steam");
        if (existingSteam) {
            SwitchToTab("Steam");
            EmbedWindow(existingSteam, "Steam");
            return;
        }

        // Launch new Steam instance
        m_steamLaunching = true;
        std::thread([this]() {
            HWND steamWindow = LaunchAndWait(m_steamPath, "", "Steam");
            m_steamLaunching = false;
            if (steamWindow) {
                SwitchToTab("Steam");
                EmbedWindow(steamWindow, "Steam");
            }
            }).detach();
    }

    void LaunchDiscord() {
        // Check if Discord is already embedded
        if (m_tabWindows.find("Discord") != m_tabWindows.end()) {
            HWND discordWindow = m_tabWindows["Discord"];
            if (IsWindow(discordWindow)) {
                SwitchToTab("Discord");
                return;
            }
            else {
                m_tabWindows.erase("Discord");
            }
        }

        // Check if Discord is running but not embedded
        HWND existingDiscord = FindWindowByTitle("Discord");
        if (existingDiscord) {
            SwitchToTab("Discord");
            EmbedWindow(existingDiscord, "Discord");
            return;
        }

        // Launch new Discord instance - EXACTLY like Chrome (no delay parameter)
        m_discordLaunching = true;
        std::thread([this]() {
            HWND discordWindow = LaunchAndWait(m_discordPath, "", "Discord"); // <-- Remove the ", 4"
            m_discordLaunching = false;
            if (discordWindow) {
                SwitchToTab("Discord");
                EmbedWindow(discordWindow, "Discord");
            }
            }).detach();
    }



    // Replace your LaunchCustomApp function with this EXACT copy of LaunchChrome:
    void LaunchCustomApp(const CustomApp& app) {
        // Check if app is already embedded
        if (m_tabWindows.find(app.name) != m_tabWindows.end()) {
            HWND appWindow = m_tabWindows[app.name];
            if (IsWindow(appWindow)) {
                SwitchToTab(app.name);
                return;
            }
            else {
                // Window is dead, remove it
                m_tabWindows.erase(app.name);
            }
        }

        // Check if app is running but not embedded
        HWND existingApp = FindWindowByTitle(app.windowTitle);
        if (existingApp) {
            SwitchToTab(app.name);
            EmbedWindow(existingApp, app.name);
            return;
        }

        // Launch new app instance - EXACT COPY of Chrome logic
        m_customAppLaunching[app.name] = true;
        std::thread([this, app]() {
            HWND appWindow = LaunchAndWait(app.exePath, "", app.windowTitle, app.delaySeconds);
            m_customAppLaunching[app.name] = false;
            if (appWindow) {
                SwitchToTab(app.name);
                EmbedWindow(appWindow, app.name);
            }
            }).detach();
    }
};

// Main code
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"Gaming Dashboard", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Gaming Dashboard", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Custom gaming theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.07f, 0.09f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.00f, 0.83f, 1.00f, 0.40f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.07f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.05f, 0.07f, 0.09f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.07f, 0.09f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.19f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.28f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.67f, 0.80f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Create dashboard instance
    GamingDashboard dashboard;
    dashboard.SetDashboardHwnd(hwnd);
    dashboard.LoadIcons();

    // Set global pointer for window proc
    g_dashboard = &dashboard;

    // Show startup message
    MessageBoxA(hwnd,
        "For best results, make sure all apps you are using are closed before opening the dashboard.\n\nThis ensures the apps embed properly into the dashboard.",
        "Gaming Dashboard - Tip",
        MB_OK | MB_ICONINFORMATION);

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render dashboard
        dashboard.Render();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.05f, 0.07f, 0.09f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    g_dashboard = nullptr;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions to setup D3D11
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();

            // Notify dashboard of resize
            if (g_dashboard) {
                g_dashboard->OnWindowResize();
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}


