#ifndef LEVEL_HPP
#define LEVEL_HPP

#include "List.hpp"
#include "math.hpp"
#include "common.hpp"
#include "parson.h"
#include "pixel.hpp"
#include "trace.hpp"

static const int TILE_WIDTH = 16;
static const int TILE_HEIGHT = 16;
static const int TILE_TOTAL_HEIGHT = 16 * 4;

enum EntityType { ENT_NONE, ENT_WOOD, ENT_TORCH, ENT_AXE, ENT_FIRE, ENT_TREE, ENT_BATTERY, ENT_FIRESRC, ENT_BOAT, NUM_ENTS };
static const bool holdable[NUM_ENTS] = { [ENT_WOOD] = true, [ENT_TORCH] = true, [ENT_AXE] = true, [ENT_BATTERY] = true };
struct Graphics {
    Tileset tileset;
    bool * isGround; //used for depth sorting when rendering
    Image itemSprites[NUM_ENTS];
    Tileset torchSprites[4];
    Tileset logSprites;
    Tileset fireSprites;
    Image player;
    Tileset player_walk;
    Tileset player_walk_log;
    Tileset player_walk_axe;
    Tileset enemy;
    Image attack[4]; //ideally should be a tileset but YOLO
    Tileset planeFire;
    Image plane;
    Image planeBehindFire;
};

Graphics load_graphics() {
    Graphics g = {};
    g.tileset = load_tileset("res/tiles.png", TILE_WIDTH, TILE_TOTAL_HEIGHT);
    g.isGround = (bool *) malloc(g.tileset.width * sizeof(bool));
    for (int i = 0; i < g.tileset.width; ++i) { g.isGround[i] = true; }
    g.isGround[15] = g.isGround[14] = g.isGround[13] = g.isGround[12] = g.isGround[11] = g.isGround[10]
        = g.isGround[9] = g.isGround[8] = false;
    g.itemSprites[ENT_WOOD] = load_image("res/log.png");
    g.itemSprites[ENT_TORCH] = load_image("res/torchBasic.png");
    g.itemSprites[ENT_AXE] = load_image("res/axe.png");
    g.itemSprites[ENT_FIRE] = load_image("res/campfire.png");
    g.itemSprites[ENT_TREE] = load_image("res/tree-placeholder.png");
    g.itemSprites[ENT_BATTERY] = load_image("res/battery.png");
    g.itemSprites[ENT_FIRESRC] = load_image("res/fireButBetter.png");
    g.itemSprites[ENT_BOAT] = load_image("res/boat.png");
    g.torchSprites[0] = load_tileset("res/torchDead.png", 16, 16, 1 / 12.0f);
    g.torchSprites[1] = load_tileset("res/torchMin.png", 16, 16, 1 / 12.0f);
    g.torchSprites[2] = load_tileset("res/torchMedium.png", 16, 16, 1 / 12.0f);
    g.torchSprites[3] = load_tileset("res/torchFull.png", 16, 16, 1 / 12.0f);
    g.logSprites = load_tileset("res/5logSpriteSheet.png", 16, 16);
    g.fireSprites = load_tileset("res/fire.png", 16, 16, 1 / 12.0f);
    g.player = load_image("res/player_idle.png");
    g.player_walk = load_tileset("res/player_walk.png", 16, 32, 1 / 10.0f);
    g.player_walk_log = load_tileset("res/player_walk_log.png", 16, 32, 1 / 10.0f);
    g.player_walk_axe = load_tileset("res/player_walk_axe.png", 16, 32, 1 / 10.0f);
    g.enemy = load_tileset("res/wraith.png", 19, 29, 1 / 6.0f);
    // g.attack = load_tileset("res/attack.png", 24, 16, 1 / 8.0f);
    g.attack[0] = load_image("res/attack-frames1.png");
    g.attack[1] = load_image("res/attack-frames2.png");
    g.attack[2] = load_image("res/attack-frames3.png");
    g.attack[3] = load_image("res/attack-frames4.png");
    g.planeFire = load_tileset("res/fireForThePlaneCrash2.png", 32, 32, 1 / 8.0f);
    g.plane = load_image("res/plane.png");
    g.planeBehindFire = load_image("res/plane_behind_fire.png");
    return g;
}



