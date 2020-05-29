//NOTE: #define SDLL_IMPL in the translation unit where you want the function pointers to reside

#include <SDL.h>
#include <SDL_syswm.h>

#if 0//def _WIN32

# ifdef SDLL_IMPL
#  define SDLL_FUNC
# else
#  define SDLL_FUNC extern
# endif

SDLL_FUNC Uint64 (* _GetPerformanceCounter) ();
SDLL_FUNC Uint64 (* _GetPerformanceFrequency) ();
SDLL_FUNC int (* _Init) (Uint32 flags);
SDLL_FUNC const char * (* _GetError) ();
SDLL_FUNC SDL_Window * (* _CreateWindow) (const char * title, int x, int y, int w, int h, Uint32 flags);
SDLL_FUNC int (* _PollEvent) (SDL_Event * event);
SDLL_FUNC SDL_Surface * (* _GetWindowSurface) (SDL_Window * window);
SDLL_FUNC int (* _UpdateWindowSurface) (SDL_Window * window);
SDLL_FUNC int (* _SetRelativeMouseMode) (SDL_bool enabled);
SDLL_FUNC int (* _GL_SetAttribute) (SDL_GLattr attr, int value);
SDLL_FUNC SDL_GLContext (* _GL_CreateContext) (SDL_Window * window);
SDLL_FUNC int (* _GL_SetSwapInterval) (int interval);
SDLL_FUNC void (* _GL_SwapWindow) (SDL_Window * window);
SDLL_FUNC void (* _Delay) (Uint32 ms);
SDLL_FUNC void * (* _GL_GetProcAddress) (const char * proc);
SDLL_FUNC SDL_Joystick * (* _JoystickOpen) (int device_index);
SDLL_FUNC SDL_GameController * (* _GameControllerOpen) (int joystick_index);
SDLL_FUNC SDL_JoystickID (* _JoystickInstanceID) (SDL_Joystick * joystick);
SDLL_FUNC void (* _GameControllerClose) (SDL_GameController * gamecontroller);
SDLL_FUNC void (* _JoystickClose) (SDL_Joystick * joystick);
SDLL_FUNC int (* _GetDisplayMode) (int displayIndex, int modeIndex, SDL_DisplayMode * mode);
SDLL_FUNC int (* _SetWindowDisplayMode) (SDL_Window * window, const SDL_DisplayMode * mode);
SDLL_FUNC int (* _SetWindowFullscreen) (SDL_Window * window, Uint32 flags);
SDLL_FUNC void (* _GL_GetDrawableSize) (SDL_Window * window, int * w, int * h);
SDLL_FUNC Uint32 (* _GetWindowFlags) (SDL_Window * window);
SDLL_FUNC int (* _GetWindowDisplayIndex) (SDL_Window * window);
SDLL_FUNC int (* _GetDesktopDisplayMode) (int displayIndex, SDL_DisplayMode * mode);
SDLL_FUNC SDL_DisplayMode * (* _GetClosestDisplayMode) (int displayIndex, const SDL_DisplayMode * mode, SDL_DisplayMode * closest);
SDLL_FUNC const char * (* _GetPixelFormatName) (Uint32 format);
SDLL_FUNC SDL_GLContext (* _GL_GetCurrentContext) ();

SDLL_FUNC void (* _free) (void * mem);
SDLL_FUNC char * (* _GetClipboardText) ();
SDLL_FUNC int (* _SetClipboardText) (const char * text);
SDLL_FUNC SDL_Keymod (* _GetModState) ();
SDLL_FUNC SDL_Cursor * (* _CreateSystemCursor) (SDL_SystemCursor id);
SDLL_FUNC SDL_bool (* _GetWindowWMInfo) (SDL_Window * window, SDL_SysWMinfo * info);
SDLL_FUNC void (* _FreeCursor) (SDL_Cursor * cursor);
SDLL_FUNC void (* _WarpMouseInWindow) (SDL_Window * window, int x, int y);
SDLL_FUNC Uint32 (* _GetMouseState) (int * x, int * y);
SDLL_FUNC SDL_Window * (* _GetKeyboardFocus) (void);
SDLL_FUNC void (* _SetWindowPosition) (SDL_Window * window, int x, int y);
SDLL_FUNC Uint32 (* _GetGlobalMouseState) (int * x, int * y);
SDLL_FUNC int (* _CaptureMouse) (SDL_bool enabled);
SDLL_FUNC int (* _ShowCursor) (int toggle);
SDLL_FUNC void (* _SetCursor) (SDL_Cursor * cursor);
SDLL_FUNC Uint8 (* _GameControllerGetButton) (SDL_GameController * gamecontroller, SDL_GameControllerButton button);
SDLL_FUNC Sint16 (* _GameControllerGetAxis) (SDL_GameController * gamecontroller, SDL_GameControllerAxis axis);
SDLL_FUNC void (* _GetWindowSize) (SDL_Window * window, int * w, int * h);
SDLL_FUNC void (* _GetWindowPosition) (SDL_Window * window, int * x, int * y);

