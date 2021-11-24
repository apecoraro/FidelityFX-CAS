//CAS Sample
//
// Copyright(c) 2020 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "stdafx.h"

#include "CAS_Sample.h"

#if _DEBUG
const bool VALIDATION_ENABLED = true;
#else
const bool VALIDATION_ENABLED = false;
#endif

CAS_Sample::CAS_Sample(LPCSTR name) : FrameworkWindows(name)
{
    m_time = 0;
    m_bPlay = true;

    m_pGltfLoader = NULL;
}

void CAS_Sample::OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight)
{
    // set some default values
    *pWidth = 1920;
    *pHeight = 1080;
    m_VsyncEnabled = false;
    m_isCpuValidationLayerEnabled = true;
    m_isGpuValidationLayerEnabled = false;
    m_stablePowerState = false;

    // TODO do we need to support json config file like other samples?
}

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void CAS_Sample::OnCreate()
{
    //init the shader compiler
    InitDirectXCompiler();
    CreateShaderCache();

    // Create a instance of the renderer and initialize it, we need to do that for each GPU
    //
    m_pNode = new CAS_Renderer();
    m_pNode->OnCreate(&m_device, &m_swapChain);

    // init GUI (non gfx stuff)
    //
    ImGUI_Init((void*)m_windowHwnd);

    // Init Camera, looking at the origin
    //
    m_roll = 0.0f;
    m_pitch = 0.0f;
    m_distance = 3.5f;

    // init GUI state
    m_state.toneMapper = 0;
    m_state.skyDomeType = 0;
    m_state.exposure = 1.0f;
    m_state.iblFactor = 2.0f;
    m_state.emmisiveFactor = 1.0f;
    m_state.bDrawLightFrustum = false;
    m_state.bDrawBoundingBoxes = false;
    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0, 0, 0, 0));

    // NOTE: Init render width/height and display mode
    m_state.usePackedMath = false;
    m_state.CASState = CAS_State_NoCas;
    m_state.renderWidth = 0;
    m_state.renderHeight = 0;
    m_state.sharpenControl = 0.0f;
    m_state.profiling = false;

    m_state.spotlightCount = 1;

    m_state.spotlight[0].intensity = 10.0f;
    m_state.spotlight[0].color = math::Vector4(1.0f, 1.0f, 1.0f, 0.0f);
    m_state.spotlight[0].light.SetFov(XM_PI / 2.0f, 1024, 1024, 0.1f, 100.0f);
    m_state.spotlight[0].light.LookAt(XM_PI / 2.0f, 0.58f, 3.5f, math::Vector4(0, 0, 0, 0));

    // Init profiling state
    m_CASTimingsCurrId = 0;
    m_CASAvgTiming = 0;
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void CAS_Sample::OnDestroy()
{
    ImGUI_Shutdown();

    m_device.GPUFlush();

    if (m_pNode)
    {
        m_pNode->UnloadScene();
        m_pNode->OnDestroyWindowSizeDependentResources();
        m_pNode->OnDestroy();
        delete m_pNode;
        m_pNode = nullptr;
    }

    //shut down the shader compiler
    DestroyShaderCache(&m_device);

    if (m_pGltfLoader)
    {
        delete m_pGltfLoader;
        m_pGltfLoader = nullptr;
    }
}

//--------------------------------------------------------------------------------------
//
// OnEvent
//
//--------------------------------------------------------------------------------------
bool CAS_Sample::OnEvent(MSG msg)
{
    if (ImGUI_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam))
        return true;

    return true;
}

//--------------------------------------------------------------------------------------
//
// OnResize
//
//--------------------------------------------------------------------------------------
void CAS_Sample::OnResize(bool resizeRender)
{
    if (resizeRender && m_Width && m_Height)
    {
        if (m_pNode != nullptr)
        {
            m_pNode->OnDestroyWindowSizeDependentResources();
        }

        // NOTE: Reset render width/height and display mode
        {
            std::vector<ResolutionInfo> supportedResolutions = {};
            CAS_Filter::GetSupportedResolutions(m_Width, m_Height, supportedResolutions);

            if (supportedResolutions.size() > 0)
            {
                if (m_curResolutionIndex >= supportedResolutions.size())
                    m_curResolutionIndex = 0u;

                m_state.renderWidth = supportedResolutions[m_curResolutionIndex].Width;
                m_state.renderHeight = supportedResolutions[m_curResolutionIndex].Height;
            }
        }

        if (m_pNode != nullptr)
        {
            m_pNode->OnCreateWindowSizeDependentResources(&m_swapChain, &m_state, m_Width, m_Height);
        }
    }
    m_state.camera.SetFov(AMD_PI_OVER_4, m_Width, m_Height, 0.1f, 1000.0f);
}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------

