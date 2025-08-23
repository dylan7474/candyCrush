#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Game constants */
#define GRID_SIZE 8
#define CANDY_TYPES 6
#define TILE_SIZE 64
#define WINDOW_WIDTH (GRID_SIZE * TILE_SIZE)
#define WINDOW_HEIGHT (GRID_SIZE * TILE_SIZE + 80)

/* Embedded asset placeholders generated via xxd -i */
/* Silent WAV used for sound placeholders */
static unsigned char sound_wav[] = {
    0x52,0x49,0x46,0x46,0x26,0x00,0x00,0x00,0x57,0x41,0x56,0x45,0x66,0x6D,0x74,0x20,
    0x10,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x40,0x1F,0x00,0x00,0x80,0x3E,0x00,0x00,
    0x02,0x00,0x10,0x00,0x64,0x61,0x74,0x61,0x02,0x00,0x00,0x00,0x00,0x00
};
static unsigned int sound_wav_len = sizeof(sound_wav);

/* Loading helpers */

static Mix_Chunk* loadSoundFromMemory(const unsigned char* data, unsigned int len) {
    SDL_RWops* rw = SDL_RWFromConstMem(data, len);
    if (!rw) return NULL;
    Mix_Chunk* c = Mix_LoadWAV_RW(rw, 1);
    return c;
}

static Mix_Music* loadMusicFromMemory(const unsigned char* data, unsigned int len) {
    SDL_RWops* rw = SDL_RWFromConstMem(data, len);
    if (!rw) return NULL;
    Mix_Music* m = Mix_LoadMUS_RW(rw, 1);
    return m;
}


static SDL_Texture* createCandyTexture(SDL_Renderer* renderer) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, TILE_SIZE, TILE_SIZE, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return NULL;
    SDL_LockSurface(surf);
    Uint32* pixels = (Uint32*)surf->pixels;
    int pitch = surf->pitch / 4;
    int cx = TILE_SIZE / 2;
    int cy = TILE_SIZE / 2;
    int radius = TILE_SIZE / 2 - 2;
    int radiusSq = radius * radius;
    int highlightCx = cx - radius / 3;
    int highlightCy = cy - radius / 3;
    int highlightRadius = radius / 3;
    int highlightRadiusSq = highlightRadius * highlightRadius;
    for (int y = 0; y < TILE_SIZE; ++y) {
        for (int x = 0; x < TILE_SIZE; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            int distSq = dx*dx + dy*dy;
            if (distSq <= radiusSq) {
                float t = (float)distSq / (float)radiusSq;
                Uint8 intensity = (Uint8)(200 + 55 * (1.f - t));
                int hdx = x - highlightCx;
                int hdy = y - highlightCy;
                if (hdx*hdx + hdy*hdy <= highlightRadiusSq) {
                    intensity = 255;
                }
                pixels[y * pitch + x] = SDL_MapRGBA(surf->format, intensity, intensity, intensity, 255);
            } else {
                pixels[y * pitch + x] = SDL_MapRGBA(surf->format, 0, 0, 0, 0);
            }
        }
    }
    SDL_UnlockSurface(surf);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surf);
    return tex;
}

/* Colors for candies */
static SDL_Color candyColors[CANDY_TYPES] = {
    {255, 0, 0, 255},
    {0, 255, 0, 255},
    {0, 0, 255, 255},
    {255, 255, 0, 255},
    {255, 0, 255, 255},
    {0, 255, 255, 255}
};

/* Game state */
typedef enum { STATE_IDLE, STATE_SWAP, STATE_REMOVE, STATE_FALL, STATE_GAMEOVER } GameState;
static GameState gameState = STATE_IDLE;

static int board[GRID_SIZE][GRID_SIZE];
static int toRemove[GRID_SIZE][GRID_SIZE];
static float fallOffset[GRID_SIZE][GRID_SIZE];

static int swapX1, swapY1, swapX2, swapY2;
static float swapProgress = 0.f;
static int swapBack = 0;

static float removeTimer = 0.f;
static int removeCount = 0;

static int score = 0;
static TTF_Font* font = NULL;

static SDL_Texture* candyTexture = NULL;
static Mix_Chunk* sndSwap = NULL;
static Mix_Chunk* sndInvalid = NULL;
static Mix_Chunk* sndLand = NULL;
static Mix_Music* music = NULL;

static void swapCandies(int x1, int y1, int x2, int y2) {
    int tmp = board[y1][x1];
    board[y1][x1] = board[y2][x2];
    board[y2][x2] = tmp;
}

static int hasMove(void);

static void initBoard(void) {
    srand((unsigned int)time(NULL));
    do {
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int x = 0; x < GRID_SIZE; ++x) {
                int c;
                do {
                    c = rand() % CANDY_TYPES;
                    board[y][x] = c;
                } while ((x >= 2 && board[y][x-1] == c && board[y][x-2] == c) ||
                         (y >= 2 && board[y-1][x] == c && board[y-2][x] == c));
                fallOffset[y][x] = 0.f;
            }
        }
    } while (!hasMove());
}

