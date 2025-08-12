//
// Game.cpp
//

#include "pch.h"
#include "Game.h"

extern void ExitGame() noexcept;

using namespace DirectX;
using Microsoft::WRL::ComPtr;

Game::Game() noexcept(false)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    // TODO: Provide parameters for swapchain format, depth/stencil format, and backbuffer count.
    //   Add DX::DeviceResources::c_AllowTearing to opt-in to variable rate displays.
    //   Add DX::DeviceResources::c_EnableHDR for HDR10 display.
    m_deviceResources->RegisterDeviceNotify(this);

    std::random_device rd;
    m_rng.seed(rd());
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{
    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    // Fixed timestep for gameplay: 10 Hz
    m_timer.SetFixedTimeStep(true);
    m_timer.SetTargetElapsedSeconds(0.1);

    InitAudio();
    PlayMusic();

    ResetGame();
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    Render();
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    std::ignore = timer;

    // Apply pending direction, disallow reversing directly
    auto isOpposite = [](Direction a, Direction b)
    {
        return (a == Direction::Up && b == Direction::Down) ||
               (a == Direction::Down && b == Direction::Up) ||
               (a == Direction::Left && b == Direction::Right) ||
               (a == Direction::Right && b == Direction::Left);
    };
    if (!isOpposite(m_direction, m_pendingDirection))
        m_direction = m_pendingDirection;

    // Compute next head
    POINT head = m_snake.front();
    switch (m_direction)
    {
    case Direction::Up:    --head.y; break;
    case Direction::Down:  ++head.y; break;
    case Direction::Left:  --head.x; break;
    case Direction::Right: ++head.x; break;
    }

    // Check wall collision
    if (head.x < 0 || head.y < 0 || head.x >= m_gridWidth || head.y >= m_gridHeight)
    {
        m_gameOver = true;
        StopMusic();
        PlayEffectGameOver();
        UpdateWindowTitle();
        return;
    }

    // Check self collision
    if (IsOccupied(head.x, head.y))
    {
        m_gameOver = true;
        StopMusic();
        PlayEffectGameOver();
        UpdateWindowTitle();
        return;
    }

    // Move snake
    m_snake.push_front(head);

    if (head.x == m_food.x && head.y == m_food.y)
    {
        m_grow = true;
        ++m_score;
        PlayEffectEat();
        SpawnFood();
        UpdateWindowTitle();
    }

    if (!m_grow)
    {
        m_snake.pop_back();
    }
    else
    {
        m_grow = false;
    }
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    Clear();

    m_deviceResources->PIXBeginEvent(L"Render");
    auto context = m_deviceResources->GetD3DDeviceContext();

    // Compute cell size based on current client size
    const auto vp = m_deviceResources->GetScreenViewport();
    const float cellW = vp.Width / static_cast<float>(m_gridWidth);
    const float cellH = vp.Height / static_cast<float>(m_gridHeight);

    // Use ClearView on sub-rects to draw solid cells (requires ID3D11DeviceContext1)
    ComPtr<ID3D11DeviceContext1> context1;
    context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()));

    auto rtv = m_deviceResources->GetRenderTargetView();

    auto drawCell = [&](int gx, int gy, const float color[4])
    {
        D3D11_RECT rect;
        rect.left = static_cast<LONG>(vp.TopLeftX + gx * cellW);
        rect.top = static_cast<LONG>(vp.TopLeftY + gy * cellH);
        rect.right = static_cast<LONG>(vp.TopLeftX + (gx + 1) * cellW);
        rect.bottom = static_cast<LONG>(vp.TopLeftY + (gy + 1) * cellH);
        if (context1)
        {
            context1->ClearView(rtv, color, &rect, 1);
        }
        else
        {
            // Fallback: fill full screen (rare), not ideal but keeps compatibility
            context->ClearRenderTargetView(rtv, color);
        }
    };

    const float red[4] = { 1,0,0,1 };
    const float green[4] = { 0,1,0,1 };

    // Draw food
    drawCell(m_food.x, m_food.y, red);

    // Draw snake
    for (const auto& seg : m_snake)
    {
        drawCell(seg.x, seg.y, green);
    }

    // HUD and overlays (D2D text)
    if (m_d2dRT)
    {
        m_d2dRT->BeginDraw();

        // Score HUD (top-left)
        wchar_t hud[64]{};
        swprintf_s(hud, L"Score: %d", m_score);
        D2D1_SIZE_F sz = m_d2dRT->GetSize();
        D2D1_RECT_F hudRect = D2D1::RectF(10.0f, 10.0f, sz.width - 10.0f, 40.0f);
        m_d2dRT->DrawText(hud, static_cast<UINT32>(wcslen(hud)), m_hudFormat.Get(), hudRect, m_whiteBrush.Get());

        // Game over overlay centered
        if (m_gameOver)
        {
            const wchar_t* text = L"Game Over\nSPACE to restart";
            D2D1_RECT_F center = D2D1::RectF(0.0f, 0.0f, sz.width, sz.height);
            m_d2dRT->DrawText(text, static_cast<UINT32>(wcslen(text)), m_overlayFormat.Get(), center, m_whiteBrush.Get());
        }

        HRESULT hr = m_d2dRT->EndDraw();
        std::ignore = hr;
    }

    m_deviceResources->PIXEndEvent();

    // Show the new frame.
    m_deviceResources->Present();
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    m_deviceResources->PIXBeginEvent(L"Clear");

    // Clear the views.
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();

    context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
    context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    // Set the viewport.
    const auto viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);

    m_deviceResources->PIXEndEvent();
}
#pragma endregion