void CAS_Sample::OnRender()
{
    // Do any start of frame stuff.
    BeginFrame();

    // Build UI and set the scene state. Note that the rendering of the UI happens later.
    //
    ImGUI_UpdateIO();
    ImGui::NewFrame();

    static int loadingStage = 0;
    if (loadingStage >= 0)
    {
        // LoadScene needs to be called a number of times, the scene is not fully loaded until it returns -1
        // This is done so we can display a progress bar when the scene is loading
        if (m_pGltfLoader == NULL)
        {
            m_pGltfLoader = new GLTFCommon();
            m_pGltfLoader->Load("..\\media\\DamagedHelmet\\glTF\\", "DamagedHelmet.gltf");
            loadingStage = 0;
        }
        loadingStage = m_pNode->LoadScene(m_pGltfLoader, loadingStage);
    }
    else
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.FrameBorderSize = 1.0f;

        bool opened = true;
        ImGui::Begin("Stats", &opened);

        if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Resolution       : %ix%i", m_Width, m_Height);
        }

        if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Play", &m_bPlay);
            ImGui::SliderFloat("Time", &m_time, 0, 30);
        }

        if (ImGui::CollapsingHeader("Model Selection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* models[] = { "busterDrone", "BoomBox", "SciFiHelmet", "DamagedHelmet", "Sponza", "MetalRoughSpheres" };
            static int selected = 3;
            if (ImGui::Combo("model", &selected, models, _countof(models)))
            {
                {
                    //free resources, unload the current scene, and load new scene...
                    m_device.GPUFlush();

                    m_pNode->UnloadScene();
                    m_pNode->OnDestroyWindowSizeDependentResources();
                    m_pGltfLoader->Unload();
                    m_pNode->OnDestroy();
                    m_pNode->OnCreate(&m_device, &m_swapChain);
                    m_pNode->OnCreateWindowSizeDependentResources(&m_swapChain, &m_state, m_Width, m_Height);
                }

                m_pGltfLoader = new GLTFCommon();
                bool res = false;
                switch (selected)
                {
                case 0:
                    m_state.iblFactor = 1.0f;
                    m_roll = 0.0f; m_pitch = 0.0f; m_distance = 3.5f;
                    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0, 0, 0, 0));
                    res = m_pGltfLoader->Load("..\\media\\buster_drone\\", "busterDrone.gltf"); break;
                case 1:
                    m_state.iblFactor = 1.0f;
                    m_roll = 0.0f; m_pitch = 0.0f; m_distance = 3.5f;
                    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0, 0, 0, 0));
                    res = m_pGltfLoader->Load("..\\media\\BoomBox\\glTF\\", "BoomBox.gltf"); break;
                case 2:
                    m_state.iblFactor = 1.0f;
                    m_roll = 0.0f; m_pitch = 0.0f; m_distance = 3.5f;
                    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0, 0, 0, 0));
                    res = m_pGltfLoader->Load("..\\media\\SciFiHelmet\\glTF\\", "SciFiHelmet.gltf"); break;
                case 3:
                    m_state.iblFactor = 2.0f;
                    m_roll = 0.0f; m_pitch = 0.0f; m_distance = 3.5f;
                    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0, 0, 0, 0));
                    res = m_pGltfLoader->Load("..\\media\\DamagedHelmet\\glTF\\", "DamagedHelmet.gltf"); break;
                case 4:
                    m_state.iblFactor = 0.362f;
                    m_pitch = 0.182035938f; m_roll = 1.92130506f; m_distance = 4.83333349f;
                    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0.703276634f, 1.02280307f, 0.218072295f, 0));
                    res = m_pGltfLoader->Load("..\\media\\sponza\\gltf\\", "sponza.gltf"); break;
                case 5:
                    m_state.iblFactor = 1.0f;
                    m_roll = 0.0f; m_pitch = 0.0f; m_distance = 16.0f;
                    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0, 0, 0, 0));
                    res = m_pGltfLoader->Load("..\\media\\MetalRoughSpheres\\glTF\\", "MetalRoughSpheres.gltf"); break;
                }

                if (res == false)
                {
                    ImGui::OpenPopup("Error");

                    delete m_pGltfLoader;
                    m_pGltfLoader = NULL;
                }
                else
                {
                    loadingStage = m_pNode->LoadScene(m_pGltfLoader, 0);
                }

                ImGui::End();
                ImGui::EndFrame();
                return;
            }
        }

        std::vector<ResolutionInfo> supportedResolutions = {};
        CAS_Filter::GetSupportedResolutions(m_Width, m_Height, supportedResolutions);
        auto itemsGetter = [](void* data, int idx, const char** outText)
        {
            ResolutionInfo* resolutions = reinterpret_cast<ResolutionInfo*>(data);
            *outText = resolutions[idx].pName;
            return true;
        };

        m_prevResolutionIndex = m_curResolutionIndex;
        ImGui::Combo("Render Dim", reinterpret_cast<int*>(&m_curResolutionIndex), itemsGetter, reinterpret_cast<void*>(&supportedResolutions.front()), static_cast<int>(supportedResolutions.size()));

        if (m_device.IsFp16Supported())
        {
            ImGui::Checkbox("Enable Packed Math", &m_state.usePackedMath);
        }

        int oldCasState = (int)m_state.CASState;
        const char* casItemNames[] =
        {
            "No Cas",
            "Cas Upsample",
            "Cas Sharpen Only",
        };
        ImGui::Combo("Cas Options", (int*)&m_state.CASState, casItemNames, _countof(casItemNames));

        ImGuiIO& io = ImGui::GetIO();
        if (io.KeysDownDuration['Q'] == 0.0f)
        {
            m_state.CASState = CAS_State_NoCas;
        }
        else if (io.KeysDownDuration['W'] == 0.0f)
        {
            m_state.CASState = CAS_State_Upsample;
        }
        else if (io.KeysDownDuration['E'] == 0.0f)
        {
            m_state.CASState = CAS_State_SharpenOnly;
        }

        if (m_prevResolutionIndex != m_curResolutionIndex || oldCasState != m_state.CASState)
        {
            m_state.renderWidth = supportedResolutions[m_curResolutionIndex].Width;
            m_state.renderHeight = supportedResolutions[m_curResolutionIndex].Height;

            m_device.GPUFlush();
            m_pNode->OnDestroyWindowSizeDependentResources();
            m_pNode->OnCreateWindowSizeDependentResources(&m_swapChain, &m_state, m_Width, m_Height);
        }

        float NewCASSharpen = m_state.sharpenControl;
        ImGui::SliderFloat("Cas Sharpen", &NewCASSharpen, 0, 1);

        if (NewCASSharpen != m_state.sharpenControl)
        {
            m_state.sharpenControl = NewCASSharpen;
            m_pNode->UpdateCASSharpness(m_state.sharpenControl, m_state.CASState);
        }

        const char * cameraControl[] = { "WASD", "Orbit" };
        static int cameraControlSelected = 1;
        ImGui::Combo("Camera", &cameraControlSelected, cameraControl, _countof(cameraControl));

        float CASTime = 0.0f;
        if (ImGui::CollapsingHeader("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::vector<TimeStamp> timeStamps = m_pNode->GetTimingValues();
            if (timeStamps.size() > 0)
            {
                for (uint32_t i = 1; i < timeStamps.size() - 1; i++)
                {
                    float DeltaTime = timeStamps[i].m_microseconds;
                    ImGui::Text("%-17s: %7.1f us", timeStamps[i].m_label.c_str(), DeltaTime);
                    if (strcmp("CAS", timeStamps[i].m_label.c_str()) == 0)
                    {
                        CASTime = DeltaTime;
                    }
                }

                //scrolling data and average computing
                static float values[128] = { 0.0f };
                float minTotal = FLT_MAX;
                float maxTotal = -1.0f;
                // Copy previous total times one element to the left.
                for (uint32_t i = 0; i < 128 - 1; i++)
                {
                    values[i] = values[i + 1];
                    if (values[i] < minTotal)
                        minTotal = values[i];
                    if (values[i] > maxTotal)
                        maxTotal = values[i];
                }
                // Store current total time at end.
                values[127] = timeStamps.back().m_microseconds;

                // round down to nearest 1000.0f
                float rangeStart = static_cast<uint32_t>(minTotal / 1000.0f) * 1000.0f;
                // round maxTotal up to nearest 10,000.0f
                float rangeStop = (static_cast<uint32_t>(maxTotal / 10000.0f) * 10000.0f) + 10000.0f;

                ImGui::PlotLines("", values, 128, 0, "", rangeStart, rangeStop, ImVec2(0, 80));
            }
        }

        if (m_state.profiling)
        {
            m_CASTimings[m_CASTimingsCurrId++] = CASTime;
            if (m_CASTimingsCurrId == _countof(m_CASTimings))
            {
                m_state.profiling = false;
                m_CASTimingsCurrId = 0;
                for (int i = 0; i < _countof(m_CASTimings); ++i)
                {
                    m_CASAvgTiming += m_CASTimings[i];
                }

                m_CASAvgTiming /= static_cast<float>(_countof(m_CASTimings));
            }
        }

        bool isHit = ImGui::Button("Update Avg", { 150, 30 });
        if (isHit)
        {
            m_state.profiling = true;
        }
        ImGui::Text("Avg Cas Time: %f", m_CASAvgTiming);

#ifdef USE_VMA
        if (ImGui::Button("Save VMA json"))
        {
            char *pJson;
            vmaBuildStatsString(m_device.GetAllocator(), &pJson, VK_TRUE);

            static char filename[256];
            time_t now = time(NULL);
            tm buf;
            localtime_s(&buf, &now);
            strftime(filename, sizeof(filename), "VMA_%Y%m%d_%H%M%S.json", &buf);
            std::ofstream ofs(filename, std::ofstream::out);
            ofs << pJson;
            ofs.close();
            vmaFreeStatsString(m_device.GetAllocator(), pJson);
        }
#endif

        ImGui::End();

        // If the mouse was not used by the GUI then it's for the camera
        //
        if (io.WantCaptureMouse == false)
        {
            if ((io.KeyCtrl == false) && (io.MouseDown[0] == true))
            {
                m_roll -= io.MouseDelta.x / 100.f;
                m_pitch += io.MouseDelta.y / 100.f;
            }

            // Choose camera movement depending on setting
            //

            if (cameraControlSelected == 0)
            {
                //  WASD
                //
                m_state.camera.UpdateCameraWASD(m_roll, m_pitch, io.KeysDown, io.DeltaTime);
            }
            else if (cameraControlSelected == 1)
            {
                //  Orbiting
                //
                m_distance -= static_cast<float>(io.MouseWheel) / 3.0f;
                m_distance = std::max<float>(m_distance, 0.1f);

                bool panning = (io.KeyCtrl == true) && (io.MouseDown[0] == true);

                m_state.camera.UpdateCameraPolar(m_roll, m_pitch, panning ? -io.MouseDelta.x / 100.0f : 0.0f, panning ? io.MouseDelta.y / 100.0f : 0.0f, m_distance);
            }
        }
    }

    // Set animation time
    //
    if (m_bPlay)
    {
        m_time += static_cast<float>(m_deltaTime) / 1000.0f;
    }

    // Animate and transform the scene
    //
    if (m_pGltfLoader)
    {
        m_pGltfLoader->SetAnimationTime(0, m_time);
        m_pGltfLoader->TransformScene(0, math::Matrix4::identity());
    }

    m_state.time = m_time;

    // Do Render frame using AFR
    //
    m_pNode->OnRender(&m_state, &m_swapChain);

    m_swapChain.Present();
}


//--------------------------------------------------------------------------------------
//
// WinMain
//
//--------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    LPCSTR Name = "CAS VK Sample v1.0";

    // create new Vulkan sample
    return RunFramework(hInstance, lpCmdLine, nCmdShow, new CAS_Sample(Name));
}

