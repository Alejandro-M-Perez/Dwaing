#include <stdbool.h>
#include <stdio.h>

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include "parser.h"

#define SAMPLE_BOX_COUNT 4

typedef struct {
    double x;
    double y;
    double w;
    double h;
} Box;

static Panel g_panel;
static bool g_have_panel = false;
static int g_win_w = 1200;
static int g_win_h = 800;
static double g_zoom = 1.0;
static double g_pan_x = 0.0;
static double g_pan_y = 0.0;

static const Box g_sample_boxes[SAMPLE_BOX_COUNT] = {
    {50.0, 520.0, 60.0, 40.0},
    {130.0, 500.0, 120.0, 60.0},
    {280.0, 480.0, 90.0, 120.0},
    {390.0, 530.0, 180.0, 30.0}
};

static void draw_rect(double x, double y, double w, double h) {
    glBegin(GL_LINE_LOOP);
    glVertex2d(x, y);
    glVertex2d(x + w, y);
    glVertex2d(x + w, y + h);
    glVertex2d(x, y + h);
    glEnd();
}

static void draw_grid(void) {
    glColor3f(0.90f, 0.90f, 0.90f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int x = -200; x <= 1400; x += 50) {
        glVertex2i(x, -200);
        glVertex2i(x, 1200);
    }
    for (int y = -200; y <= 1200; y += 50) {
        glVertex2i(-200, y);
        glVertex2i(1400, y);
    }
    glEnd();
}

static void draw_sample_boxes(void) {
    glColor3f(0.18f, 0.42f, 0.83f);
    glLineWidth(2.0f);

    for (int i = 0; i < SAMPLE_BOX_COUNT; ++i) {
        const Box *b = &g_sample_boxes[i];
        draw_rect(b->x, b->y, b->w, b->h);
    }
}

static void draw_regions(void) {
    glColor3f(0.10f, 0.55f, 0.25f);
    glLineWidth(2.0f);
    for (int i = 0; i < g_panel.region_count; ++i) {
        const Region *r = &g_panel.regions[i];
        draw_rect(r->min.x, r->min.y, r->max.x - r->min.x, r->max.y - r->min.y);
    }
}

static void draw_rails(void) {
    glColor3f(0.60f, 0.40f, 0.08f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < g_panel.rail_count; ++i) {
        const Rail *r = &g_panel.rails[i];
        glVertex2d(r->start.x, r->start.y);
        glVertex2d(r->end.x, r->end.y);
    }
    glEnd();
}

static void draw_devices(void) {
    glLineWidth(2.0f);
    for (int i = 0; i < g_panel.device_count; ++i) {
        const Device *d = &g_panel.devices[i];
        const double w = 32.0;
        const double h = 18.0;
        const double x = d->position.x - (w / 2.0);
        const double y = d->position.y - (h / 2.0);

        if (d->domain == DOMAIN_AC) {
            glColor3f(0.75f, 0.20f, 0.18f);
        } else if (d->domain == DOMAIN_DC) {
            glColor3f(0.10f, 0.35f, 0.80f);
        } else {
            glColor3f(0.45f, 0.45f, 0.45f);
        }

        draw_rect(x, y, w, h);
    }
}

static void set_ortho(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void render_frame(void) {
    glClearColor(0.98f, 0.98f, 0.98f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glLoadIdentity();
    glTranslated(g_pan_x, g_pan_y, 0.0);
    glScaled(g_zoom, g_zoom, 1.0);

    draw_grid();
    if (g_have_panel) {
        draw_regions();
        draw_rails();
        draw_devices();
    }
    draw_sample_boxes();
}

static void maybe_load_panel(const char *panel_path) {
    char err[256];

    if (!panel_path) {
        model_init_panel(&g_panel);
        g_have_panel = false;
        return;
    }

    if (!parse_panel(panel_path, &g_panel, err, (int)sizeof(err))) {
        fprintf(stderr, "warning: failed to load panel '%s': %s\n", panel_path, err);
        model_init_panel(&g_panel);
        g_have_panel = false;
        return;
    }

    g_have_panel = true;
}

int main(int argc, char **argv) {
    bool running = true;
    SDL_Window *window;
    SDL_GLContext gl_ctx;
    const char *panel_path = 0;

    if (argc >= 2) {
        panel_path = argv[1];
    }
    maybe_load_panel(panel_path);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window = SDL_CreateWindow(
        "Dwaing Viewer (OpenGL MVP)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        g_win_w,
        g_win_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);
    set_ortho(g_win_w, g_win_h);

    printf("Viewer controls: +/- zoom, arrows pan, mouse wheel zoom, r reset, q/ESC quit\n");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                g_win_w = event.window.data1;
                g_win_h = event.window.data2;
                set_ortho(g_win_w, g_win_h);
            } else if (event.type == SDL_MOUSEWHEEL) {
                if (event.wheel.y > 0) {
                    g_zoom *= 1.08;
                } else if (event.wheel.y < 0) {
                    g_zoom /= 1.08;
                    if (g_zoom < 0.1) {
                        g_zoom = 0.1;
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_PLUS:
                    case SDLK_EQUALS:
                    case SDLK_KP_PLUS:
                        g_zoom *= 1.10;
                        break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        g_zoom /= 1.10;
                        if (g_zoom < 0.1) {
                            g_zoom = 0.1;
                        }
                        break;
                    case SDLK_LEFT:
                        g_pan_x += 20.0;
                        break;
                    case SDLK_RIGHT:
                        g_pan_x -= 20.0;
                        break;
                    case SDLK_UP:
                        g_pan_y += 20.0;
                        break;
                    case SDLK_DOWN:
                        g_pan_y -= 20.0;
                        break;
                    case SDLK_r:
                        g_zoom = 1.0;
                        g_pan_x = 0.0;
                        g_pan_y = 0.0;
                        break;
                    case SDLK_q:
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                    default:
                        break;
                }
            }
        }

        render_frame();
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
