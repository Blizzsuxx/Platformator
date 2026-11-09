#pragma once

#include <backends/imgui_impl_sdl3.h>

struct ImGuiContext;
struct SDL_Window;

// Editor-only additions to the upstream SDL3 backend. The implementation is
// kept in this directory so the vendored upstream file remains untouched.
void ImGui_ImplSDL3_EditorSetSharedGamepadOwner(bool owner);
ImGuiContext *ImGui_ImplSDL3_EditorContextForWindow(SDL_Window *window);
// Private deterministic seam for the editor's host ownership regression.
void ImGui_ImplSDL3_EditorTestUpdateImeForWindow(SDL_Window *focusedWindow);
SDL_Window *ImGui_ImplSDL3_EditorTestImeOwner();