#pragma region Message Handlers
// Message handlers
void Game::OnActivated()
{
    // TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
    // TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();
    if (m_musicOn)
        PlayMusic();
}

void Game::OnWindowMoved()
{
    const auto r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnDisplayChange()
{
    m_deviceResources->UpdateColorSpace();
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();

    // TODO: Game window is being resized.
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    // TODO: Change to desired default window size (note minimum size is 320x200).
    width = 800;
    height = 600;
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    // D2D/DWrite initialization
    if (!m_d2dFactory)
    {
        D2D1_FACTORY_OPTIONS opts{};
#if defined(_DEBUG)
        opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        DX::ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, m_d2dFactory.ReleaseAndGetAddressOf()));
    }

    if (!m_dwriteFactory)
    {
        DX::ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.ReleaseAndGetAddressOf())));
    }

    // Text formats
    if (!m_hudFormat)
    {
        DX::ThrowIfFailed(m_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us", m_hudFormat.ReleaseAndGetAddressOf()));
    }

    if (!m_overlayFormat)
    {
        DX::ThrowIfFailed(m_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 36.0f, L"en-us", m_overlayFormat.ReleaseAndGetAddressOf()));
        m_overlayFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_overlayFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void Game::CreateWindowSizeDependentResources()
{
    // Create a D2D render target bound to the swap chain backbuffer via DXGI surface
    m_d2dRT.Reset();
    m_whiteBrush.Reset();

    auto dxgiRT = m_deviceResources->GetRenderTarget();
    if (!dxgiRT) return;

    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    DX::ThrowIfFailed(dxgiRT->QueryInterface(IID_PPV_ARGS(surface.ReleaseAndGetAddressOf())));

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);

    DX::ThrowIfFailed(m_d2dFactory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, m_d2dRT.ReleaseAndGetAddressOf()));

    DX::ThrowIfFailed(m_d2dRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), m_whiteBrush.ReleaseAndGetAddressOf()));
}

void Game::OnDeviceLost()
{
    // Release D2D resources
    m_whiteBrush.Reset();
    m_d2dRT.Reset();
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();

    CreateWindowSizeDependentResources();
}
#pragma endregion

#pragma region Gameplay
void Game::ResetGame()
{
    m_snake.clear();
    m_snake.push_back(POINT{ m_gridWidth / 2, m_gridHeight / 2 });
    m_direction = Direction::Right;
    m_pendingDirection = Direction::Right;
    m_grow = false;
    m_gameOver = false;
    m_score = 0;

    SpawnFood();
    UpdateWindowTitle();
}

void Game::SpawnFood()
{
    std::uniform_int_distribution<int> dx(0, m_gridWidth - 1);
    std::uniform_int_distribution<int> dy(0, m_gridHeight - 1);

    POINT p{};
    do
    {
        p.x = dx(m_rng);
        p.y = dy(m_rng);
    } while (IsOccupied(p.x, p.y));

    m_food = p;
}

bool Game::IsOccupied(int x, int y) const
{
    for (const auto& seg : m_snake)
    {
        if (seg.x == x && seg.y == y)
            return true;
    }
    return false;
}