static int findMatches(void) {
    memset(toRemove, 0, sizeof(toRemove));
    int count = 0;
    for (int y = 0; y < GRID_SIZE; ++y) {
        int run = 1;
        for (int x = 1; x < GRID_SIZE; ++x) {
            if (board[y][x] == board[y][x-1] && board[y][x] != -1) run++;
            else {
                if (run >= 3) {
                    for (int k = 0; k < run; ++k) {
                        toRemove[y][x-1-k] = 1;
                        count++;
                    }
                }
                run = 1;
            }
        }
        if (run >= 3) {
            for (int k = 0; k < run; ++k) {
                toRemove[y][GRID_SIZE-1-k] = 1;
                count++;
            }
        }
    }
    for (int x = 0; x < GRID_SIZE; ++x) {
        int run = 1;
        for (int y = 1; y < GRID_SIZE; ++y) {
            if (board[y][x] == board[y-1][x] && board[y][x] != -1) run++;
            else {
                if (run >= 3) {
                    for (int k = 0; k < run; ++k) {
                        if (!toRemove[y-1-k][x]) count++;
                        toRemove[y-1-k][x] = 1;
                    }
                }
                run = 1;
            }
        }
        if (run >= 3) {
            for (int k = 0; k < run; ++k) {
                if (!toRemove[GRID_SIZE-1-k][x]) count++;
                toRemove[GRID_SIZE-1-k][x] = 1;
            }
        }
    }
    return count;
}

static int hasMove(void) {
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            if (x < GRID_SIZE - 1) {
                swapCandies(x, y, x + 1, y);
                int m = findMatches();
                swapCandies(x, y, x + 1, y);
                if (m > 0) return 1;
            }
            if (y < GRID_SIZE - 1) {
                swapCandies(x, y, x, y + 1);
                int m = findMatches();
                swapCandies(x, y, x, y + 1);
                if (m > 0) return 1;
            }
        }
    }
    return 0;
}

static void startRemove(void) {
    removeTimer = 0.f;
    gameState = STATE_REMOVE;
}

static void applyRemove(void) {
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            if (toRemove[y][x]) {
                board[y][x] = -1;
                fallOffset[y][x] = 0.f;
            }
        }
    }
    score += removeCount * 10;
    removeCount = 0;
}

static void startFall(void) {
    for (int x = 0; x < GRID_SIZE; ++x) {
        int write = GRID_SIZE - 1;
        for (int y = GRID_SIZE - 1; y >= 0; --y) {
            if (board[y][x] != -1) {
                board[write][x] = board[y][x];
                if (write != y)
                    fallOffset[write][x] = (float)(write - y) * TILE_SIZE;
                else
                    fallOffset[write][x] = 0.f;
                write--;
            }
        }
        int newCount = write + 1;
        while (write >= 0) {
            board[write][x] = rand() % CANDY_TYPES;
            fallOffset[write][x] = (float)newCount * TILE_SIZE;
            write--;
        }
    }
    gameState = STATE_FALL;
}

static int fallStep(float dt) {
    int moving = 0;
    float speed = 400.f;
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            if (fallOffset[y][x] > 0.f) {
                fallOffset[y][x] -= speed * dt;
                if (fallOffset[y][x] <= 0.f) fallOffset[y][x] = 0.f;
                else moving = 1;
            }
        }
    }
    return moving;
}

static void renderScore(SDL_Renderer* renderer) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Score: %d", score);
    if (font) {
        SDL_Color white = {255,255,255,255};
        SDL_Surface* surf = TTF_RenderText_Blended(font, buf, white);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {10, GRID_SIZE * TILE_SIZE + 10, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
            return;
        }
    }
    SDL_Rect r = {10, GRID_SIZE * TILE_SIZE + 20, 8, 20};
    SDL_SetRenderDrawColor(renderer, 255,255,255,255);
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] < '0' || buf[i] > '9') continue;
        r.x = 10 + i * 12;
        SDL_RenderFillRect(renderer, &r);
    }
}