static const float TORCH_LIFETIME = 30;
static const float TORCH_RADIUS = 80;
static const float FIRE_RADIUS = 200;
static const float FIRE_LOGS = 5;
static const float FIRE_LOG_TIME = 20;
static const float FIRE_TOTAL_TIME = FIRE_LOGS * FIRE_LOG_TIME;
static const int TREE_HITS = 6;
struct Entity {
    EntityType type;
    Vec2 pos;
    float animationTimer; //???
    float torchTimer; //ENT_TORCH
    float fireTimer; //ENT_FIRE
    bool lit; //ENT_FIRE
    int woodLeft; //ENT_TREE
};

struct Player {
    Vec2 pos, vel, knockbackVel;
    Entity held;
    float animationTimer;

    bool flip;

    float sfx_step_base_timer;
    float sfx_step_grass_layer1_timer;
    float sfx_step_grass_layer2_timer;

    // You enter sprint mode for a minimum of 0.5 seconds so you can't
    // cheese the game by sprinting forward and picking up your item and
    // dropping it again on the ground
    float sprintRetriggerTimer;
};



static const int ATTACK_COOLDOWN = 2;
struct Enemy {
    Vec2 pos, vel;
    float animationTimer;
    float attackCooldown;

    bool is_aggroed;
    bool has_ever_aggroed;
    float chatterTimer;
};

struct Layer {
    int width, height;
    int * tiles;
    int * operator[] (int y) { return &tiles[y * width]; }
};

struct Decal {
    Coord2 pos;
    int imgIdx;
    int layerIdx; //this decal will be drawn immediately behind `layers[layerIdx]`
};

struct Attack {
    Vec2 pos;
    Vec2 dir;
    float timer;
};

struct Level {
    List<Layer> layers;
    List<Decal> decals;
    List<Image> decalImages;
    List<Entity> entities;
    List<Enemy> enemies;
    Player player;

    List<Attack> attacks;
};

static void nan_check(Level & level) {
#if 0
    for (Entity & e : level.entities) {
        if (isnan(e.pos.x) || isnan(e.pos.y)) {
            __builtin_trap();
        }
    }
    for (Enemy & e : level.enemies) {
        if (isnan(e.pos.x) || isnan(e.pos.y)) {
            __builtin_trap();
        }
    }
    if (isnan(level.player.pos.x) || isnan(level.player.pos.y) || isnan(level.player.vel.x) || isnan(level.player.vel.y)
        || isnan(level.player.knockbackVel.x) || isnan(level.player.knockbackVel.y))
    {
        __builtin_trap();
    }
    if (isnan(level.player.held.pos.x) || isnan(level.player.held.pos.y)) {
        __builtin_trap();
    }
#endif
}