void Game::OnKeyDown(unsigned int key)
{
    if (m_gameOver)
    {
        if (key == VK_SPACE)
        {
            PlayMusic();
            ResetGame();
        }
        return;
    }

    if (key == 'M')
    {
        m_musicOn = !m_musicOn;
        if (m_musicOn) PlayMusic(); else StopMusic();
        return;
    }

    switch (key)
    {
    case VK_UP:    m_pendingDirection = Direction::Up; break;
    case VK_DOWN:  m_pendingDirection = Direction::Down; break;
    case VK_LEFT:  m_pendingDirection = Direction::Left; break;
    case VK_RIGHT: m_pendingDirection = Direction::Right; break;
    default: break;
    }
}

void Game::OnKeyUp(unsigned int /*key*/)
{
    // Not used yet
}

void Game::UpdateWindowTitle()
{
    wchar_t title[128]{};
    if (m_gameOver)
        swprintf_s(title, L"Snake - Game Over! Score: %d (SPACE to restart)", m_score);
    else
        swprintf_s(title, L"Snake - Score: %d", m_score);

    if (auto hwnd = m_deviceResources->GetWindow())
    {
        SetWindowTextW(hwnd, title);
    }
}
#pragma endregion

static void WriteWavHeader(std::vector<uint8_t>& data, int sampleRate, int channels, int bits, int samples)
{
    auto write32 = [&](int offset, uint32_t v)
    {
        data[offset + 0] = uint8_t(v & 0xFF);
        data[offset + 1] = uint8_t((v >> 8) & 0xFF);
        data[offset + 2] = uint8_t((v >> 16) & 0xFF);
        data[offset + 3] = uint8_t((v >> 24) & 0xFF);
    };
    auto write16 = [&](int offset, uint16_t v)
    {
        data[offset + 0] = uint8_t(v & 0xFF);
        data[offset + 1] = uint8_t((v >> 8) & 0xFF);
    };

    memcpy(&data[0], "RIFF", 4);
    write32(4, uint32_t(36 + samples * (bits / 8)));
    memcpy(&data[8], "WAVEfmt ", 8);
    write32(16, 16);
    write16(20, 1);
    write16(22, uint16_t(channels));
    write32(24, sampleRate);
    write32(28, sampleRate * channels * bits / 8);
    write16(32, uint16_t(channels * bits / 8));
    write16(34, uint16_t(bits));
    memcpy(&data[36], "data", 4);
    write32(40, uint32_t(samples * (bits / 8)));
}