static void renderBoard(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            if (board[y][x] < 0) continue;
            SDL_Rect dst = {x * TILE_SIZE, y * TILE_SIZE - (int)fallOffset[y][x], TILE_SIZE, TILE_SIZE};
            float alpha = 1.f;
            if (gameState == STATE_REMOVE && toRemove[y][x]) {
                alpha = 1.f - removeTimer;
                if (alpha < 0.f) alpha = 0.f;
            }
            if (gameState == STATE_SWAP) {
                if (x == swapX1 && y == swapY1) {
                    dst.x = (int)((swapX1 + (swapX2 - swapX1) * swapProgress) * TILE_SIZE);
                    dst.y = (int)((swapY1 + (swapY2 - swapY1) * swapProgress) * TILE_SIZE);
                } else if (x == swapX2 && y == swapY2) {
                    dst.x = (int)((swapX2 + (swapX1 - swapX2) * swapProgress) * TILE_SIZE);
                    dst.y = (int)((swapY2 + (swapY1 - swapY2) * swapProgress) * TILE_SIZE);
                }
            }
            SDL_SetTextureColorMod(candyTexture,
                                   candyColors[board[y][x]].r,
                                   candyColors[board[y][x]].g,
                                   candyColors[board[y][x]].b);
            SDL_SetTextureAlphaMod(candyTexture, (Uint8)(alpha * 255));
            SDL_RenderCopy(renderer, candyTexture, NULL, &dst);
        }
    }
    renderScore(renderer);
    if (gameState == STATE_GAMEOVER) {
        const char* msg = "No moves! Press R to restart";
        if (font) {
            SDL_Color white = {255,255,255,255};
            SDL_Surface* surf = TTF_RenderText_Blended(font, msg, white);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect dst = { (WINDOW_WIDTH - surf->w) / 2,
                                 (WINDOW_HEIGHT - 80 - surf->h) / 2,
                                 surf->w, surf->h };
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }
    }
    SDL_RenderPresent(renderer);
}

static int selectedX = -1, selectedY = -1;

static void handleInput(SDL_Event* e) {
    if (gameState == STATE_GAMEOVER && e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_r) {
        score = 0;
        selectedX = selectedY = -1;
        initBoard();
        gameState = STATE_IDLE;
        return;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && gameState == STATE_IDLE) {
        int x = e->button.x / TILE_SIZE;
        int y = e->button.y / TILE_SIZE;
        if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE) {
            if (selectedX == -1) {
                selectedX = x;
                selectedY = y;
            } else {
                if ((abs(x - selectedX) + abs(y - selectedY)) == 1) {
                    swapX1 = selectedX;
                    swapY1 = selectedY;
                    swapX2 = x;
                    swapY2 = y;
                    swapProgress = 0.f;
                    swapBack = 0;
                    selectedX = selectedY = -1;
                    gameState = STATE_SWAP;
                    if (sndSwap) Mix_PlayChannel(-1, sndSwap, 0);
                } else {
                    selectedX = x;
                    selectedY = y;
                }
            }
        }
    }
}

static void updateGame(float dt) {
    switch (gameState) {
    case STATE_SWAP:
        swapProgress += dt * 5.f;
        if (swapProgress >= 1.f) {
            swapProgress = 1.f;
            swapCandies(swapX1, swapY1, swapX2, swapY2);
            if (swapBack) {
                gameState = STATE_IDLE;
                swapBack = 0;
            } else {
                removeCount = findMatches();
                if (removeCount > 0) {
                    startRemove();
                } else {
                    swapBack = 1;
                    swapProgress = 0.f;
                    if (sndInvalid) Mix_PlayChannel(-1, sndInvalid, 0);
                }
            }
        }
        break;
    case STATE_REMOVE:
        removeTimer += dt * 3.f;
        if (removeTimer >= 1.f) {
            applyRemove();
            startFall();
        }
        break;
    case STATE_FALL:
        if (!fallStep(dt)) {
            if (sndLand) Mix_PlayChannel(-1, sndLand, 0);
            removeCount = findMatches();
            if (removeCount > 0) startRemove();
            else if (!hasMove()) gameState = STATE_GAMEOVER;
            else gameState = STATE_IDLE;
        }
        break;
    case STATE_IDLE:
        if (!hasMove()) gameState = STATE_GAMEOVER;
        break;
    case STATE_GAMEOVER:
        break;
    default:
        break;
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
    }
    if (TTF_Init() == -1) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
    }

    SDL_Window* window = SDL_CreateWindow("Candy Crush Clone",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        return 1;
    }

    candyTexture = createCandyTexture(renderer);
    sndSwap = loadSoundFromMemory(sound_wav, sound_wav_len);
    sndInvalid = loadSoundFromMemory(sound_wav, sound_wav_len);
    sndLand = loadSoundFromMemory(sound_wav, sound_wav_len);
    music = loadMusicFromMemory(sound_wav, sound_wav_len);
    if (music) Mix_PlayMusic(music, -1);
    font = TTF_OpenFont("DejaVuSans.ttf", 24);
    if (!font) {
        fprintf(stderr, "Failed to load font: %s\n", TTF_GetError());
    }

    initBoard();

    int running = 1;
    Uint32 last = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else handleInput(&e);
        }
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        updateGame(dt);
        renderBoard(renderer);
        SDL_Delay(16);
    }

    if (music) Mix_FreeMusic(music);
    if (sndSwap) Mix_FreeChunk(sndSwap);
    if (sndInvalid) Mix_FreeChunk(sndInvalid);
    if (sndLand) Mix_FreeChunk(sndLand);
    if (candyTexture) SDL_DestroyTexture(candyTexture);
    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    Mix_CloseAudio();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
