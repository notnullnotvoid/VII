/*
TODOs:
- ???

REFACTORS:
- remove old VI code
- running average over frame timings display
- write SSE2 versions of all blit routines
- further SSE optimization of routines that are currently scalar
- reduce massive code duplication of sprite blit routines with templates?
- finish moving heavy draw ops into pixel.cpp

BUGS:
- many transparent sprites drawn on top of each other can result in visible darkening (possible blend func issue?)
*/

#define GL_SILENCE_DEPRECATION

#define SDLL_IMPL
#include "sdll.hpp"
#include "trace.hpp"
#include "msf_gif.h"
#include "List.hpp"
#include "common.hpp"
#include "glad.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"
#include "pixel.hpp"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// MAIN FUNCTION                                                                                                    ///
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const float SPRINT_RETRIGGER_TIME = 0.25f;

#ifdef _WIN32
# undef main
#else
# include <unistd.h> //chdir()
#endif

#define print_log printf

int main(int argc, char ** argv) {
    init_profiling_trace(); //TODO: have this not allocate profiling event arenas if we aren't actually using them

    #ifdef _WIN32
        //bool success = load_sdl_functions("link/SDL2.dll");
        //if (!success) {
        //    printf("exiting application because we couldn't load SDL dynamically\n");
        //    exit(1);
        //}
    #else
        //set the current working directory to the folder containing the game's executable
        //NOTE: windows already does this when double-clicking the executable, which I think is all we care about?
        // printf("INVOCATION: %s\n", argv[0]);
        char * lastSlash = strrchr(argv[0], '/');
        assert(lastSlash);
        //NOTE: yes, I checked - it is safe to modify argv strings in-place according to the standard
        *lastSlash = '\0';
        assert(!chdir(argv[0]));
    #endif

        //initialize timer and startup SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO)) {
            printf("SDL FAILED TO INIT: %s\n", SDL_GetError());
            return 1;
        }
    print_log("[] SDL init: %f seconds\n", get_time());
        const int canvasWidth = 420;
        const int canvasHeight = 240;
        const int pixelScale = 3;
        const int windowWidth = canvasWidth * pixelScale;
        const int windowHeight = canvasHeight * pixelScale;
        const int windowDisplay = 0;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

        //we use 4 multisamples because the opengl spec guarantees it as the minimum that applications must support
        //and we have found in practice that many devices do run into problems at higher sample counts
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

        //NOTE: Drew's MS surface can use either high sample counts (e.g. 16) or an sRGB framebuffer,
        //      but not both at the same time - this was the source of bad gamma and crashes.
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

        SDL_Window * window = SDL_CreateWindow("Stranded",
            SDL_WINDOWPOS_CENTERED_DISPLAY(windowDisplay),
            SDL_WINDOWPOS_CENTERED_DISPLAY(windowDisplay),
            windowWidth, windowHeight,
            SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (window == nullptr) {
            printf("SDL FAILED TO CREATE WINDOW: %s\n", SDL_GetError());
            return 1;
        }

        assert(SDL_GL_CreateContext(window));
        if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal Graphics Error", "Your system does not support OpenGL 3.3.\r\nPlease update your graphics drivers and/or hardware.", NULL);
            printf("failed to load GLAD\n");
            exit(1);
        }

        SDL_GL_SetSwapInterval(1);
    print_log("[] SDL create window: %f seconds\n", get_time());
        uint blitShader = create_program(read_entire_file("res/blit.vert"), read_entire_file("res/blit.frag"));
        Canvas canvas = make_canvas(canvasWidth, canvasHeight, 16);
        Canvas lights = make_canvas(canvasWidth, canvasHeight, 16);
        Canvas light2 = make_canvas(canvasWidth, canvasHeight, 16); //electric boogaloo
        MonoFont font = load_mono_font("res/font-16-white.png", 8, 16);
    print_log("[] graphics init: %f seconds\n", get_time());
        SoLoud::Soloud loud = {};
        if (int soloudError = loud.init(); soloudError) {
            printf("soloud init error: %d\n", soloudError);
        }
        loud.setMaxActiveVoiceCount(64);
    print_log("[] soloud init: %f seconds\n", get_time());
        loud.setGlobalVolume(1.0f);
    print_log("[] audio init: %f seconds\n", get_time());
    resetWholeGame: //OOH HELL YEAH BOIIIIIII
		loud.stopAll();

        float frameTimes[100] = {};
        float thisTime = get_time();
        float lastTime = 0;
        float accumulator = 0;

        //keyboard input data
        bool keyHeld[SDL_NUM_SCANCODES] = {};
        bool keyDown[SDL_NUM_SCANCODES] = {}; //tracks whether keys were pressed THIS TICK (so only check in tick loop)
        //convenience macros
        #define HELD(X) (keyHeld[SDL_SCANCODE_ ## X])
        #define DOWN(X) (keyDown[SDL_SCANCODE_ ## X])

        const int gifCentiseconds = 5;
        MsfGifState gifState = {};
        bool giffing = false;
        float gifTimer = 0;

        float gameOverTimer = 0; //for both dead and win
        float fadeInTimer = 0;
        bool dead = false;
        bool win = false;

        gl_error("program init");
    print_log("[] done initializing: %f seconds\n", get_time());

    const float tickLength = 1.0f/240;
    float gameTime = 0;
    bool shouldExit = false;
    int frameCount = 0;
    while (!shouldExit) {
        float preFrame = get_time();
        int gameWidth, gameHeight;
        SDL_GetWindowSize(window, &gameWidth, &gameHeight);

        Vec2 tryAgain = vec2(canvas.width / 2, canvas.height / 2 + 20);
        Rect tryAgainRect = rect(tryAgain.x - 50, tryAgain.y - 10, 100, 20);

        //TODO: should event polling be moved into the tick loop
        //      for theoretical maximum responsiveness?
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                shouldExit = true;
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                SDL_Scancode scancode = event.key.keysym.scancode;
                keyHeld[scancode] = event.type == SDL_KEYDOWN;
                keyDown[scancode] |= event.type == SDL_KEYDOWN && !event.key.repeat;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                //left click
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                //right click
            }
        }

        //update timestep
        lastTime = thisTime;
        thisTime = get_time();
        float dt = thisTime - lastTime;
        //assert(dt > 0);
        accumulator += dt;
        gifTimer += dt;

        //update
        float gameSpeed = 1;
        float tickRateMultiplier = 1.0f;
        // tickRateMultiplier = 100.0f;
        // if (HELD(LGUI) || HELD(LCTRL) || HELD(RSHIFT)) gameSpeed *= 0.1f;
        // if (HELD(LSHIFT)) gameSpeed *= 5.0f;
        // if (HELD(RCTRL) || HELD(M)) tickRateMultiplier = 0.01f;
        // if (HELD(N)) tickRateMultiplier = 0.001f;
        //NOTE: we limit the maximum number of ticks per frame to avoid spinlocking
        for (int _tcount = 0; accumulator > tickLength / tickRateMultiplier && _tcount < 50; ++_tcount) {
            float tick = tickLength * gameSpeed;
            accumulator -= tickLength / tickRateMultiplier;

            //toggle gif recording
            if (DOWN(G) && (HELD(LGUI) || HELD(RGUI) || HELD(LCTRL))) {
                giffing = !giffing;
                if (giffing) {
                    gifTimer = 0;
                    msf_gif_begin(&gifState, "out.gif", canvasWidth, canvasHeight);
                } else {
                    msf_gif_end(&gifState);
                }
            }

            //handle end conditions
            if (dead || win) gameOverTimer += tick;
            fadeInTimer += tick;

            //NOTE: We reset these on a per-tick rather than per-frame basis,
            //      because if we reset per-frame and the tick rate is less
            //      than the frame rate, some keyDown and keyUp events can get missed.
            //      This means keyUp[] and keyDown[] must be checked each tick, NOT each frame!!!
            memset(keyDown, 0, sizeof(keyDown));
            gameTime += tick;
        }



        //update sliding window filter for framerate
        float timeSum = 0;
        for (int i = 1; i < ARR_SIZE(frameTimes); ++i) {
            frameTimes[i - 1] = frameTimes[i];
            timeSum += frameTimes[i - 1];
        }
        frameTimes[ARR_SIZE(frameTimes) - 1] = dt;
        timeSum += dt;

        //print framerate every so often
        float framerate = ARR_SIZE(frameTimes) / timeSum;
        if (frameCount % 100 == 99) {
            print_log("frame %5d     fps %3d\n", frameCount, (int)(framerate + 0.5f));
        }

        //NOTE: We need to do this because glViewport() isn't called for us
        //      when the window is resized on Windows, even though it is on macOS
        int bufferWidth, bufferHeight;
        SDL_GL_GetDrawableSize(window, &bufferWidth, &bufferHeight);
        glViewport(0, 0, bufferWidth, bufferHeight);



        //clear screen
        glEnable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_MULTISAMPLE);
        glDisable(GL_CULL_FACE);
        glClear(GL_COLOR_BUFFER_BIT);

        for (int y = 0; y < canvas.height; ++y) {
            for (int x = 0; x < canvas.width; ++x) {
                canvas[y][x] = { 0, 0, 0, 255 };
            }
        }

        //TODO: draw game

        {
            if (fadeInTimer > 3) fadeInTimer = 3;
            float fadeOutTimer = fmaxf(0, fminf(3, 3 - gameOverTimer));
            float globalVolume = (fadeInTimer / 3) * (fadeOutTimer / 3);
            loud.setGlobalVolume(globalVolume * 0.666f);
        }

        auto draw_text_centered = [] (Canvas canvas, MonoFont font, int cx, int cy, Color color, const char * text) {
            int len = strlen(text);
            int x = cx - font.glyphWidth * len / 2;
            int y = cy - font.glyphHeight / 2;
            draw_text(canvas, font, x, y, color, text);
        };

        //apply fullscreen overlays
        {
            if (fadeInTimer > 3) fadeInTimer = 3;
            u8 blackOpacity = imin(255, (1 - fadeInTimer / 3) * 255);
            draw_rect(canvas, 0, 0, canvas.width, canvas.height, { 0, 0, 0, blackOpacity });
        }

        if (dead) {
            u8 blackOpacity = imin(255, gameOverTimer / 2 * 255);
            draw_rect(canvas, 0, 0, canvas.width, canvas.height, { 0, 0, 0, blackOpacity });
            u8 textOpacity = imax(0, imin(255, (gameOverTimer - 1) / 3 * 191));
            draw_text_centered(canvas,
                font, canvas.width / 2, canvas.height / 2 - 40, { 255, 255, 255, textOpacity }, "YOU DIED");
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            Vec2 mousePos = vec2((float)mx * canvas.width / gameWidth, (float)my * canvas.height / gameHeight);
            u8 textOpacity2 = imax(0, imin(255, (gameOverTimer - 2) / 3 * 255));
            u8 buttonBaseOpacity = !contains(tryAgainRect, mousePos)? 32 : 64;
            u8 buttonOpacity = (textOpacity2 * buttonBaseOpacity) >> 8;
            draw_rect(canvas,
                tryAgainRect.x, tryAgainRect.y, tryAgainRect.w, tryAgainRect.h, { 255, 255, 255, buttonOpacity });
            if (!contains(tryAgainRect, mousePos)) textOpacity2 *= 0.75f;
            draw_text_centered(canvas, font, tryAgain.x, tryAgain.y, { 255, 255, 255, textOpacity2 }, "TRY AGAIN");
        }
        if (win) {
            float beginFade = 0; //gives time for the boat to animate
            float timer = fmaxf(0, gameOverTimer - beginFade);
            u8 blackOpacity = imin(255, timer / 2 * 255);
            draw_rect(canvas, 0, 0, canvas.width, canvas.height, { 255, 255, 255, blackOpacity });
            u8 textOpacity = imax(0, imin(255, (timer - 1) / 3 * 191));
            int cx = canvas.width / 2, cy = canvas.height / 2;
            Color c = { 64, 96, 96, textOpacity };
            draw_text_centered(canvas, font, cx, cy - 40, c, "YOU ESCAPED");
            draw_text_centered(canvas, font, cx, cy     , c, "CREDITS:");
            draw_text_centered(canvas, font, cx, cy + 20, c, "Phillip Trudeau-Tavara   Programming, Sound");
            draw_text_centered(canvas, font, cx, cy + 40, c, "Miles Fogle                     Programming");
            draw_text_centered(canvas, font, cx, cy + 60, c, "Isaiah Davis-Stober                     Art");
            draw_text_centered(canvas, font, cx, cy + 80, c, "Eugenio Bruno                         Music");
        }

        if (giffing && gifTimer > gifCentiseconds / 100.0f) {
            msf_gif_frame(&gifState, (uint8_t *) canvas.pixels, canvas.pitch * 4, gifCentiseconds, 15, false);
            gifTimer -= gifCentiseconds / 100.0f;
        }

        if (giffing) {
            draw_text(canvas, font, 2, 2, { 255, 255, 255, 100 }, "GIF");
        }

        // char buffer[256];
        // snprintf(buffer, sizeof(buffer), "%4.1f ms", (get_time() - preFrame) * 1000);
        // draw_text(canvas, font, canvas.width - 80, 2, { 255, 255, 255, 100 }, buffer);
        // snprintf(buffer, sizeof(buffer), "%d%% hp", (int) (playerHealth * 100));
        // draw_text(canvas, font, canvas.width - 80, 20, { 255, 255, 255, 100 }, buffer);

        draw_canvas(blitShader, 0? lights : canvas, bufferWidth, bufferHeight);

        gl_error("after everything");
        SDL_GL_SwapWindow(window);
        fflush(stdout);
        fflush(stderr);
        frameCount += 1;

        //limit framerate when window is not focused
        u32 flags = SDL_GetWindowFlags(window);
        if (!(flags & SDL_WINDOW_INPUT_FOCUS)) {
            SDL_Delay(50);
        }

        //uncomment this to make the game exit immediately (good for testing compile+load times)
        // if (frameCount > 5) shouldExit = true;
    }

    printf("exiting game normally at %f seconds\n", get_time());
    return 0;
}