#define SDL_GetPerformanceCounter   _GetPerformanceCounter
#define SDL_GetPerformanceFrequency _GetPerformanceFrequency
#define SDL_Init                    _Init
#define SDL_GetError                _GetError
#define SDL_CreateWindow            _CreateWindow
#define SDL_PollEvent               _PollEvent
#define SDL_GetWindowSurface        _GetWindowSurface
#define SDL_UpdateWindowSurface     _UpdateWindowSurface
#define SDL_SetRelativeMouseMode    _SetRelativeMouseMode
#define SDL_GL_SetAttribute         _GL_SetAttribute
#define SDL_GL_CreateContext        _GL_CreateContext
#define SDL_GL_SetSwapInterval      _GL_SetSwapInterval
#define SDL_GL_SwapWindow           _GL_SwapWindow
#define SDL_Delay                   _Delay
#define SDL_GL_GetProcAddress       _GL_GetProcAddress
#define SDL_JoystickOpen            _JoystickOpen
#define SDL_GameControllerOpen      _GameControllerOpen
#define SDL_JoystickInstanceID      _JoystickInstanceID
#define SDL_GameControllerClose     _GameControllerClose
#define SDL_JoystickClose           _JoystickClose
#define SDL_GetDisplayMode          _GetDisplayMode
#define SDL_SetWindowDisplayMode    _SetWindowDisplayMode
#define SDL_SetWindowFullscreen     _SetWindowFullscreen
#define SDL_GL_GetDrawableSize      _GL_GetDrawableSize
#define SDL_GetWindowFlags          _GetWindowFlags
#define SDL_GetWindowDisplayIndex   _GetWindowDisplayIndex
#define SDL_GetDesktopDisplayMode   _GetDesktopDisplayMode
#define SDL_GetClosestDisplayMode   _GetClosestDisplayMode
#define SDL_GetPixelFormatName      _GetPixelFormatName
#define SDL_GL_GetCurrentContext    _GL_GetCurrentContext

#define SDL_free                    _free
#define SDL_GetClipboardText        _GetClipboardText
#define SDL_SetClipboardText        _SetClipboardText
#define SDL_GetModState             _GetModState
#define SDL_CreateSystemCursor      _CreateSystemCursor
#define SDL_GetWindowWMInfo         _GetWindowWMInfo
#define SDL_FreeCursor              _FreeCursor
#define SDL_WarpMouseInWindow       _WarpMouseInWindow
#define SDL_GetMouseState           _GetMouseState
#define SDL_GetKeyboardFocus        _GetKeyboardFocus
#define SDL_SetWindowPosition       _SetWindowPosition
#define SDL_GetGlobalMouseState     _GetGlobalMouseState
#define SDL_CaptureMouse            _CaptureMouse
#define SDL_ShowCursor              _ShowCursor
#define SDL_SetCursor               _SetCursor
#define SDL_GameControllerGetButton _GameControllerGetButton
#define SDL_GameControllerGetAxis   _GameControllerGetAxis
#define SDL_GetWindowSize           _GetWindowSize
#define SDL_GetWindowPosition       _GetWindowPosition

# ifdef SDLL_IMPL

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

template <typename TYPE>
static bool load_func(HMODULE handle, TYPE * func, const char * name) {
    *func = (TYPE) GetProcAddress(handle, name);
    if (*func == nullptr) {
        printf("FAILED TO LOAD FUNCTION %s\n", name);
        printf("Error code: %lu\n", GetLastError());
        return false;
    }
    return true;
}