void Game::InitAudio()
{
    // Create XAudio2 engine
    DX::ThrowIfFailed(XAudio2Create(m_xaudio.ReleaseAndGetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR));
    DX::ThrowIfFailed(m_xaudio->CreateMasteringVoice(&m_masterVoice));

    // Load WAV assets from disk
    auto loadFile = [](const wchar_t* path) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> bytes;
        FILE* f{};
        if (_wfopen_s(&f, path, L"rb") != 0 || !f)
            return bytes;
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        if (len > 0)
        {
            bytes.resize(static_cast<size_t>(len));
            fseek(f, 0, SEEK_SET);
            fread(bytes.data(), 1, bytes.size(), f);
        }
        fclose(f);
        return bytes;
    };

    auto parseWav = [](const std::vector<uint8_t>& bytes, WavData& out) -> bool
    {
        if (bytes.size() < 44) return false;
        auto rd32 = [&](size_t o) { return uint32_t(bytes[o]) | (uint32_t(bytes[o+1])<<8) | (uint32_t(bytes[o+2])<<16) | (uint32_t(bytes[o+3])<<24); };
        auto rd16 = [&](size_t o) { return uint16_t(bytes[o]) | (uint16_t(bytes[o+1])<<8); };
        if (memcmp(&bytes[0], "RIFF", 4) != 0 || memcmp(&bytes[8], "WAVE", 4) != 0) return false;
        size_t pos = 12;
        WAVEFORMATEX wfx{};
        uint32_t dataOffset = 0, dataBytes = 0;
        while (pos + 8 <= bytes.size())
        {
            uint32_t id = rd32(pos);
            uint32_t sz = rd32(pos + 4);
            pos += 8;
            if (pos + sz > bytes.size()) break;
            if (id == ' tmf') // 'fmt '
            {
                wfx.wFormatTag = rd16(pos + 0);
                wfx.nChannels = rd16(pos + 2);
                wfx.nSamplesPerSec = rd32(pos + 4);
                wfx.nAvgBytesPerSec = rd32(pos + 8);
                wfx.nBlockAlign = rd16(pos + 12);
                wfx.wBitsPerSample = rd16(pos + 14);
                // skip any extra fmt bytes
            }
            else if (id == 'atad') // 'data'
            {
                dataOffset = static_cast<uint32_t>(pos);
                dataBytes = sz;
            }
            pos += sz + (sz & 1);
        }
        if (!dataOffset || !dataBytes || wfx.nChannels == 0) return false;
        out.bytes = bytes;
        out.wfx = wfx;
        out.dataOffset = dataOffset;
        out.dataBytes = dataBytes;
        return true;
    };

    auto tryLoad = [&](const wchar_t* filename, WavData& dst)
    {
        auto bytes = loadFile(filename);
        if (!bytes.empty())
        {
            parseWav(bytes, dst);
        }
    };

    tryLoad(L"Audio/eat.wav", m_eat);
    tryLoad(L"Audio/gameover.wav", m_gameOverSnd);
    tryLoad(L"Audio/music.wav", m_music);

    // Create voices matching formats
    if (m_eat.wfx.nChannels)
    {
        DX::ThrowIfFailed(m_xaudio->CreateSourceVoice(&m_fxVoice, &m_eat.wfx));
    }
    else
    {
        // fallback SFX voice 8-bit mono 22050 to avoid null
        WAVEFORMATEX wfx{}; wfx.wFormatTag = WAVE_FORMAT_PCM; wfx.nChannels=1; wfx.nSamplesPerSec=22050; wfx.wBitsPerSample=8; wfx.nBlockAlign=1; wfx.nAvgBytesPerSec=22050;
        DX::ThrowIfFailed(m_xaudio->CreateSourceVoice(&m_fxVoice, &wfx));
    }

    if (m_music.wfx.nChannels)
    {
        DX::ThrowIfFailed(m_xaudio->CreateSourceVoice(&m_musicVoice, &m_music.wfx));
    }
    else
    {
        // fallback music voice
        WAVEFORMATEX wfx{}; wfx.wFormatTag = WAVE_FORMAT_PCM; wfx.nChannels=1; wfx.nSamplesPerSec=22050; wfx.wBitsPerSample=8; wfx.nBlockAlign=1; wfx.nAvgBytesPerSec=22050;
        DX::ThrowIfFailed(m_xaudio->CreateSourceVoice(&m_musicVoice, &wfx));
    }
}

void Game::PlayEffectEat()
{
    if (!m_fxVoice || m_eat.dataBytes == 0) return;
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = m_eat.dataBytes;
    buf.pAudioData = m_eat.bytes.data() + m_eat.dataOffset;
    DX::ThrowIfFailed(m_fxVoice->Stop());
    DX::ThrowIfFailed(m_fxVoice->FlushSourceBuffers());
    DX::ThrowIfFailed(m_fxVoice->SubmitSourceBuffer(&buf));
    DX::ThrowIfFailed(m_fxVoice->Start());
}

void Game::PlayEffectGameOver()
{
    if (!m_fxVoice || m_gameOverSnd.dataBytes == 0) return;
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = m_gameOverSnd.dataBytes;
    buf.pAudioData = m_gameOverSnd.bytes.data() + m_gameOverSnd.dataOffset;
    DX::ThrowIfFailed(m_fxVoice->Stop());
    DX::ThrowIfFailed(m_fxVoice->FlushSourceBuffers());
    DX::ThrowIfFailed(m_fxVoice->SubmitSourceBuffer(&buf));
    DX::ThrowIfFailed(m_fxVoice->Start());
}

void Game::PlayMusic()
{
    if (!m_musicOn || !m_musicVoice || m_music.dataBytes == 0) return;
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = m_music.dataBytes;
    buf.pAudioData = m_music.bytes.data() + m_music.dataOffset;
    buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    DX::ThrowIfFailed(m_musicVoice->Stop());
    DX::ThrowIfFailed(m_musicVoice->FlushSourceBuffers());
    DX::ThrowIfFailed(m_musicVoice->SubmitSourceBuffer(&buf));
    DX::ThrowIfFailed(m_musicVoice->Start());
}

void Game::StopMusic()
{
    if (m_musicVoice)
    {
        std::ignore = m_musicVoice->Stop();
        std::ignore = m_musicVoice->FlushSourceBuffers();
    }
}