static Level load_level(const char * levelPath, const char * projectPath) {
    //extract info from project file
    JSON_Value * projectRoot = json_parse_file(projectPath);
    JSON_Array * entityDefs = json_object_get_array(json_object(projectRoot), "entities");
    List<Pair<long long, char *>> entityTypes = {};
    for (int i = 0; i < json_array_get_count(entityDefs); ++i) {
        JSON_Object * def = json_array_get_object(entityDefs, i);
        entityTypes.add({
            atoll(json_object_get_string(def, "exportID")),
            dup(json_object_get_string(def, "name"))
        });
    }
    json_value_free(projectRoot);

    List<char *> decalImageNames = {};

    //extract info from level file
    Level level = {};
    JSON_Value * levelRoot = json_parse_file(levelPath);
    JSON_Array * layers = json_object_get_array(json_object(levelRoot), "layers");
    for (int i = json_array_get_count(layers) - 1; i >= 0; --i) {
        JSON_Object * layer = json_array_get_object(layers, i);
        JSON_Array * tiles = json_object_get_array(layer, "data");
        JSON_Array * decals = json_object_get_array(layer, "decals");
        JSON_Array * entities = json_object_get_array(layer, "entities");
        if (tiles) {
            int width = json_object_get_number(layer, "gridCellsX");
            int height = json_object_get_number(layer, "gridCellsY");
            Layer map = { width, height, (int *) malloc(width * height * sizeof(int)) };
            for (int i = 0; i < json_array_get_count(tiles); ++i) {
                map.tiles[i] = json_array_get_number(tiles, i);
            }
            level.layers.add(map);
        } else if (decals) {
            for (int i = 0; i < json_array_get_count(decals); ++i) {
                JSON_Object * decal = json_array_get_object(decals, i);

                //build path string to image file
                const char * lastSlash = strrchr(projectPath, '/');
                char * imagePath = dsprintf(nullptr, "%.*s/%s/%s", (int) (lastSlash - projectPath), projectPath,
                    json_object_get_string(layer, "folder"), json_object_get_string(decal, "texture"));

                //find index of image if it's already been loaded
                int idx = -1;
                for (int i = 0; i < decalImageNames.len; ++i) {
                    if (!strcmp(decalImageNames[i], imagePath)) {
                        idx = i;
                        break;
                    }
                }

                //load image if not loaded already
                if (idx < 0) {
                    idx = decalImageNames.len;
                    decalImageNames.add(dup(imagePath));
                    level.decalImages.add(load_image(imagePath));
                    printf("loading %s\n", imagePath);
                }
                free(imagePath);

                //actually add the decal
                level.decals.add({
                    coord2(json_object_get_number(decal, "x"), json_object_get_number(decal, "y")),
                    idx, (int) level.layers.len
                });
            }
        } else if (entities) {
            JSON_Array * entities = json_object_get_array(layer, "entities");
            for (int i = 0; i < json_array_get_count(entities); ++i) {
                JSON_Object * entity = json_array_get_object(entities, i);
                JSON_Object * properties = json_object_get_object(entity, "values");

                long long id = atoll(json_object_get_string(entity, "_eid"));
                const char * entityType = nullptr;
                for (Pair<long long, char *> & pair : entityTypes) {
                    if (pair.first == id) {
                        entityType = pair.second;
                        break;
                    }
                }
                assert(entityType);

                Vec2 pos = vec2(json_object_get_number(entity, "x"),
                                json_object_get_number(entity, "y") - 3 * TILE_HEIGHT);
                if (!strcmp(entityType, "player")) {
                    level.player.pos = pos;
                } else if (!strcmp(entityType, "wood")) {
                    level.entities.add({ ENT_WOOD, pos });
                } else if (!strcmp(entityType, "torch")) {
                    level.entities.add({ ENT_TORCH, pos });
                } else if (!strcmp(entityType, "axe")) {
                    level.entities.add({ ENT_AXE, pos });
                } else if (!strcmp(entityType, "battery")) {
                    level.entities.add({ ENT_BATTERY, pos });
                } else if (!strcmp(entityType, "campfire")) {
                    level.entities.add({ ENT_FIRE, pos, .fireTimer = FIRE_LOG_TIME, .lit = true });
                } else if (!strcmp(entityType, "tree")) {
                    level.entities.add({ ENT_TREE, pos, .woodLeft = TREE_HITS });
                } else if (!strcmp(entityType, "fire_source")) {
                    level.entities.add({ ENT_FIRESRC, pos });
                } else if (!strcmp(entityType, "boat")) {
                    level.entities.add({ ENT_BOAT, pos });
                } else if (!strcmp(entityType, "enemy")) {
                    level.enemies.add({ pos });
                } else {
                    printf("UNKNOWN ENTITY TYPE: %s\n", entityType);
                }
            }
        } else {
            printf("UNSUPPORTED LAYER TYPE! (probably a grid layer)\n");
        }
    }
    json_value_free(levelRoot);
    entityTypes.finalize();
    return level;
}

static void finalize(Level & level) {
    for (Layer & layer : level.layers) {
        free(layer.tiles);
    }
    level.layers.finalize();
    level.decals.finalize();
    for (Image & image : level.decalImages) {
        free(image.pixels);
    }
    level.decalImages.finalize();
    level = {};
}

//TODO: allow priveleging certain entity types over others
static inline int get_nearest_entity(Level & level, Vec2 pos, float maxDist) {
    int nearest = -1;
    float dist = maxDist;
    for (int i = 0; i < level.entities.len; ++i) {
        float d = len(pos - level.entities[i].pos);
        if (d < dist) {
            nearest = i;
            dist = d;
        }
    }
    return nearest;
}

bool filter_none(EntityType type) { return true; }
bool filter_pickups(EntityType type) { return holdable[type]; }

// yuck, templates. sorry -phil
template <typename Lambda>
static inline int get_nearest_entity(Level & level, Vec2 pos, float maxDist, Lambda filter = filter_none) {
    int nearest = -1;
    float dist = maxDist;
    for (int i = 0; i < level.entities.len; ++i) {
        float d = len(pos - level.entities[i].pos);
        if (level.entities[i].type == ENT_BOAT) d *= 0.5f;
        if (level.entities[i].type == ENT_FIRESRC) d *= 0.5f;
        if (d < dist && filter(level.entities[i].type)) {
            nearest = i;
            dist = d;
        }
    }
    return nearest;
}