//we don't care to unload the DLL before application exit,
//so it's fine that we just throw away the handle inside this function
//instead of returning it like we're "supposed" to
static bool load_sdl_functions(const char * filepath) {
    HMODULE handle = LoadLibraryA(filepath);
    if (!handle) {
        printf("FAILED TO LOAD DYNAMIC LIBRARY: %s\n", filepath);
        printf("Error code: %lu\n", GetLastError());
        return false;
    }

    if (!load_func(handle, &_GetPerformanceCounter  , "SDL_GetPerformanceCounter"  )) return false;
    if (!load_func(handle, &_GetPerformanceFrequency, "SDL_GetPerformanceFrequency")) return false;
    if (!load_func(handle, &_Init                   , "SDL_Init"                   )) return false;
    if (!load_func(handle, &_GetError               , "SDL_GetError"               )) return false;
    if (!load_func(handle, &_CreateWindow           , "SDL_CreateWindow"           )) return false;
    if (!load_func(handle, &_PollEvent              , "SDL_PollEvent"              )) return false;
    if (!load_func(handle, &_GetWindowSurface       , "SDL_GetWindowSurface"       )) return false;
    if (!load_func(handle, &_UpdateWindowSurface    , "SDL_UpdateWindowSurface"    )) return false;
    if (!load_func(handle, &_SetRelativeMouseMode   , "SDL_SetRelativeMouseMode"   )) return false;
    if (!load_func(handle, &_GL_SetAttribute        , "SDL_GL_SetAttribute"        )) return false;
    if (!load_func(handle, &_GL_CreateContext       , "SDL_GL_CreateContext"       )) return false;
    if (!load_func(handle, &_GL_SetSwapInterval     , "SDL_GL_SetSwapInterval"     )) return false;
    if (!load_func(handle, &_GL_SwapWindow          , "SDL_GL_SwapWindow"          )) return false;
    if (!load_func(handle, &_Delay                  , "SDL_Delay"                  )) return false;
    if (!load_func(handle, &_GL_GetProcAddress      , "SDL_GL_GetProcAddress"      )) return false;
    if (!load_func(handle, &_JoystickOpen           , "SDL_JoystickOpen"           )) return false;
    if (!load_func(handle, &_GameControllerOpen     , "SDL_GameControllerOpen"     )) return false;
    if (!load_func(handle, &_JoystickInstanceID     , "SDL_JoystickInstanceID"     )) return false;
    if (!load_func(handle, &_GameControllerClose    , "SDL_GameControllerClose"    )) return false;
    if (!load_func(handle, &_JoystickClose          , "SDL_JoystickClose"          )) return false;
    if (!load_func(handle, &_GetDisplayMode         , "SDL_GetDisplayMode"         )) return false;
    if (!load_func(handle, &_SetWindowDisplayMode   , "SDL_SetWindowDisplayMode"   )) return false;
    if (!load_func(handle, &_SetWindowFullscreen    , "SDL_SetWindowFullscreen"    )) return false;
    if (!load_func(handle, &_GL_GetDrawableSize     , "SDL_GL_GetDrawableSize"     )) return false;
    if (!load_func(handle, &_GetWindowFlags         , "SDL_GetWindowFlags"         )) return false;
    if (!load_func(handle, &_GetWindowDisplayIndex  , "SDL_GetWindowDisplayIndex"  )) return false;
    if (!load_func(handle, &_GetDesktopDisplayMode  , "SDL_GetDesktopDisplayMode"  )) return false;
    if (!load_func(handle, &_GetClosestDisplayMode  , "SDL_GetClosestDisplayMode"  )) return false;
    if (!load_func(handle, &_GetPixelFormatName     , "SDL_GetPixelFormatName"     )) return false;
    if (!load_func(handle, &_GL_GetCurrentContext   , "SDL_GL_GetCurrentContext"   )) return false;

    if (!load_func(handle, &_free                   , "SDL_free"                   )) return false;
    if (!load_func(handle, &_GetClipboardText       , "SDL_GetClipboardText"       )) return false;
    if (!load_func(handle, &_SetClipboardText       , "SDL_SetClipboardText"       )) return false;
    if (!load_func(handle, &_GetModState            , "SDL_GetModState"            )) return false;
    if (!load_func(handle, &_CreateSystemCursor     , "SDL_CreateSystemCursor"     )) return false;
    if (!load_func(handle, &_GetWindowWMInfo        , "SDL_GetWindowWMInfo"        )) return false;
    if (!load_func(handle, &_FreeCursor             , "SDL_FreeCursor"             )) return false;
    if (!load_func(handle, &_WarpMouseInWindow      , "SDL_WarpMouseInWindow"      )) return false;
    if (!load_func(handle, &_GetMouseState          , "SDL_GetMouseState"          )) return false;
    if (!load_func(handle, &_GetKeyboardFocus       , "SDL_GetKeyboardFocus"       )) return false;
    if (!load_func(handle, &_SetWindowPosition      , "SDL_SetWindowPosition"      )) return false;
    if (!load_func(handle, &_GetGlobalMouseState    , "SDL_GetGlobalMouseState"    )) return false;
    if (!load_func(handle, &_CaptureMouse           , "SDL_CaptureMouse"           )) return false;
    if (!load_func(handle, &_ShowCursor             , "SDL_ShowCursor"             )) return false;
    if (!load_func(handle, &_SetCursor              , "SDL_SetCursor"              )) return false;
    if (!load_func(handle, &_GameControllerGetButton, "SDL_GameControllerGetButton")) return false;
    if (!load_func(handle, &_GameControllerGetAxis  , "SDL_GameControllerGetAxis"  )) return false;
    if (!load_func(handle, &_GetWindowSize          , "SDL_GetWindowSize"          )) return false;
    if (!load_func(handle, &_GetWindowPosition      , "SDL_GetWindowPosition"      )) return false;

    return true;
}

#endif //SDLL_IMPL

#endif //_WIN32
