/*
POST-JAM SNEAKY CHANGES
- missing sounds
    - death
- smaller battery
    - animation for carrying battery
- animation for picking up items (so they take time - anti-cheese as well as polish)
- remove unused assets

POLISH:
- boat animation
- music wash-out upon death
- show player facing direction (flip sprites)
- player running animation
- fancier lighting effects (rabbit hole)
    - form shadows (surface lighting)
    - cast shadows
    - cast shadows on forms
- better (light-buffer-based) enemy repulsion logic
    - compute local gradient
        - debug viz
    - moving away from light takes priority over flocking and chasing
- proper tree felling animation
- particle effects for fire
- animation for dropped items to fall and bounce
- better player + enemy movement feel (persistent velocity, similar to escher)
- in-game tutorial text
    - thought bubbles?
- intro cutscene
- make items on ground repel each other?
- show player stats on win screen
- axe and torch take priority over logs when trying to pick up items?
- fancier rendering of items in hand (and on ground)

REFACTORS:
- everything
- sort all sprites the simple way because we can
- combine item highlights and level rendering
- player holds reference to item in list instead of holding item directly?
- put all debug helper code behind one bool so I don't have to constantly toggle individual things

- running average over frame timings display
- write SSE2 versions of all blit routines
- further SSE optimization of routines that are currently scalar
- reduce massive code duplication of sprite blit routines with templates?
- finish moving heavy draw ops into pixel.cpp

STRETCH:
- car
- car headlight
- car radio

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
#include "level.hpp"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// AUDIO                                                                                                            ///
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static SoLoud::Soloud loud;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PHYSICS                                                                                                          ///
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LineSegment {
    Vec2 a, b;
};

struct Circle {
    Vec2 p;
    float r;
};

struct ArcConnector {
    Vec2 p, leg1, leg2;
    float r;
};

static inline bool angle_contains(ArcConnector arc, Vec2 p) {
    Vec2 v = p - arc.p;
    if (cross(arc.leg1, arc.leg2) < 0) { //arc length > 180 degrees
        return cross(arc.leg1, v) < 0 && cross(arc.leg2, v) > 0;
    } else { //arc length < 180 degrees
        return cross(arc.leg1, v) < 0 || cross(arc.leg2, v) > 0;
    }
}

//TODO: find a reasonable way to de-dupe these almost identical functions
static void add_collision_rect(List<LineSegment> & lines, List<ArcConnector> & arcs, Vec2 p1, Vec2 p2, float r,
                               bool npx, bool nnx, bool npy, bool nny)
{
    if (!nnx) lines.add({ { p1.x - r, p1.y     }, { p1.x - r, p2.y     } });
    if (!npy) lines.add({ { p1.x    , p2.y + r }, { p2.x    , p2.y + r } });
    if (!npx) lines.add({ { p2.x + r, p2.y     }, { p2.x + r, p1.y     } });
    if (!nny) lines.add({ { p2.x    , p1.y - r }, { p1.x    , p1.y - r } });

    if (r > 0 && !nnx && !nny) arcs.add({ { p1.x, p1.y }, {  0, -1 }, { -1,  0 }, r });
    if (r > 0 && !nnx && !npy) arcs.add({ { p1.x, p2.y }, { -1,  0 }, {  0,  1 }, r });
    if (r > 0 && !npx && !npy) arcs.add({ { p2.x, p2.y }, {  0,  1 }, {  1,  0 }, r });
    if (r > 0 && !npx && !nny) arcs.add({ { p2.x, p1.y }, {  1,  0 }, {  0, -1 }, r });
}

static void add_collision_volumes(Level & level, Graphics & graphics,
    List<LineSegment> & lines, List<ArcConnector> & arcs, Circle collider, Vec2 movement)
{
    Layer & tiles = level.layers[0];
    float moveLen = len(movement) + 0.5f;
    int minx = imax(0, (collider.p.x - moveLen - collider.r) / TILE_WIDTH);
    int miny = imax(0, (collider.p.y - moveLen - collider.r) / TILE_HEIGHT);
    int maxx = imin(tiles.width - 1, (collider.p.x + moveLen + collider.r) / TILE_WIDTH);
    int maxy = imin(tiles.height - 1, (collider.p.y + moveLen + collider.r) / TILE_HEIGHT);
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            if (tiles[y][x] < 0 || !graphics.isGround[tiles[y][x]]) {
                //determine whether we have unreachable neighbors in each cardinal direction
                bool npx = x + 1 > maxx || tiles[y][x + 1] < 0 || !graphics.isGround[tiles[y][x + 1]];
                bool nnx = x - 1 < minx || tiles[y][x - 1] < 0 || !graphics.isGround[tiles[y][x - 1]];
                bool npy = y + 1 > maxy || tiles[y + 1][x] < 0 || !graphics.isGround[tiles[y + 1][x]];
                bool nny = y - 1 < miny || tiles[y - 1][x] < 0 || !graphics.isGround[tiles[y - 1][x]];
                add_collision_rect(lines, arcs,
                    vec2(x * TILE_WIDTH, y * TILE_HEIGHT), vec2((x + 1) * TILE_WIDTH, (y + 1) * TILE_WIDTH),
                    collider.r, npx, nnx, npy, nny);
            }
        }
    }

    for (Entity & e : level.entities) {
        if (e.type == ENT_TREE) {
            add_collision_rect(lines, arcs, e.pos - vec2(2), e.pos + vec2(2), collider.r + 3, false, false, false, false);
        } else if (e.type == ENT_FIRESRC) {
            Vec2 p = e.pos - vec2(0, 6);
            add_collision_rect(lines, arcs, p - vec2(22, 2), p + vec2(22, 2), collider.r + 4, false, false, false, false);
        }
    }
}

struct RaycastResult { float tmin; Vec2 normal; };
static RaycastResult raycast(List<LineSegment> lines, List<ArcConnector> arcs, Vec2 rayStart, Vec2 rayDir) {
    float tmin = 1; //0 = no distance, 1 = full distance of rayDir
    Vec2 normal = {};
    for (LineSegment line : lines) {
        Vec2 segmentStart = line.a, segmentDir = line.b - line.a;
        //NOTE: algorithm adapted from https://stackoverflow.com/a/565282/3064745
        float divisor = cross(rayDir, segmentDir);
        if (divisor > 0) {
            Vec2 diff = segmentStart - rayStart;
            float t0 = cross(diff, segmentDir);
            if (t0 >= 0 && t0 <= divisor) {
                float u0 = cross(diff, rayDir);
                if (u0 >= 0 && u0 <= divisor) {
                    float t = t0 / divisor;
                    if (t < tmin) {
                        tmin = t;
                        Vec2 tangent = line.b - line.a;
                        normal = vec2(-tangent.y, tangent.x);
                    }
                }
            }
        }
    }

    for (ArcConnector arc : arcs) {
        //check if circle is in general direction of ray
        //NOTE: this prevents intersecting with a circle from the inside
        //      so we won't get stuck bouncing around inside a circle
        Vec2 AC = arc.p - rayStart;
        if (dot(rayDir, AC) > 0.0f) {
            //project circle center onto ray
            Vec2 R = nor(rayDir);
            Vec2 P = rayStart + R * dot(R, AC);
            //calculate distance between C and P
            float dist2 = len2(P - arc.p);
            if (dist2 < arc.r * arc.r) {
                //solve the circle equation y = sqrt(r^2 - x^2) at x = dist
                float solve = sqrtf(arc.r * arc.r - dist2);
                //move backward along the ray from P to find the nearest intersection point
                Vec2 intersect = P - solve * R;

                if (angle_contains(arc, intersect)) {
                    //test if the intersection point is within the bounds of the ray
                    float t = len(intersect - rayStart) / len(rayDir);
                    if (t < tmin) {
                        normal = intersect - arc.p;
                        tmin = t;
                    }
                }
            }
        }
    }

    return { tmin, nor(normal) };
}

struct PhysicsFrame {
    List<LineSegment> lines;
    List<ArcConnector> arcs;
    Vec2 pos;
};

static PhysicsFrame debugPhysicsFrame = {};
static bool recordingPhysicsFrames = false;
static const float PHYSICS_DISCRETE_RADIUS = 1.0f;

//TODO: why are we passing in rayStart when it's identical to *pos at the beginning of the func?
static void collide_with_volumes(Vec2 rayStart, Vec2 rayDir, Vec2 * pos, Vec2 * vel,
                                 List<LineSegment> lines, List<ArcConnector> arcs,
                                 List<Vec2> * normals, float tick, float bounce)
{
    if (recordingPhysicsFrames) {
        debugPhysicsFrame.lines.finalize();
        debugPhysicsFrame.arcs.finalize();
        debugPhysicsFrame.lines = lines.clone();
        debugPhysicsFrame.arcs = arcs.clone();
        debugPhysicsFrame.pos = rayStart;
    }

    //keep resolving one collision at a time until we either run out of collisions
    //or run out of iterations, whichever happens first, then update position and velocity
    float timeRemaining = 1; //0 = no distance, 1 = full distance of rayDir
    bool stillColliding = true; //if we have not resolved all collisions by the end of the loop
    for (int iter = 0; iter < 4; ++iter) {
        RaycastResult cast = raycast(lines, arcs, rayStart, rayDir);

        //early exit if no collision
        if (cast.tmin >= 1) {
            stillColliding = false;
            break;
        }

        //resolve collision
        float epsilon = 0.001f; //the amount by which we push the collision point along the normal
                                //after calculating it, to avoid floating point problems
        rayStart += rayDir * cast.tmin;
        Vec2 epsilonVector = cast.normal * epsilon * 2;
        RaycastResult result = raycast(lines, arcs, rayStart, epsilonVector);
        rayStart += epsilonVector * result.tmin * 0.5f;

        //calculate exit vector (by reflecting the leftover movement vector)
        Vec2 incoming = rayDir * (1 - cast.tmin); //<- the leftover movement vector
        //blend between incoming and reflected movement vectors based on bounciness
        //bounciness: -1 = preserve velocity, 0 = fully plastic, 1 = fully elastic
        //TODO: allow bounciness < 0?
        //TODO: do we even need the tangent, or can we change this to use the normal directly?
        Vec2 tangent = { cast.normal.y, -cast.normal.x };
        rayDir = incoming + (bounce + 1) * (dot(incoming, tangent) * tangent - incoming);

        timeRemaining *= 1 - cast.tmin;

        if (normals) {
            normals->add(cast.normal);
        }
    }

    if (!stillColliding) {
        rayStart += rayDir;
    }

    //NOTE: We do a discrete collision response step after the continuous collision step
    //      to fix a bug where the player can push themselves out of bounds when walking into
    //      corners less than 90 degrees due to floating point imprecision.
    for (int iter = 0; iter < 4; ++iter) {
        Vec2 push = {};
        for (LineSegment line : lines) {
            //only eject if we are on the "inside" of the line
            if (cross(rayStart - line.a, line.b - line.a) >= 0) {
                //project the player position onto the line
                float segmentDelta = dot(rayStart - line.a, noz(line.b - line.a));
                //check whether the projected point is within the bounds of the segment
                if (segmentDelta >= 0 && segmentDelta <= len(line.b - line.a)) {
                    Vec2 closest = line.a + segmentDelta * noz(line.b - line.a);
                    float distanceFromLine = len(closest - rayStart);
                    if (distanceFromLine < PHYSICS_DISCRETE_RADIUS) {
                        Vec2 tangent = noz(line.b - line.a);
                        Vec2 normal = vec2(-tangent.y, tangent.x);
                        push += distanceFromLine * normal;
                    }
                }
            }
        }

        for (ArcConnector arc : arcs) {
            float distance = len(rayStart - arc.p);
            if (distance <= arc.r && distance > arc.r - PHYSICS_DISCRETE_RADIUS && angle_contains(arc, rayStart)) {
                Vec2 normal = noz(rayStart - arc.p);
                push += (arc.r - distance) * normal;
            }
        }
        if (push == vec2()) break;
        rayStart += push * 1.01f;

        //clamp velocity to be non-negative along the direction of the push
        if (dot(rayDir, push) < 0) {
            Vec2 normal = nor(push);
            rayDir -= dot(rayDir, normal) * normal;
        }
    }

    //store back results of collision logic
    *vel = rayDir / (tick * timeRemaining);
    *pos = rayStart;
}

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

unsigned int sfxrandi() {
    static unsigned int seed = (rand() | 0xf03ff033) * 214013 + 2531011;
    seed = seed * 214013 + 2531011;
    return (seed >> 8) ^ (seed << 24);
}
float sfxrand() {
    return (sfxrandi() & 16777215) / 16777216.0f;
}

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
        Graphics graphics = load_graphics();
    print_log("[] graphics init: %f seconds\n", get_time());
        if (int soloudError = loud.init(); soloudError) {
            printf("soloud init error: %d\n", soloudError);
        }
        loud.setMaxActiveVoiceCount(64);
    print_log("[] soloud init: %f seconds\n", get_time());
        loud.setGlobalVolume(1.0f);
        #define LOOPSTREAM(name) SoLoud::WavStream name; name.load("res/sound/" #name ".ogg"); name.setLooping(true);
        #define STEM(name) SoLoud::WavStream name; name.load("res/sound/stems/" #name ".ogg"); name.setLooping(true);
        LOOPSTREAM(forestscape);
        LOOPSTREAM(beachscape);
        LOOPSTREAM(plane_fire_loop);
        LOOPSTREAM(breath);
        LOOPSTREAM(fire_loop);
        STEM(brass);
        STEM(celli_long);
        STEM(celli_spiccato);
        STEM(clarinet);
        STEM(double_bass);
        STEM(elec_buzzyfifths);
        STEM(elec_buzzylow);
        STEM(elec_hihats);
        STEM(elec_kick);
        STEM(elec_modulated);
        STEM(elec_space);
        STEM(flute);
        STEM(percussion);
        STEM(piccolo);
        STEM(timpani);
        STEM(viola);
        STEM(violin);
        #define SFX(name) SoLoud::Wav sfx_ ## name; sfx_ ## name.load("res/sound/" #name ".ogg"); \
            sfx_ ## name.setLooping(false);
        SFX(axe_drop);
        SFX(axe_pickup);
        SFX(axe_swing);
        SFX(fire_lit);
        SFX(player_hurt);
        SFX(step_base1);
        SFX(step_base2);
        SFX(step_base3);
        SFX(step_grass_layer1_1);
        SFX(step_grass_layer1_2);
        SFX(step_grass_layer1_3);
        SFX(step_grass_layer1_4);
        SFX(step_grass_layer2_1);
        SFX(step_grass_layer2_2);
        SFX(step_grass_layer2_3);
        SFX(step_grass_layer2_4);
        SFX(step_grass_layer2_5);
        SFX(step_twig1);
        SFX(step_twig2);
        SFX(step_twig3);
        SFX(tinnitus);
        SFX(wood_chop1);
        SFX(wood_chop2);
        SFX(wood_chop3);
        SFX(wood_chop4);
        SFX(wood_chop5);
        SFX(wood_chop6);
        SFX(wood_chop7);
        SFX(wood1);
        SFX(wood2);
        SFX(wood3);
        SFX(wood4);
        SFX(zombie_aggro1);
        SFX(zombie_aggro2);
        SFX(zombie_belch);
        SFX(zombie_gasp);
        SFX(zombie_grunt1);
        SFX(zombie_grunt2);
        SFX(zombie_pain);
        SFX(zombie_scared);
        SFX(zombie_sleep1);
        SFX(zombie_sleep2);
        SFX(zombie_yell);

    print_log("[] audio init: %f seconds\n", get_time());
    resetWholeGame: //OOH HELL YEAH BOIIIIIII
		loud.stopAll();

        int forestscape_handle = loud.play(forestscape, 0.0f);
        int beachscape_handle = loud.play(beachscape, 0.0f);
        int plane_fire_loop_handle = loud.play(plane_fire_loop, 0.0f);
        int breath_handle = loud.play(breath, 0.0f);
        int fire_loop_handle = loud.play(fire_loop, 0.0f);

        int elec_buzzyfifths_handle = loud.play(elec_buzzyfifths, 0.001f);
        int elec_buzzylow_handle    = loud.play(elec_buzzylow   , 0.001f);
        int elec_hihats_handle      = loud.play(elec_hihats     , 0.001f);
        int elec_kick_handle        = loud.play(elec_kick       , 0.001f);
        int elec_modulated_handle   = loud.play(elec_modulated  , 0.001f);
        int elec_space_handle       = loud.play(elec_space      , 0.001f);
        int brass_handle            = loud.play(brass           , 0.001f);
        int celli_long_handle       = loud.play(celli_long      , 0.001f);
        int celli_spiccato_handle   = loud.play(celli_spiccato  , 0.001f);
        int clarinet_handle         = loud.play(clarinet        , 0.001f);
        int double_bass_handle      = loud.play(double_bass     , 0.001f);
        int flute_handle            = loud.play(flute           , 0.001f);
        int percussion_handle       = loud.play(percussion      , 0.001f);
        int piccolo_handle          = loud.play(piccolo         , 0.001f);
        int timpani_handle          = loud.play(timpani         , 0.001f);
        int viola_handle            = loud.play(viola           , 0.001f);
        int violin_handle           = loud.play(violin          , 0.001f);

        bool played_tinnitus = false;
        Level level = load_level("res/map/island.json", "res/map/island.ogmo");
        float frameTimes[100] = {};
        float thisTime = get_time();
        float lastTime = 0;
        float accumulator = 0;
        float globalAttackCooldown = 0; //lol game jam code amirite

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

        Vec2 camPos = level.player.pos;
        float playerHealth = 1; //in range [0, 1]
        float playerDamageTimer = 0;
        float gameOverTimer = 0; //for both dead and win
        float fadeInTimer = 0;
        bool dead = false;
        bool win = false;
        bool lightingEnabled = true; //convenient to be able to toggle this for debug purposes
        bool boatIsReady = false;

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

        // @Volatile: need to maintain this function alongside the interact code. -phil
        auto filter_player_can_interact = [&] (EntityType target_type) {
            Entity & held = level.player.held;
            if (held.type == ENT_TORCH && target_type == ENT_WOOD) {
                if (held.torchTimer > 0) {
                    return true;
                }
            } else if (held.type == ENT_TORCH && target_type == ENT_FIRE) {
                return true;
            } else if (held.type == ENT_WOOD && target_type == ENT_FIRE) {
                return true;
            } else if (held.type == ENT_AXE && target_type == ENT_TREE) {
                return true;
            } else if (held.type == ENT_TORCH && target_type == ENT_FIRESRC) {
                return true;
            } else if (held.type == ENT_BATTERY && target_type == ENT_BOAT) {
                return true;
            } else if (held.type == ENT_NONE && target_type == ENT_BOAT) {
                return boatIsReady;
            } else if (held.type == ENT_AXE && target_type == ENT_WOOD) {
                return false; // logs get in the way of chopping a tree
            } else if (held.type == target_type) {
                return false; // we're just swapping out apples with apples -phil
            } else if (holdable[target_type]) {
                return true;
            }
            return false;
        };

        auto playPitched = [&] (SoLoud::Wav & sfx, float vol) { // TODO: panning -phil
            loud.setRelativePlaySpeed(loud.play(sfx, vol), 0.9f + 0.2f*sfxrand());
        };
        auto playPitchedPanned = [&] (SoLoud::Wav & sfx, float vol, float pan) { // TODO: panning -phil
            pan = fmaxf(-1, fminf(1, pan));
            int handle = loud.play(sfx, vol);
            loud.setRelativePlaySpeed(handle, 0.9f + 0.2f*sfxrand());
            loud.setPan(handle, pan);
        };
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
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                Vec2 mousePos = vec2((float)mx * canvas.width / gameWidth, (float)my * canvas.height / gameHeight);
                if (dead && contains(tryAgainRect, mousePos)) {
                    goto resetWholeGame;
                }

                if (level.player.sprintRetriggerTimer <= 0) {
                    //update posision of held item based on player facing
                    int mx, my;
                    SDL_GetMouseState(&mx, &my);
                    mx = ((float)mx * canvas.width) / gameWidth;
                    my = ((float)my * canvas.height) / gameHeight;
                    int offx = camPos.x - canvas.width / 2;
                    int offy = camPos.y - canvas.height / 2;
                    Vec2 facing = noz(vec2(mx, my) - (level.player.pos - vec2(offx, offy + 16)));

                    int hoveredEntity = get_nearest_entity(level, level.player.pos + facing * 10 - vec2(0, 8), 20,
                        filter_player_can_interact);
                    if (hoveredEntity >= 0) {
                        Entity & target = level.entities[hoveredEntity];
                        Entity & held = level.player.held;
                        if (held.type == ENT_TORCH && target.type == ENT_WOOD) {
                            if (held.torchTimer > 0) {
                                target.type = ENT_FIRE;
                                target.fireTimer = FIRE_LOG_TIME;
                                target.lit = true;

                                playPitched(sfx_fire_lit, 0.5f);
                            }
                        } else if (held.type == ENT_TORCH && target.type == ENT_FIRE) {
                            if (!target.lit && target.fireTimer > 0 && held.torchTimer > 0) {
                                target.lit = true;
                                playPitched(sfx_fire_lit, 0.5f);
                            } else if (target.lit) {
                                held.torchTimer = TORCH_LIFETIME;
                                playPitched(sfx_fire_lit, 0.25f);
                            }
                        } else if (held.type == ENT_WOOD && target.type == ENT_FIRE) {
                            if (target.fireTimer < FIRE_LOG_TIME * 4) {
                                target.fireTimer += FIRE_LOG_TIME;
                                held = {};
                                if (target.lit) playPitched(sfx_fire_lit, 0.3f);
                                float v = 0.4f;
                                switch (sfxrandi() % 4) {
                                    case 0: playPitched(sfx_wood1, v); break;
                                    case 1: playPitched(sfx_wood2, v); break;
                                    case 2: playPitched(sfx_wood3, v); break;
                                    case 3: playPitched(sfx_wood4, v); break;
                                }
                            }
                        } else if (held.type == ENT_AXE && target.type == ENT_TREE) {
                            level.entities.add({ ENT_WOOD, target.pos + rotate(rand_float(TAU)) * vec2(10, 0) });
                            target.woodLeft -= 1;
                            //TODO: proper tree felling animation
                            if (target.woodLeft == 0) {
                                level.entities.remove(hoveredEntity);
                            }
                            switch (sfxrandi() % 4) {
                                case 0: playPitched(sfx_wood_chop1, 0.4f); break;
                                case 1: playPitched(sfx_wood_chop2, 0.4f); break;
                                case 2: playPitched(sfx_wood_chop3, 0.4f); break;
                                case 3: playPitched(sfx_wood_chop4, 0.4f); break;
                            }
                        } else if (held.type == ENT_BATTERY && target.type == ENT_BOAT) {
                            boatIsReady = true;
                            held = {};
                        } else if (held.type == ENT_NONE && target.type == ENT_BOAT) {
                            if (boatIsReady) win = true;
                        } else if (held.type == ENT_TORCH && target.type == ENT_FIRESRC) {
                            held.torchTimer = TORCH_LIFETIME;
                            playPitched(sfx_fire_lit, 0.5f);
                        } else if (held.type == target.type) {
                            // we're just swapping out apples with apples -phil
                        } else if (holdable[target.type]) {
                            //move held entity to ground
                            if (held.type != ENT_NONE) {
                                level.entities.add(held);
                                level.player.sprintRetriggerTimer = SPRINT_RETRIGGER_TIME;
                                if (held.type == ENT_BATTERY) level.player.sprintRetriggerTimer *= 8;
                            }

                            //move selected entity to hand
                            //IMPORTANT: `target` reference may have been invalidated, se we can't use it
                            held = level.entities[hoveredEntity];
                            level.entities.remove(hoveredEntity);
                            if (held.type == ENT_AXE) {
                                playPitched(sfx_axe_pickup, 0.3f);
                            } else if (held.type == ENT_TORCH) {
                                playPitched(sfx_wood4, 0.1f);
                                playPitched(sfx_fire_lit, 0.1f);
                            } else if (held.type == ENT_WOOD) {
                                float v = 0.4f;
                                switch (sfxrandi() % 4) {
                                    case 0: playPitched(sfx_wood1, v); break;
                                    case 1: playPitched(sfx_wood2, v); break;
                                    case 2: playPitched(sfx_wood3, v); break;
                                    case 3: playPitched(sfx_wood4, v); break;
                                }
                            }
                        }
                        nan_check(level);
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                Entity & held = level.player.held;
                if (held.type != ENT_NONE) {
                    // play sounds to drop the object -phil
                    if (held.type == ENT_AXE) {
                        playPitched(sfx_axe_drop, 0.2f);
                    } else if (held.type == ENT_WOOD) {
                        float v = 0.1f;
                        switch (sfxrandi() % 4) {
                            case 0: playPitched(sfx_wood1, v); break;
                            case 1: playPitched(sfx_wood2, v); break;
                            case 2: playPitched(sfx_wood3, v); break;
                            case 3: playPitched(sfx_wood4, v); break;
                        }
                    }

                    if (held.type != ENT_TORCH) { // thump to the ground -phil
                        float v = 0.2f;
                        switch (sfxrandi() % 3) {
                            case 0: playPitched(sfx_step_base1, v); break;
                            case 1: playPitched(sfx_step_base2, v); break;
                            case 2: playPitched(sfx_step_base3, v); break;
                        }
                    }

                    level.player.sprintRetriggerTimer = SPRINT_RETRIGGER_TIME;
                    if (held.type == ENT_BATTERY) level.player.sprintRetriggerTimer *= 8;
                    level.entities.add(held);
                    held = {};
                }
                nan_check(level);
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

            for (Entity & e : level.entities) {
                e.animationTimer += tick;
            }
            for (Enemy & e : level.enemies) {
                e.animationTimer += tick;
            }
            level.player.held.animationTimer += tick;

            //player movement
            level.player.sprintRetriggerTimer -= tick;
            float playerSpeed = 40; //nice
            //change player speed based on held item
            switch (level.player.held.type) {
                case ENT_NONE: playerSpeed = 70; break;
                case ENT_BATTERY: playerSpeed = 25; break;
                case ENT_TORCH: playerSpeed = 50; break;
                default: break;
            }
            float playerAnimationSpeed = playerSpeed / 40;
            float playerAccel = 1000;
            level.player.animationTimer += tick * playerAnimationSpeed;
            Vec2 input = vec2(HELD(D) - HELD(A), HELD(S) - HELD(W));
            if (dead) {
                input = {};
            }
            level.player.vel += noz(input) * playerAccel * tick;
            nan_check(level);
            level.player.vel -= level.player.vel * 4 * tick;
            nan_check(level);
            level.player.vel -= noz(level.player.vel) * fminf(120 * tick, len(level.player.vel));
            nan_check(level);
            if (len(level.player.vel) > playerSpeed) level.player.vel = nor(level.player.vel) * playerSpeed;
            nan_check(level);
            Vec2 kb1 = level.player.knockbackVel;
            level.player.knockbackVel -= level.player.knockbackVel * 4 * tick;
            nan_check(level);
            Vec2 kb2 = level.player.knockbackVel;
            float kblen = len(level.player.knockbackVel);
            Vec2 kbnoz = noz(level.player.knockbackVel);
            Vec2 wholeExpr = noz(level.player.knockbackVel) * fminf(120 * tick, len(level.player.knockbackVel));
            level.player.knockbackVel -= noz(level.player.knockbackVel) * fminf(120 * tick, len(level.player.knockbackVel));
            nan_check(level);
            List<LineSegment> lines = {};
            List<ArcConnector> arcs = {};
            Vec2 move = (level.player.vel + level.player.knockbackVel) * tick;
            add_collision_volumes(level, graphics, lines, arcs, { level.player.pos, 5 }, move);
            collide_with_volumes(level.player.pos, move, &level.player.pos, &level.player.vel,
                lines, arcs, nullptr, tick, 0.1f);
            nan_check(level);
            lines.finalize();
            arcs.finalize();

            //update posision of held item based on player facing
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            mx = ((float)mx * canvas.width) / gameWidth;
            my = ((float)my * canvas.height) / gameHeight;
            int offx = camPos.x - canvas.width / 2;
            int offy = camPos.y - canvas.height / 2;
            Vec2 facing = noz(vec2(mx, my) - (level.player.pos - vec2(offx, offy + 16)));
            //TODO: figure out how to make this rotate in an oval around the player, instead of a pure circle
            //      (taking into account that it still needs to face the mouse correctly)
            level.player.held.pos = level.player.pos + facing * 12;

            if (len(level.player.vel) < 10.0f) {
                level.player.animationTimer = 0;
            }
            if (level.player.vel.x < -10) {
                level.player.flip = true;
            } else if (level.player.vel.x > 10) {
                level.player.flip = false;
            }

            nan_check(level);

            //follow cam
            const float radius = 10;
            Vec2 followPoint = level.player.pos + facing * 20;
            if (len(camPos - followPoint) > radius) {
                float easeLen = lerp(len(camPos - followPoint), tick * 5/*arbitrary magic number?*/, radius);
                camPos = followPoint + setlen(camPos - followPoint, easeLen);
            }

            //enemy movement behavior
            const float enemySpeed = 60;
            for (Enemy & enemy : level.enemies) {
                //make enemies repel one another
                Vec2 desired_direction = noz(level.player.pos - enemy.pos) * 20;
                for (Enemy & e : level.enemies) {
                    if (e.pos != enemy.pos) {
                        float dist = len(e.pos - enemy.pos);
                        float outerRadius = 20;
                        float innerRadius = 15;
                        float repulsion = fmaxf(0, fminf(1, (outerRadius - dist) / (outerRadius - innerRadius)));
                        enemy.pos += nor(enemy.pos - e.pos) * repulsion * 60 * tick;
                    }
                }

                nan_check(level);

                auto do_entity = [&] (Entity & e) {
                    if (e.type != ENT_FIRE && e.type != ENT_TORCH && e.type != ENT_FIRESRC) {
                        return;
                    }
                    float effective_radius = 0;
                    if (e.type == ENT_TORCH) {
                        effective_radius = TORCH_RADIUS * sqrt(e.torchTimer / TORCH_LIFETIME);
                    } else if (e.type == ENT_FIRE) {
                        effective_radius =
                            e.lit? fmaxf(1, FIRE_RADIUS * sqrtf(e.fireTimer / FIRE_TOTAL_TIME) * 0.8f + 10) : 1;
                    } else if (e.type == ENT_FIRESRC) {
                        effective_radius = FIRE_RADIUS;
                    } else {
                        assert(0);
                    }
                    if (e.pos != enemy.pos) {
                        float dist = len(e.pos - enemy.pos);
                        if (dist > 0 && effective_radius > 0) {
                            float outerRadius = effective_radius * 1.125f;
                            float innerRadius = effective_radius * 0.875f;
                            float repulsion = fmaxf(0, fminf(1, (outerRadius - dist) / (outerRadius - innerRadius)));
                            desired_direction += nor(enemy.pos - e.pos) * repulsion * 1000;
                        }
                    }
                };
                for (Entity & e : level.entities) {
                    do_entity(e);
                }
                nan_check(level);
                if (level.player.held.type) {
                    do_entity(level.player.held);
                }
                nan_check(level);
                enemy.vel += noz(desired_direction) * 80.0f * tick;
                if (len(enemy.vel) > 0) enemy.vel *= fminf(len(enemy.vel), enemySpeed) / len(enemy.vel);
                nan_check(level);
            }

            //enemy collision
            for (Enemy & enemy : level.enemies) {

                enemy.chatterTimer -= tick;
                if (enemy.chatterTimer < 0) {
                    enemy.chatterTimer = 1.0f;

                    if (enemy.is_aggroed) {
                        if (sfxrandi() % 10 == 0) {
                            float v = 1.0f;
                            v *= fmaxf(0, fminf(1, len(enemy.pos - level.player.pos) / 800));
                            switch (sfxrandi() % 2) {
                            case 0: playPitchedPanned(sfx_zombie_grunt1, v, (enemy.pos.x - level.player.pos.x) / 200); break;
                            case 1: playPitchedPanned(sfx_zombie_grunt2, v, (enemy.pos.x - level.player.pos.x) / 200); break;
                            }
                        }
                    } else {
                        if (sfxrandi() % 20 == 0) {
                            float v = 0.2f;
                            v *= fmaxf(0, fminf(1, len(enemy.pos - level.player.pos) / 800));
                            switch (sfxrandi() % 2) {
                            case 0: playPitchedPanned(sfx_zombie_sleep1, v, (enemy.pos.x - level.player.pos.x) / 200); break;
                            case 1: playPitchedPanned(sfx_zombie_sleep2, v, (enemy.pos.x - level.player.pos.x) / 200); break;
                            }
                        }
                    }
                }

                if (len(enemy.pos - level.player.pos) > 200) {
                    enemy.is_aggroed = false;
                    continue;
                } else {
                    enemy.is_aggroed = true;
                }
                if (enemy.attackCooldown > ATTACK_COOLDOWN - 0.666f) {
                    continue;
                }

                if (!enemy.has_ever_aggroed) {
                    float v = 0.4f;
                    switch (sfxrandi() % 2) {
                        case 0: playPitchedPanned(sfx_zombie_aggro1, v, (enemy.pos.x - level.player.pos.x) / 200); break;
                        case 1: playPitchedPanned(sfx_zombie_aggro2, v, (enemy.pos.x - level.player.pos.x) / 200); break;
                    }
                    enemy.has_ever_aggroed = true;
                    enemy.chatterTimer = 1.0f;
                }

                List<LineSegment> lines = {};
                List<ArcConnector> arcs = {};
                add_collision_volumes(level, graphics, lines, arcs, { enemy.pos, 5 }, enemy.vel * tick);
                //TODO: why do we multiply the velocity by the tick but also pass in the tick?
                collide_with_volumes(enemy.pos,
                    enemy.vel * tick, &enemy.pos, &enemy.vel, lines, arcs, nullptr, tick, 0.1f);
                nan_check(level);
                lines.finalize();
                arcs.finalize();
            }

            //enemy attacks
            for (Enemy & e : level.enemies) {
                if (globalAttackCooldown == 0 && e.attackCooldown == 0 && len(e.pos - level.player.pos) < 20) {
                    //gameplay logic
                    playerHealth -= 0.3f; //TODO: change this back
                    playerDamageTimer = 1;
                    e.attackCooldown = ATTACK_COOLDOWN;
                    globalAttackCooldown = 0.5f;

                    //graphics logic
                    level.attacks.add({ e.pos, noz(level.player.pos - e.pos) });
                    //TODO: screenshake?
                    playPitched(sfx_player_hurt, 0.2f);

                    //TODO: sound effect
                    playPitchedPanned(sfx_zombie_yell, 0.4f, (e.pos.x - level.player.pos.x) / 200);

                    //move player away from enemies, with collision (knockback)
                    level.player.knockbackVel += noz(level.player.pos - e.pos) * 80;
                    nan_check(level);

                    // List<LineSegment> lines = {};
                    // List<ArcConnector> arcs = {};
                    // Vec2 knockback = noz(level.player.pos - e.pos) * 15;
                    // add_collision_volumes(level, graphics, lines, arcs, { level.player.pos, 5 }, knockback);
                    // //TODO: why do we multiply the velocity by the tick but also pass in the tick?
                    // collide_with_volumes(level.player.pos,
                    //     knockback, &level.player.pos, &level.player.vel, lines, arcs, nullptr, tick, 0.1f);
                    // lines.finalize();
                    // arcs.finalize();
                }
                e.attackCooldown = fmaxf(0, e.attackCooldown - tick);
            }
            globalAttackCooldown = fmaxf(0, globalAttackCooldown - tick);

            for (int i = 0; i < level.attacks.len;) {
                level.attacks[i].timer += tick;
                if (level.attacks[i].timer > 4 / 6.0f) {
                    level.attacks.remove(i);
                } else {
                    ++i;
                }
            }

            //DEBUG toggle lighting
            // if (DOWN(L)) lightingEnabled = !lightingEnabled;

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

            //update entity timers
            for (Entity & e : level.entities) {
                e.torchTimer = fmaxf(0, e.torchTimer - tick);
                if (e.lit) {
                    e.fireTimer = fmaxf(0, e.fireTimer - tick);
                    if (e.fireTimer == 0) e.lit = false;
                }
                if (e.type == ENT_WOOD || e.type == ENT_TORCH) {
                    for (Entity & e2 : level.entities) {
                        if (e2.pos != e.pos) {
                            float dist = len(e2.pos - e.pos);
                            float outerRadius = 15;
                            float innerRadius = 10;
                            if (e2.type == ENT_FIRESRC) {
                                outerRadius = 20;
                                innerRadius = 15;
                            }
                            float repulsion = fmaxf(0, fminf(1, (outerRadius - dist) / (outerRadius - innerRadius)));
                            e.pos += nor(e.pos - e2.pos) * repulsion * 60 * tick;
                        }
                    }
                }
            }
            level.player.held.torchTimer = fmaxf(0, level.player.held.torchTimer - tick);

            if (len(level.player.vel) <= 10) {
                level.player.sfx_step_base_timer = 1.0f; // quarter of whatever step time we're gonna use
            } else {
                float step_time = 0.4f / playerAnimationSpeed;
                level.player.sfx_step_base_timer -= tick / step_time;
                level.player.sfx_step_grass_layer1_timer -= tick / step_time;
                level.player.sfx_step_grass_layer2_timer -= tick / step_time;

                if (level.player.sfx_step_base_timer < 0) {
                    level.player.sfx_step_base_timer += 1;

                    float v = 0.1f;
                    switch (sfxrandi() % 3) {
                        case 0: playPitched(sfx_step_base1, v); break;
                        case 1: playPitched(sfx_step_base2, v); break;
                        case 2: playPitched(sfx_step_base3, v); break;
                    }
                }
                if (level.player.sfx_step_grass_layer1_timer < 0) {
                    level.player.sfx_step_grass_layer1_timer += 0.5f + 1.0f*sfxrand();
                    float v = 0.06f;
                    switch (sfxrandi() % 4) {
                        case 0: playPitched(sfx_step_grass_layer1_1, v); break;
                        case 1: playPitched(sfx_step_grass_layer1_2, v); break;
                        case 2: playPitched(sfx_step_grass_layer1_3, v); break;
                        case 3: playPitched(sfx_step_grass_layer1_4, v); break;
                    }
                }
                if (level.player.sfx_step_grass_layer2_timer < 0) {
                    level.player.sfx_step_grass_layer2_timer += 0.5f + 1.0f*sfxrand();
                    float v = 0.1f;
                    switch (sfxrandi() % 5) {
                        case 0: playPitched(sfx_step_grass_layer2_1, v); break;
                        case 1: playPitched(sfx_step_grass_layer2_2, v); break;
                        case 2: playPitched(sfx_step_grass_layer2_3, v); break;
                        case 3: playPitched(sfx_step_grass_layer2_4, v); break;
                        case 4: playPitched(sfx_step_grass_layer2_5, v); break;
                    }
                }
            }

            //handle player health
            playerHealth = fminf(1, playerHealth + 0.05f * tick);
            playerDamageTimer = fmaxf(0, playerDamageTimer - tick / 0.25f);
            if (playerHealth < 0) {
                dead = true;
                if (!played_tinnitus) {
                    playPitched(sfx_tinnitus, 0.3f);
                    played_tinnitus = true;
                }
            }
            if (dead || win) gameOverTimer += tick;
            fadeInTimer += tick;

            //DEBUG health manipulation
            //if (DOWN(P)) { playerHealth -= 0.3f; playerDamageTimer = 1; }
            //if (DOWN(O)) win = true;

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

        for (int y = 0; y < lights.height; ++y) {
            for (int x = 0; x < lights.width; ++x) {
                lights[y][x] = { 25, 26, 32, 255 }; // Carefully chosen based on dithering levels
                // lights[y][x] = { 32, 40, 64, 255 };
            }
        }

        int mx, my;
        SDL_GetMouseState(&mx, &my);
        mx = ((float)mx * canvas.width) / gameWidth;
        my = ((float)my * canvas.height) / gameHeight;
        int offx = camPos.x - canvas.width / 2;
        int offy = camPos.y - canvas.height / 2;
        Vec2 facing = noz(vec2(mx, my) - (level.player.pos - vec2(offx, offy + 16)));

        draw_level(level, canvas, lights, graphics, offx, offy);

        //quantize lighting (continuous -> discreet light levels)
        static const int ditherKernel[8 * 8] = {
             0, 48, 12, 60,  3, 51, 15, 63,
            32, 16, 44, 28, 35, 19, 47, 31,
             8, 56,  4, 52, 11, 59,  7, 55,
            40, 24, 36, 20, 43, 27, 39, 23,
             2, 50, 14, 62,  1, 49, 13, 61,
            34, 18, 46, 30, 33, 17, 45, 29,
            10, 58,  6, 54,  9, 57,  5, 53,
            42, 26, 38, 22, 41, 25, 37, 21,
        };
        for (int y = 0; y < lights.height; ++y) {
            for (int x = 0; x < lights.width; ++x) {
                // int dither = rand() & 63;
                int dither = ditherKernel[(y & 7) * 8 + (x & 7)];
                // int dither = ditherKernel[(y * x) & 63];
                // int dither = 0;
                #if 0
                if (lights[y][x].r + (dither >> 2) < 32) {
                    // int noise = rand() & 31;
                    // lights[y][x] = { (u8) (12 + noise), (u8) (20 + noise), (u8) (44 + noise), 255 };
                    lights[y][x] = { 32, 40, 64, 255 };
                } else if (lights[y][x].r + dither < 192) {
                    // int noise = rand() & 15;
                    // lights[y][x] = { (u8) (72 + noise), (u8) (72 + noise), (u8) (72 + noise), 255 };
                    lights[y][x] = { 80, 80, 80, 255 };
                } else {
                    lights[y][x] = { 255, 192, 96, 255 };
                }
                #else
                int r = rand() & 31;
                auto ditherize = [&] (u8 *px, int dither) {
                    if (*px + 1*(dither >> 3) < 32) {
                        *px = 8 + (r);
                    } else if (*px + 1*(dither >> 2) < 64) {
                        *px = 30 + (r >> 5);
                    //} else if (*px + 1*(dither >> 2) < 96) {
                    //    *px = 80;
                    } else if (*px + 1*(dither >> 1) < 128) {
                        *px = 112;
                    //} else if (*px + 1*(dither >> 1) < 160) {
                    //    *px = 144;
                    } else if (*px + 1*(dither >> 0) < 192) {
                        *px = 192;
                    //} else if (*px + 1*(dither >> 0) < 224) {
                    //    *px = 176;
                    } else {
                        *px = 240;
                    }
                };
                ditherize(&lights[y][x].r, dither);
                ditherize(&lights[y][x].g, dither);
                ditherize(&lights[y][x].b, dither);
                #endif
            }
        }

        //optionally blur lighting results
        static const int blurKernel[7] = { 1, 6, 15, 20, 15, 6, 1 };
        for (int i = 0; i < 0; ++i) {
            for (int y = 0; y < light2.height; ++y) {
                for (int x = 0; x < light2.width; ++x) {
                    Color c = {};
                    for (int dx = 0; dx < 7; ++dx) {
                        int xx = x + dx - 3;
                        c.r += (lights[y][xx].r * blurKernel[dx]) >> 6;
                        c.g += (lights[y][xx].g * blurKernel[dx]) >> 6;
                        c.b += (lights[y][xx].b * blurKernel[dx]) >> 6;
                        c.a += (lights[y][xx].a * blurKernel[dx]) >> 6;
                    }
                    light2[y][x] = c;
                }
            }
            for (int y = 0; y < lights.height; ++y) {
                for (int x = 0; x < lights.width; ++x) {
                    Color c = {};
                    for (int dy = 0; dy < 7; ++dy) {
                        int yy = y + dy - 3;
                        c.r += (light2[yy][x].r * blurKernel[dy]) >> 6;
                        c.g += (light2[yy][x].g * blurKernel[dy]) >> 6;
                        c.b += (light2[yy][x].b * blurKernel[dy]) >> 6;
                        c.a += (light2[yy][x].a * blurKernel[dy]) >> 6;
                    }
                    lights[y][x] = c;
                }
            }
        }

        // Apply lighting -phil
        if (lightingEnabled) {
            assert(lights.width == canvas.width);
            assert(lights.height == canvas.height);
            for (int y = 0; y < lights.height; ++y) {
                for (int x = 0; x < lights.width; ++x) {
                    // int r = lights[y][x].r < 48? rand() & 15 : 0;
                    int r = 0;
                    canvas[y][x].r = (canvas[y][x].r * lights[y][x].r) >> 8;
                    canvas[y][x].g = (canvas[y][x].g * lights[y][x].g) >> 8;
                    canvas[y][x].b = (canvas[y][x].b * lights[y][x].b) >> 8;
                    canvas[y][x].r = imin(255, canvas[y][x].r + r + (lights[y][x].r >> 3));
                    canvas[y][x].g = imin(255, canvas[y][x].g + r + (lights[y][x].g >> 3));
                    canvas[y][x].b = imin(255, canvas[y][x].b + r + (lights[y][x].b >> 3));
                }
            }
        }

        //DEBUG draw collision volumes
        // recordingPhysicsFrames = true;
        // for (LineSegment & line : debugPhysicsFrame.lines) {
        //     draw_line(canvas, line.a.x - offx, line.a.y - offy, line.b.x - offx, line.b.y - offy, { 255, 0, 255, 127 });
        // }
        // for (ArcConnector & arc : debugPhysicsFrame.arcs) {
        //     draw_oval(canvas, arc.p.x - offx, arc.p.y - offy, arc.r, arc.r, { 255, 0, 255, 127 });
        // }
        // draw_oval(canvas, debugPhysicsFrame.pos.x - offx, debugPhysicsFrame.pos.y - offy, 3, 3, { 0, 255, 0, 255 });

        // DYNAMIC SOUND ENGINE -phil
        {
            //loud.setVolume(forestscape_handle, sinf(4*get_time())/2+1);
            //forestscape
            //beachscape
            //plane_fire_loop
            //breath
            //fire_loop
            //brass
            //celli_long
            //celli_spiccato
            //clarinet
            //double_bass
            //elec_buzzyfifths
            //elec_buzzylow
            //elec_hihats
            //elec_kick
            //elec_modulated
            //elec_space
            //flute
            //percussion
            //piccolo
            //timpani
            //viola
            //violin
            float player_light = 0;
            static const float DISTANCE_FROM_LIGHT_CONSIDERED_SCARY_DARKNESS = 1.0f; // multiple of radius
            for (Entity & e : level.entities) {
                if (e.type != ENT_FIRE && e.type != ENT_TORCH && e.type != ENT_FIRESRC) continue;
                // @volatile in case we add more light sources -phil
                float radius_to_use   = e.type == ENT_TORCH ? TORCH_RADIUS : FIRE_RADIUS;
                float lifetime_to_use = e.type == ENT_TORCH ? TORCH_LIFETIME : FIRE_TOTAL_TIME;
                float timer_to_use    =
                    e.type == ENT_TORCH ? e.torchTimer : e.type == ENT_FIRESRC ? FIRE_TOTAL_TIME : e.fireTimer;
                float r = timer_to_use / lifetime_to_use;
                float dist = len(level.player.pos - e.pos) / radius_to_use;
                player_light += fmaxf(0, DISTANCE_FROM_LIGHT_CONSIDERED_SCARY_DARKNESS - dist) * r;
            }
            if (level.player.held.type == ENT_TORCH) {
                float r = level.player.held.torchTimer / TORCH_LIFETIME;
                float dist = len(level.player.pos - level.player.held.pos) / TORCH_RADIUS;
                player_light += fmaxf(0, DISTANCE_FROM_LIGHT_CONSIDERED_SCARY_DARKNESS - dist) * r;
            }
            //printf("%f\n", player_light);
            float nearest_enemy = 1000;
            Vec2 near_enemies_vel = {};
            int num_near_enemies = 0;
            for (Enemy & e : level.enemies) {
                float d = len(level.player.pos - e.pos);
                if (d < nearest_enemy) {
                    nearest_enemy = d;
                    num_near_enemies++;
                    near_enemies_vel += e.vel;
                }
            }
            if (num_near_enemies) near_enemies_vel /= num_near_enemies;

            float health_factor = fmaxf(0, fminf(1, 1 - playerHealth));
            float light_factor = fmaxf(0.125f, fminf(1, 1 - player_light));
            float enemy_factor = 0.99f*fmaxf(0, fminf(1, 1 / (nearest_enemy * 0.005 + 1)));
            float player_velocity_factor = 0.0f*fmaxf(0, fminf(1, len(level.player.vel) / 70));
            float enemy_velocity_factor = fmaxf(0.75f, fminf(1, len(near_enemies_vel) / 70));

            float all_music_factors =
                (light_factor * health_factor + player_velocity_factor + enemy_factor * enemy_velocity_factor);
            //printf("%f\n", all_music_factors);

            all_music_factors = (all_music_factors - 0.25f) / (0.8f - 0.25f); // clamp from 0.1 to 2 and scale accordingly

            float music_volume = fmaxf(0, fminf(1, all_music_factors));
            float elec_music_volume = music_volume;
            float rest_music_volume = fmaxf(0, fminf(1, (music_volume - 0.5f) / (1 - 0.5f)));
            //printf("%f\n", rest_music_volume);

            if (playerHealth <= 0) music_volume = 0;

            float beachiness = fmaxf(0, fminf(1, (850 - level.player.pos.y) / 200));
            float planefire = 0;
            for (Entity & e : level.entities) {
                 if (e.type != ENT_FIRESRC) continue;
                 planefire = len(e.pos - level.player.pos);
                 break;
            }

            float music_master = 1.0f;

            loud.setVolume(forestscape_handle     , 0.5f * (  beachiness));
            loud.setVolume(beachscape_handle      , 0.5f * (1-beachiness));
            loud.setVolume(plane_fire_loop_handle , 0.8f * (1- fmaxf(0, fminf(1, planefire / 150))));
            // no breath until 0.8 health, then scales up to max breath at 0 health -phil
            loud.setVolume(breath_handle          , playerHealth > 0 ? 0.5f * fmaxf(0, fminf(1, 1 - playerHealth / 0.8f)) : 0);
            loud.setVolume(fire_loop_handle       , 0.00f);
            loud.setVolume(elec_buzzyfifths_handle, music_master * elec_music_volume);
            loud.setVolume(elec_buzzylow_handle   , music_master * elec_music_volume);
            loud.setVolume(elec_hihats_handle     , music_master * elec_music_volume);
            loud.setVolume(elec_kick_handle       , music_master * elec_music_volume);
            loud.setVolume(elec_modulated_handle  , music_master * elec_music_volume);
            loud.setVolume(elec_space_handle      , music_master * elec_music_volume);
            loud.setVolume(brass_handle           , music_master * rest_music_volume);
            loud.setVolume(celli_long_handle      , music_master * rest_music_volume);
            loud.setVolume(celli_spiccato_handle  , music_master * rest_music_volume);
            loud.setVolume(clarinet_handle        , music_master * rest_music_volume);
            loud.setVolume(double_bass_handle     , music_master * rest_music_volume);
            loud.setVolume(flute_handle           , music_master * rest_music_volume);
            loud.setVolume(percussion_handle      , music_master * rest_music_volume);
            loud.setVolume(piccolo_handle         , music_master * rest_music_volume);
            loud.setVolume(timpani_handle         , music_master * rest_music_volume);
            loud.setVolume(viola_handle           , music_master * rest_music_volume);
            loud.setVolume(violin_handle          , music_master * rest_music_volume);

            if (fadeInTimer > 3) fadeInTimer = 3;
            float fadeOutTimer = fmaxf(0, fminf(3, 3 - gameOverTimer));
            float globalVolume = (fadeInTimer / 3) * (fadeOutTimer / 3);
            loud.setGlobalVolume(globalVolume * 0.666f);
        }

        //draw hovered entities
        int hoveredEntity = get_nearest_entity(level, level.player.pos + facing * 10 - vec2(0, 8), 20,
            filter_player_can_interact);
        if (hoveredEntity >= 0 && (level.player.sprintRetriggerTimer <= 0)) {
            Entity & e = level.entities[hoveredEntity];
            if (e.type == ENT_TORCH) {
                Tileset set = getTorchSprite(graphics, e.torchTimer);
                draw_animation_silhouette(canvas, set,
                    e.pos.x - offx - set.tileWidth / 2, e.pos.y - offy - set.tileHeight, { 255, 255, 255, 80 },
                    e.animationTimer);
            } else if (e.type == ENT_FIRE) {
                int x = e.pos.x - offx - graphics.logSprites.tileWidth / 2;
                int y = e.pos.y - offy - graphics.logSprites.tileHeight;
                draw_tile_silhouette(canvas, graphics.logSprites,
                    ceilf(e.fireTimer / FIRE_LOG_TIME), 0, x, y, { 255, 255, 255, 80 });
                if (e.lit) draw_tile_silhouette(canvas, graphics.fireSprites,
                    (int) (e.animationTimer / graphics.fireSprites.frame_time) % graphics.fireSprites.width,
                    0, x, y - 4, { 255, 255, 255, 80 });
            } else if (e.type == ENT_FIRESRC) {
                Tileset set = graphics.planeFire;
                draw_animation_silhouette(canvas, set,
                    e.pos.x - offx - set.tileWidth / 2, e.pos.y - offy - set.tileHeight, { 255, 255, 255, 120 },
                    e.animationTimer);
            } else {
                Image img = graphics.itemSprites[e.type];
                draw_sprite_silhouette(canvas,
                    img, e.pos.x - offx - img.width / 2, e.pos.y - offy - img.height, { 255, 255, 255, 80 });
            }
        }

        auto draw_text_centered = [] (Canvas canvas, MonoFont font, int cx, int cy, Color color, const char * text) {
            int len = strlen(text);
            int x = cx - font.glyphWidth * len / 2;
            int y = cy - font.glyphHeight / 2;
            draw_text(canvas, font, x, y, color, text);
        };

        //apply fullscreen overlays
        u8 redOpacity = imax(0, imin(255, 128 * (1 - playerHealth) + 64 * playerDamageTimer));
        if (redOpacity) draw_rect(canvas, 0, 0, canvas.width, canvas.height, { 255, 0, 0, redOpacity });

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

    finalize(level);
    printf("exiting game normally at %f seconds\n", get_time());
    return 0;
}
#ifdef _WIN32
int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const char * argv[] = {"", ""};
    main(0, (char **)argv);
}
#endif
