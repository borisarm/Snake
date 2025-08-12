# Snake game
This is a simple Snake game implemented in C++ using DirectX 11.
The snake grows as it eats food. The player controls direction with the arrow keys. The game ends on self-collision or hitting the window bounds.

Project guidance
- Language/SDK: C++17, Win32, DirectX 11, WRL ComPtr.
- Rendering:
  - Back buffer format: DXGI_FORMAT_B8G8R8A8_UNORM; depth: DXGI_FORMAT_D32_FLOAT.
  - DXGI flip-model swap chain (IDXGISwapChain1) with 2 back buffers.
  - Maintain viewport and render/depth views in DeviceResources.
  - Optional HDR/tearing via DeviceResources options and UpdateColorSpace.
- Input: Arrow keys for movement; SPACE to restart after game over; M toggles music.
- Audio: XAudio2 engine with a mastering voice; one source voice for looped music, one for SFX. WAV assets loaded from Snake/Audio (music.wav, eat.wav, gameover.wav).
- Conventions:
  - RAII; use Microsoft::WRL::ComPtr for COM objects.
  - Mark trivial accessors/helpers noexcept where applicable.
  - Keep functions focused; avoid global state. Follow existing naming patterns.
- Diagnostics: Use ID3DUserDefinedAnnotation for PIX markers (BeginEvent/EndEvent/SetMarker).

Key code areas
- DeviceResources.*: D3D device/context, swap chain, RTV/DSV, viewport, present, color space.
- Game.* and Main.cpp: Game loop, update, render, input, window management.

Decision log
- 2025-08-12: Adopt .github/copilot-instructions.md as the project guidance and decision log.
- 2025-08-12: Use fixed timestep for gameplay logic: 10 updates/second.
- 2025-08-12: Grid-based gameplay. Default grid 40x30 cells; cell size derived from client size.
- 2025-08-12: Handle input via WM_KEYDOWN/WM_KEYUP in WndProc and forward to Game (no reverse direction).
- 2025-08-12: Rendering approach: draw rectangles via ClearView; avoid extra dependencies.
- 2025-08-12: Add score and game-over state; show score/status in window title; SPACE restarts.
- 2025-08-12: Switch audio to XAudio2; loop music and play SFX via source voices; WAV assets loaded from Snake/Audio.

Next steps
- Add simple WAV loader validation and support for more formats (PCM16/float/stereo).
- Volume controls and mute.
- Document gameplay parameters in this file if changed.

Open items
- Provide actual WAV files under Snake/Audio (music.wav, eat.wav, gameover.wav) or update paths.