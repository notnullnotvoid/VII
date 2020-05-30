/*
TODOs:
- draw blinking cursor at prompt
- clip terminal output
- create map
- spec out puzzles
- colored text?
- physical terminal graphics
- startup sound
- other sounds?
- replace terminal graphics
- screen-space CRT bulge effect
- better font
- windows builds

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
/// TERMINAL                                                                                                         ///
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Line {
    char * text;
    Image image;
};

struct Puzzle {
    List<Line> prompt;
    List<char *> answer;
};

//REMINDER: this hasn't been tested!
List<Puzzle> parse_puzzles() {
    char * text = read_entire_file("res/puzzles.txt");
    List<char *> lines = split_non_empty_lines_in_place(text);
    List<Puzzle> puzzles = {};
    puzzles.add({});
    bool p2 = false, consumingLines = false;
    for (char * line : lines) {
        if (line[0] == '@') {
            char * token = strtok(line, " \t");
            if (!strcmp(token, p2? "@q2" : "@q1")) {
                consumingLines = true;
            } else if (consumingLines && !strcmp(token, "@image")) {
                char * path = strtok(nullptr, "");
                puzzles[puzzles.len - 1].prompt.add({ nullptr, load_image(path) });
            } else {
                consumingLines = false;
                if (!strcmp(token, "@p2")) {
                    p2 = true;
                } else if (!strcmp(token, "@next")) {
                    puzzles.add({});
                } else if (!strcmp(token, p2? "@a2" : "@a1")) {
                    while ((token = strtok(nullptr, " \t"))) {
                        for (char * c = token; *c != '\0'; ++c) *c = tolower(*c);
                        puzzles[puzzles.len - 1].answer.add(token);
                    }
                }
            }
        } else if (consumingLines) {
            puzzles[puzzles.len - 1].prompt.add({ line });
        }
    }
    return puzzles;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// MAIN FUNCTION                                                                                                    ///
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
# undef main
#else
# include <unistd.h> //chdir()
#endif

#define print_log printf

int main(int argc, char ** argv) {
    init_profiling_trace(); //TODO: have this not allocate profiling event arenas if we aren't actually using them

    #ifdef _WIN32
        bool success = load_sdl_functions("link/SDL2.dll");
        if (!success) {
           printf("exiting application because we couldn't load SDL dynamically\n");
           exit(1);
        }
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
        const int canvasWidth = 640;
        const int canvasHeight = 360;
        const int pixelScale = 2;
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

        SDL_Window * window = SDL_CreateWindow("Terminal",
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
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                "Fatal Graphics Error",
                "Your system does not support OpenGL 3.3.\r\nPlease update your graphics drivers and/or hardware.",
                NULL);
            printf("failed to load GLAD\n");
            exit(1);
        }

        SDL_GL_SetSwapInterval(1);
    print_log("[] SDL create window: %f seconds\n", get_time());
        uint blitShader = create_program(read_entire_file("res/blit.vert"), read_entire_file("res/blit.frag"));
        Canvas canvas = make_canvas(canvasWidth, canvasHeight, 16);
        MonoFont font = load_mono_font("res/font-16-white.png", 8, 16);
    print_log("[] graphics init: %f seconds\n", get_time());
        SoLoud::Soloud loud = {};
        if (int soloudError = loud.init(); soloudError) {
            printf("soloud init error: %d\n", soloudError);
        }
        loud.setMaxActiveVoiceCount(64);
    print_log("[] soloud init: %f seconds\n", get_time());
        loud.setGlobalVolume(0.0f);


    print_log("[] audio init: %f seconds\n", get_time());
        float frameTimes[100] = {};
        float thisTime = get_time();
        float lastTime = 0;
        float accumulator = 0;
        float fadeInTimer = 0;

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

        //terminal data
        const int MAX_INPUT = 40;
        List<Line> term = {};
        char input[MAX_INPUT + 1] = {};
        int upscroll = 0;

        //terminal viewport calculations
        int cw = font.glyphWidth;
        int ch = font.glyphHeight * 1.6;
        int tw = 40 * cw;
        int th = 26 * ch;
        auto total_term_height = [&] () {
            int totalHeight = 0;
            for (Line line : term) {
                if (line.text) {
                    totalHeight += ch;
                } else {
                    totalHeight += line.image.height;
                }
            }
            return imax(th, totalHeight);
        };

        //game progression
        List<Puzzle> puzzles = parse_puzzles();
        int puzzleIdx = 0;

        //print initial puzzle
        auto print_puzzle = [&] () {
            for (Line line : puzzles[puzzleIdx].prompt) {
                if (line.text) {
                    term.add({ dsprintf(nullptr, " %s", line.text) });
                } else {
                    term.add(line);
                }
            }
        };
        print_puzzle();

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

                //handle text input
                if (event.type == SDL_KEYDOWN && scancode == SDL_SCANCODE_BACKSPACE) {
                    int c = strlen(input);
                    if (c > 0) {
                        input[c - 1] = '\0';
                        upscroll = 0;
                    }
                }

                if (event.type == SDL_KEYDOWN && scancode == SDL_SCANCODE_RETURN) {
                    term.add({ dsprintf(nullptr, "> %s", input) });
                    upscroll = 0;

                    //tokenize input
                    const char * delims = " !\"#$%&()*+,-./:;<=>?@[\\]^_`{|}~";
                    char * token = strtok(input, delims);
                    List<char *> tokens = {};
                    if (token) {
                        do {
                            for (char * ch = token; *ch; ++ch) {
                                *ch = tolower(*ch);
                            }
                            tokens.add(token);
                        } while ((token = strtok(nullptr, delims)));
                    }

                    //check against answer
                    bool correct = tokens.len == puzzles[puzzleIdx].answer.len;
                    if (correct) {
                        for (int i = 0; i < tokens.len; ++i) {
                            if (strcmp(tokens[i], puzzles[puzzleIdx].answer[i])) {
                                correct = false;
                                break;
                            }
                        }
                    }

                    //clear input
                    input[0] = '\0';

                    //print response
                    if (puzzleIdx < puzzles.len - 1) {
                        if (correct) {
                            ++puzzleIdx;
                            print_puzzle();
                        } else {
                            term.add({ dup(" ERROR: incorrect input") });
                        }
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                //left click
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                //right click
            } else if (event.type == SDL_TEXTINPUT) {
                int c = strlen(input);
                if (c < MAX_INPUT && isprint(event.text.text[0])) {
                    input[c] = event.text.text[0];
                    input[c + 1] = '\0';
                    upscroll = 0;
                }
            } else if (event.type == SDL_MOUSEWHEEL) {
                upscroll = imax(0, imin(total_term_height() - th, upscroll + event.wheel.y * ch));
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
            fadeInTimer += tick;

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



        //draw terminal lines
        Color white = { 255, 255, 255, 255 };
        int tx = 20;
        int ty = 20 + th - total_term_height() + upscroll;
        for (Line line : term) {
            if (line.text) {
                draw_text(canvas, font, tx, ty, white, line.text);
                ty += ch;
            } else {
                draw_sprite(canvas, line.image, tx, ty);
                ty += line.image.height;
            }
        }

        //draw input line
        draw_text(canvas, font, tx, ty, white, ">");
        draw_text(canvas, font, tx + font.glyphWidth * 2, ty, white, input);



        //fade in sound
        if (fadeInTimer > 3) fadeInTimer = 3;
        float globalVolume = (fadeInTimer / 3);
        loud.setGlobalVolume(globalVolume * 0.666f);

        auto draw_text_centered = [] (Canvas canvas, MonoFont font, int cx, int cy, Color color, const char * text) {
            int len = strlen(text);
            int x = cx - font.glyphWidth * len / 2;
            int y = cy - font.glyphHeight / 2;
            draw_text(canvas, font, x, y, color, text);
        };

        //apply fullscreen fade-in overlay
        fadeInTimer = 3; //DEBUG
        u8 blackOpacity = imin(255, (1 - fadeInTimer / 3) * 255);
        draw_rect(canvas, 0, 0, canvas.width, canvas.height, { 0, 0, 0, blackOpacity });

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

        draw_canvas(blitShader, canvas, bufferWidth, bufferHeight);

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