static inline Tileset getTorchSprite(Graphics & graphics, float timer) {
    return graphics.torchSprites[imax(0, imin(3, timer / TORCH_LIFETIME * 3 + 0.99f))];
}


#undef swap
#include <algorithm>
#define swap myswap

static void draw_level(Level & level, Canvas & canvas, Canvas & lights, Graphics & graphics, int offx, int offy) {
    struct DrawCommand {
        Pixel * pixels;
        int w, h, p, x, y;
        int offy;
        int flip;
    };
    List<List<DrawCommand>> rows = {};
    for (int i = 0; i < level.layers[0].height; ++i) { rows.add({}); }
    auto queue_sprite = [&] (Image image, Vec2 pos, int offy = 0) {
        DrawCommand cmd = { image.pixels, image.width, image.height, image.width, (int) pos.x, (int) pos.y, offy };
        rows[imax(0, imin(rows.len - 1, cmd.y / TILE_HEIGHT))].add(cmd);
    };
    auto queue_sprite_anim = [&] (Tileset set, Vec2 pos, float anim_timer, int offy = 0, int flip = 0) {
        int index = anim_timer / set.frame_time;
        int tx = index % set.width;
        int ty = (index / set.width) % set.height;
        DrawCommand cmd = { &set.image[ty * set.tileHeight][tx * set.tileWidth],
            set.tileWidth, set.tileHeight, set.image.width, (int) pos.x, (int) pos.y, offy, flip };
        rows[imax(0, imin(rows.len - 1, cmd.y / TILE_HEIGHT))].add(cmd);
    };
    auto queue_tile = [&] (Tileset set, int tx, Vec2 pos, int offy = 0) {
        DrawCommand cmd = { &set.image[0][tx * set.tileWidth],
            set.tileWidth, set.tileHeight, set.image.width, (int) pos.x, (int) pos.y, offy };
        rows[imax(0, imin(rows.len - 1, cmd.y / TILE_HEIGHT))].add(cmd);
    };

    for (Enemy & e : level.enemies) {
        queue_sprite_anim(graphics.enemy, e.pos, e.animationTimer);
    }
    for (Entity & e : level.entities) {
        if (e.type == ENT_TORCH) {
            queue_sprite_anim(getTorchSprite(graphics, e.torchTimer), e.pos, e.animationTimer);
        } else if (e.type == ENT_FIRE) {
            queue_tile(graphics.logSprites, ceilf(e.fireTimer / FIRE_LOG_TIME), e.pos);
            if (e.lit) queue_sprite_anim(graphics.fireSprites, e.pos + vec2(0, 0.01f), e.animationTimer, 4);
        } else if (e.type == ENT_FIRESRC) {
            queue_sprite(graphics.plane, e.pos, 0);
            queue_sprite_anim(graphics.planeFire, e.pos - vec2(0, 2), e.animationTimer, -2);
            queue_sprite(graphics.planeBehindFire, e.pos - vec2(4, 4), -4);
        } else {
            queue_sprite(graphics.itemSprites[e.type], e.pos);
        }
    }

    if (level.player.held.type == ENT_WOOD) {
        queue_sprite_anim(graphics.player_walk_log, level.player.pos, level.player.animationTimer, 0, level.player.flip);
    } else if (level.player.held.type == ENT_AXE) {
        queue_sprite_anim(graphics.player_walk_axe, level.player.pos, level.player.animationTimer, 0, level.player.flip);
    } else if (level.player.held.type == ENT_TORCH) {
        queue_sprite_anim(graphics.player_walk, level.player.pos, level.player.animationTimer, 0, level.player.flip);
        queue_sprite_anim(getTorchSprite(graphics, level.player.held.torchTimer),
            level.player.held.pos, level.player.held.animationTimer, 16);
    } else {
        queue_sprite_anim(graphics.player_walk, level.player.pos, level.player.animationTimer, 0, level.player.flip);
        queue_sprite(graphics.itemSprites[level.player.held.type], level.player.held.pos, 16);
    }

    for (List<DrawCommand> & row : rows) {
        std::sort(row.begin(), row.end(), [] (DrawCommand l, DrawCommand r) { return l.y < r.y; });
    }

    for (int i = 0; i < level.layers.len; ++i) {
        Layer & layer = level.layers[i];
        //draw decals that are directly behind this layer
        for (Decal & decal : level.decals) {
            if (decal.layerIdx == i) {
                Image & sprite = level.decalImages[decal.imgIdx];
                draw_sprite_a1(canvas, sprite,
                    decal.pos.x - sprite.width / 2 - offx, decal.pos.y - sprite.height / 2 - offy);
            }
        }

        int minx = imax(0, offx / TILE_WIDTH);
        int miny = imax(0, offy / TILE_HEIGHT);
        int maxx = imin(layer.width, (offx + canvas.width + TILE_WIDTH - 1) / TILE_WIDTH);
        int maxy = imin(layer.height, (offy + canvas.height + TILE_HEIGHT - 1) / TILE_HEIGHT + 6);
        for (int y = miny; y < maxy; ++y) {
            for (int x = minx; x < maxx; ++x) {
                int tileIdx = layer[y][x];
                if (tileIdx >= 0 && graphics.isGround[tileIdx]) {
                    int tx = tileIdx % graphics.tileset.width;
                    int ty = tileIdx / graphics.tileset.width;
                    draw_tile_a1(canvas, graphics.tileset, tx, ty, x * TILE_WIDTH - offx, (y - 3) * TILE_HEIGHT - offy);
                }
            }

            //draw entities that are directly behind this row
            for (DrawCommand & cmd : rows[y]) {
                if (cmd.flip) {
                    _draw_sprite_flip(canvas, cmd.pixels, cmd.w, cmd.h, cmd.p,
                        cmd.x - cmd.w / 2 - offx, cmd.y - cmd.h - offy - cmd.offy, true);
                } else
                _draw_sprite_a1(canvas, cmd.pixels, cmd.w, cmd.h, cmd.p,
                    cmd.x - cmd.w / 2 - offx, cmd.y - cmd.h - offy - cmd.offy);
            }

            //@HACK: AHHHHHHHHH
            for (Attack & attack : level.attacks) {
                int yy = attack.pos.y / TILE_HEIGHT;
                if (yy == y) {
                    draw_transformed_sprite(canvas, graphics.attack[imin(3, attack.timer * 6)],
                        attack.pos - vec2(offx, offy + 16) + attack.dir * 16,
                        atan2f(attack.dir.x, attack.dir.y), vec2(1));
                }
            }

            for (int x = minx; x < maxx; ++x) {
                int tileIdx = layer[y][x];
                if (tileIdx >= 0 && !graphics.isGround[tileIdx]) {
                    int tx = tileIdx % graphics.tileset.width;
                    int ty = tileIdx / graphics.tileset.width;
                    draw_tile_a1(canvas, graphics.tileset, tx, ty, x * TILE_WIDTH - offx, (y - 3) * TILE_HEIGHT - offy);
                }
            }
        }
    }

    auto draw_torch_light = [&] (Vec2 pos, float timer) {
        float jitter = sin(timer * 5) * 0.05;
        float radius = (TORCH_RADIUS * sqrtf(timer / TORCH_LIFETIME)) * (1 + jitter);
        add_light(lights, pos.x - offx, pos.y - offy, radius, radius, { 255, 70, 20, 160 });
        // add_light(lights, pos.x - offx, pos.y - offy, radius*0.6, radius*0.6, { 255, 64, 0, 64 });
    };
    auto draw_fire_light = [&] (Vec2 pos, float timer) {
        float jitter = sin((get_time() - 0.01) * 5) * 0.05;
        float radius = (FIRE_RADIUS) * (1 + jitter) * sqrtf(timer / FIRE_TOTAL_TIME);
        float alpha = 192 + 32 * (timer / FIRE_TOTAL_TIME);
        add_light(lights, pos.x - offx, pos.y - offy, radius, radius, { 255, 90, 20, (u8) alpha });
        // add_light(lights, pos.x - offx, pos.y - offy, radius*0.6, radius*0.6, { 255, 100, 50, 96 });
    };
    for (Entity & e : level.entities) {
        if (e.type == ENT_TORCH) draw_torch_light(e.pos, e.torchTimer);
        else if (e.type == ENT_FIRE && e.lit) draw_fire_light(e.pos, e.fireTimer);
        else if (e.type == ENT_FIRESRC) draw_fire_light(e.pos, FIRE_TOTAL_TIME);
    }
    if (level.player.held.type == ENT_TORCH) {
        draw_torch_light(level.player.held.pos + Vec2{0, -16}, level.player.held.torchTimer);
    }
}

#endif //LEVEL_HPP
