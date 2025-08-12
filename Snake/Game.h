//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"

#include <memory>
#include <deque>
#include <random>
#include <vector>

// A basic game implementation that creates a D3D11 device and
// provides a game loop.
class Game final : public DX::IDeviceNotify
{
public:

    Game() noexcept(false);
    ~Game() = default;

    Game(Game&&) = default;
    Game& operator= (Game&&) = default;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(HWND window, int width, int height);

    // Basic game loop
    void Tick();

    // IDeviceNotify
    void OnDeviceLost() override;
    void OnDeviceRestored() override;

    // Messages
    void OnActivated();
    void OnDeactivated();
    void OnSuspending();
    void OnResuming();
    void OnWindowMoved();
    void OnDisplayChange();
    void OnWindowSizeChanged(int width, int height);

    // Properties
    void GetDefaultSize( int& width, int& height ) const noexcept;

    // Input
    void OnKeyDown(unsigned int key);
    void OnKeyUp(unsigned int key);

private:

    void Update(DX::StepTimer const& timer);
    void Render();

    void Clear();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

    // Gameplay helpers
    enum class Direction { Up, Down, Left, Right };

    void ResetGame();
    void SpawnFood();
    bool IsOccupied(int x, int y) const;
    void UpdateWindowTitle();

    // Audio helpers (XAudio2)
    struct WavData
    {
        std::vector<uint8_t> bytes;
        WAVEFORMATEX         wfx{};
        uint32_t             dataOffset = 0;
        uint32_t             dataBytes = 0;
    };

    void InitAudio();
    void PlayMusic();
    void StopMusic();
    void PlayEffectEat();
    void PlayEffectGameOver();

    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer                           m_timer;

    // Grid and game state
    int                                     m_gridWidth = 40;
    int                                     m_gridHeight = 30;

    std::deque<POINT>                       m_snake;
    POINT                                   m_food{ 0, 0 };
    Direction                               m_direction = Direction::Right;
    Direction                               m_pendingDirection = Direction::Right;
    bool                                    m_grow = false;
    bool                                    m_gameOver = false;
    int                                     m_score = 0;

    std::mt19937                            m_rng;

    // XAudio2 state
    Microsoft::WRL::ComPtr<IXAudio2>        m_xaudio;
    IXAudio2MasteringVoice*                 m_masterVoice = nullptr;
    IXAudio2SourceVoice*                    m_musicVoice = nullptr;
    IXAudio2SourceVoice*                    m_fxVoice = nullptr; // simple single-channel for sfx

    WavData                                 m_eat;
    WavData                                 m_gameOverSnd;
    WavData                                 m_music;

    bool                                    m_musicOn = true;

    // Direct2D/DirectWrite for HUD/overlay
    Microsoft::WRL::ComPtr<ID2D1Factory1>   m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory>  m_dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> m_d2dRT; // D2D on DXGI surface via DXGI target
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_whiteBrush;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_hudFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_overlayFormat;
};
