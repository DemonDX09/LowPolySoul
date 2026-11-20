/* LOW POLY SOUL - tiny 3D soulslike prototype for PSP.  By Demon_DX_
 * Built with PSPDEV (pspsdk). No textures, no assets: everything is colored boxes.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psprtc.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

PSP_MODULE_INFO("LowPolySoul", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define BUF_WIDTH 512
#define SCR_W 480
#define SCR_H 272
#define PI 3.14159265f

#define RGB(r,g,b)    (0xff000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define RGBA(r,g,b,a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define FOGC RGB(22, 20, 34)

#define PL_MAX_HP 100.0f
#define PL_MAX_ST 100.0f
#define BOSS_HP   400.0f
#define ARENA     16.5f

/* ---------------------------------------------------------------- exit cb */
static int exit_callback(int a1, int a2, void *c) { sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

/* ---------------------------------------------------------------- vram */
static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int staticOffset = 0;

static unsigned int vramSize(unsigned int w, unsigned int h, unsigned int psm)
{
    switch (psm) {
    case GU_PSM_5650: case GU_PSM_5551: case GU_PSM_4444: return 2 * w * h;
    case GU_PSM_8888: return 4 * w * h;
    default: return 0;
    }
}
static void *vramAlloc(unsigned int w, unsigned int h, unsigned int psm)
{
    void *r = (void *)staticOffset;
    staticOffset += vramSize(w, h, psm);
    return r;
}

/* ---------------------------------------------------------------- math */
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float wrapAng(float d) { while (d > PI) d -= 2 * PI; while (d < -PI) d += 2 * PI; return d; }
static float turnToward(float cur, float tgt, float step)
{
    return cur + clampf(wrapAng(tgt - cur), -step, step);
}

static unsigned int shadeColor(unsigned int c, float f)
{
    int r = (int)((c & 0xff) * f), g = (int)(((c >> 8) & 0xff) * f), b = (int)(((c >> 16) & 0xff) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xff000000u | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}
static unsigned int mixColor(unsigned int a, unsigned int b, float t)
{
    int ar = a & 0xff, ag = (a >> 8) & 0xff, ab = (a >> 16) & 0xff;
    int br = b & 0xff, bg = (b >> 8) & 0xff, bb = (b >> 16) & 0xff;
    return RGB((int)lerpf((float)ar, (float)br, t), (int)lerpf((float)ag, (float)bg, t),
               (int)lerpf((float)ab, (float)bb, t));
}

/* ---------------------------------------------------------------- drawing */
typedef struct { unsigned int color; float x, y, z; } Vtx;
typedef struct { unsigned int color; short x, y, z; } Vtx2D;

static const float cv[8][3] = {
    {-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
    {-.5f,-.5f, .5f},{.5f,-.5f, .5f},{.5f,.5f, .5f},{-.5f,.5f, .5f}
};
static const int cf[6][4] = { {4,5,6,7},{1,0,3,2},{5,1,2,6},{0,4,7,3},{7,6,2,3},{0,1,5,4} };
static const float faceShade[6] = { 0.75f, 0.55f, 0.85f, 0.60f, 1.00f, 0.35f };

static void drawUnitBox(unsigned int color)
{
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    Vtx *v = (Vtx *)sceGuGetMemory(36 * sizeof(Vtx));
    int n = 0, f, k;
    for (f = 0; f < 6; f++) {
        unsigned int c = shadeColor(color, faceShade[f]);
        for (k = 0; k < 6; k++) {
            const float *p = cv[cf[f][order[k]]];
            v[n].color = c; v[n].x = p[0]; v[n].y = p[1]; v[n].z = p[2];
            n++;
        }
    }
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, v);
}

static float bx_, by_, bz_, byaw_, bscale_;
static void setBase(float x, float y, float z, float yaw, float s)
{
    bx_ = x; by_ = y; bz_ = z; byaw_ = yaw; bscale_ = s;
}

/* box hanging from pivot (px,py,pz), rotated rx about X, then centered at local offset l */
static void part(float px, float py, float pz, float rx,
                 float lx, float ly, float lz,
                 float sx, float sy, float sz, unsigned int col)
{
    ScePspFVector3 t1 = {bx_, by_, bz_}, gs = {bscale_, bscale_, bscale_};
    ScePspFVector3 t2 = {px, py, pz}, t3 = {lx, ly, lz}, sc = {sx, sy, sz};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t1);
    sceGumRotateY(byaw_);
    sceGumScale(&gs);
    sceGumTranslate(&t2);
    if (rx != 0.0f) sceGumRotateX(rx);
    sceGumTranslate(&t3);
    sceGumScale(&sc);
    drawUnitBox(col);
}

static void worldBox(float cx, float cy, float cz, float sx, float sy, float sz, unsigned int col)
{
    setBase(0, 0, 0, 0, 1.0f);
    part(cx, cy, cz, 0, 0, 0, 0, sx, sy, sz, col);
}

static void drawHumanoid(float x, float yoff, float z, float yaw, float s,
                         unsigned int armor, unsigned int helm, unsigned int cloth, unsigned int blade,
                         float swordLen, int shield, unsigned int shieldCol,
                         float armR, float armL, float walk, float move)
{
    float bob = fabsf(sinf(walk)) * 0.06f * move;
    float sw = sinf(walk) * 0.7f * move;
    setBase(x, yoff + bob * s, z, yaw, s);
    part(-0.16f, 0.85f, 0, sw, 0, -0.42f, 0, 0.24f, 0.85f, 0.28f, cloth);          /* legs */
    part( 0.16f, 0.85f, 0, -sw, 0, -0.42f, 0, 0.24f, 0.85f, 0.28f, cloth);
    part(0, 0.85f, 0, 0, 0, 0.37f, 0, 0.66f, 0.74f, 0.36f, armor);                 /* torso */
    part(0, 0.85f, 0, 0, 0, 0.04f, 0, 0.70f, 0.10f, 0.40f, shadeColor(cloth, 0.7f)); /* belt */
    part(0, 1.59f, 0, 0, 0, 0.18f, 0, 0.34f, 0.36f, 0.34f, helm);                  /* head */
    part(0, 1.59f, 0, 0, 0, 0.42f, -0.02f, 0.07f, 0.14f, 0.30f, shadeColor(helm, 0.7f)); /* crest */
    part(-0.44f, 1.5f, 0, armR, 0, -0.36f, 0, 0.20f, 0.72f, 0.22f, armor);         /* right arm */
    part( 0.44f, 1.5f, 0, armL, 0, -0.36f, 0, 0.20f, 0.72f, 0.22f, armor);         /* left arm */
    part(-0.44f, 1.5f, 0, armR, 0, -(0.72f + swordLen * 0.5f - 0.15f), 0, 0.06f, swordLen, 0.18f, blade);
    part(-0.44f, 1.5f, 0, armR, 0, -0.74f, 0, 0.10f, 0.07f, 0.34f, RGB(150, 120, 50)); /* guard */
    if (shield)
        part(0.44f, 1.5f, 0, armL, 0.10f, -0.42f, 0.20f, 0.07f, 0.66f, 0.50f, shieldCol);
}

/* 2D HUD helpers */
static void rect(int x0, int y0, int x1, int y1, unsigned int c)
{
    Vtx2D *v = (Vtx2D *)sceGuGetMemory(2 * sizeof(Vtx2D));
    v[0].color = c; v[0].x = (short)x0; v[0].y = (short)y0; v[0].z = 0;
    v[1].color = c; v[1].x = (short)x1; v[1].y = (short)y1; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static const unsigned char *glyph(char c)
{
    static const unsigned char G[10][5] = {
        {0x11,0x11,0x0A,0x04,0x04}, /* Y */
        {0x0E,0x11,0x11,0x11,0x0E}, /* O */
        {0x11,0x11,0x11,0x11,0x0E}, /* U */
        {0x1E,0x11,0x11,0x11,0x1E}, /* D */
        {0x1F,0x04,0x04,0x04,0x1F}, /* I */
        {0x1F,0x10,0x1E,0x10,0x1F}, /* E */
        {0x11,0x19,0x15,0x13,0x11}, /* N */
        {0x11,0x1B,0x15,0x11,0x11}, /* M */
        {0x1F,0x10,0x1E,0x10,0x10}, /* F */
        {0x10,0x10,0x10,0x10,0x1F}  /* L */
    };
    switch (c) {
    case 'Y': return G[0]; case 'O': return G[1]; case 'U': return G[2]; case 'D': return G[3];
    case 'I': return G[4]; case 'E': return G[5]; case 'N': return G[6]; case 'M': return G[7];
    case 'F': return G[8]; case 'L': return G[9];
    default: return NULL;
    }
}

static void drawText(const char *s, int x, int y, int sc, unsigned int col)
{
    for (; *s; s++, x += 6 * sc) {
        const unsigned char *g = glyph(*s);
        int r, c;
        if (!g) continue;
        for (r = 0; r < 5; r++)
            for (c = 0; c < 5; c++)
                if ((g[r] >> (4 - c)) & 1)
                    rect(x + c * sc, y + r * sc, x + c * sc + sc, y + r * sc + sc, col);
    }
}

/* ---------------------------------------------------------------- game state */
enum { P_FREE, P_ROLL, P_ATTACK, P_HEAL, P_STAGGER, P_DEAD };
enum { B_APPROACH, B_WINDUP, B_ACTIVE, B_RECOVER, B_DEAD };

typedef struct {
    float x, z, yaw, hp, stam, stamDelay, t, rdx, rdz, walk, moveAmt, hurtFlash;
    int state, flasks, hitDone, blocking, healed;
} Player;

typedef struct {
    float x, z, yaw, hp, t, cool, dirx, dirz, walk, moveAmt, slamFx, flash;
    int state, atk, hitDone;
} Boss;

static Player P;
static Boss B;
static float camYaw, camPitch = 0.5f, camDist = 7.0f;
static int locked = 0;
static float gTime = 0.0f;

static void resetFight(void)
{
    memset(&P, 0, sizeof(P));
    P.z = -10.0f; P.hp = PL_MAX_HP; P.stam = PL_MAX_ST; P.flasks = 3; P.state = P_FREE;
    memset(&B, 0, sizeof(B));
    B.z = 8.0f; B.yaw = PI; B.hp = BOSS_HP; B.state = B_APPROACH; B.cool = 2.5f;
    camYaw = PI; camPitch = 0.5f; locked = 0;
}

static void clampArena(float *x, float *z)
{
    *x = clampf(*x, -ARENA, ARENA);
    *z = clampf(*z, -ARENA, ARENA);
}

/* --------------- combat */
static void hurtPlayer(float dmg, float fromX, float fromZ)
{
    if (P.state == P_DEAD) return;
    if (P.state == P_ROLL && P.t > 0.06f && P.t < 0.38f) return; /* i-frames */

    if (P.blocking && P.state == P_FREE) {
        float dx = fromX - P.x, dz = fromZ - P.z, d = sqrtf(dx * dx + dz * dz);
        if (d < 0.001f) d = 0.001f;
        if ((dx / d) * sinf(P.yaw) + (dz / d) * cosf(P.yaw) > 0.2f) {
            P.hp -= dmg * 0.12f;
            P.stam -= dmg * 0.9f;
            P.stamDelay = 0.8f;
            P.hurtFlash = 0.12f;
            if (P.stam <= 0.0f) { P.stam = 0.0f; P.state = P_STAGGER; P.t = 0.0f; P.blocking = 0; }
            if (P.hp <= 0.0f) { P.hp = 0.0f; P.state = P_DEAD; P.t = 0.0f; }
            return;
        }
    }
    P.hp -= dmg;
    P.hurtFlash = 0.4f;
    P.blocking = 0;
    if (P.hp <= 0.0f) { P.hp = 0.0f; P.state = P_DEAD; P.t = 0.0f; }
    else { P.state = P_STAGGER; P.t = 0.0f; }
}

static void hitBoss(void)
{
    float dx = B.x - P.x, dz = B.z - P.z, d = sqrtf(dx * dx + dz * dz);
    if (B.state == B_DEAD || d < 0.001f) return;
    if (d < 3.6f && (dx / d) * sinf(P.yaw) + (dz / d) * cosf(P.yaw) > 0.25f) {
        B.hp -= 20.0f;
        B.flash = 0.15f;
        if (B.hp <= 0.0f) { B.hp = 0.0f; B.state = B_DEAD; B.t = 0.0f; locked = 0; }
    }
}

static void startRoll(float dx, float dz, float mag)
{
    P.state = P_ROLL; P.t = 0.0f; P.hitDone = 0; P.blocking = 0;
    P.stam -= 22.0f; P.stamDelay = 0.7f;
    if (mag > 0.2f) { P.rdx = dx; P.rdz = dz; P.yaw = atan2f(dx, dz); }
    else { P.rdx = -sinf(P.yaw); P.rdz = -cosf(P.yaw); } /* backstep */
}

static void updatePlayer(float dt, unsigned int pressed, unsigned int held, float ax, float ay)
{
    float fx = -sinf(camYaw), fz = -cosf(camYaw);
    float rx = cosf(camYaw), rz = -sinf(camYaw);
    float mag = sqrtf(ax * ax + ay * ay);
    float dx = 0.0f, dz = 0.0f, moveTarget = 0.0f;
    float bossYaw = atan2f(B.x - P.x, B.z - P.z);

    if (mag > 1.0f) mag = 1.0f;
    if (mag > 0.18f) {
        float l;
        dx = (-ay) * fx + ax * rx;
        dz = (-ay) * fz + ax * rz;
        l = sqrtf(dx * dx + dz * dz);
        if (l > 0.0001f) { dx /= l; dz /= l; }
    } else mag = 0.0f;

    if (P.hurtFlash > 0.0f) P.hurtFlash -= dt;
    if (P.stamDelay > 0.0f) P.stamDelay -= dt;

    switch (P.state) {
    case P_FREE: {
        float spd;
        P.blocking = ((held & PSP_CTRL_LTRIGGER) && P.stam > 0.0f) ? 1 : 0;
        spd = (P.blocking ? 2.2f : 5.2f) * mag;
        if (mag > 0.0f) { P.x += dx * spd * dt; P.z += dz * spd * dt; moveTarget = 1.0f; P.walk += spd * dt * 1.7f; }
        if (locked && B.state != B_DEAD) P.yaw = turnToward(P.yaw, bossYaw, 14.0f * dt);
        else if (mag > 0.0f) P.yaw = turnToward(P.yaw, atan2f(dx, dz), 16.0f * dt);
        if (P.stamDelay <= 0.0f) P.stam += (P.blocking ? 14.0f : 38.0f) * dt;
        if (pressed & PSP_CTRL_CROSS) startRoll(dx, dz, mag);
        else if (pressed & PSP_CTRL_SQUARE) {
            P.state = P_ATTACK; P.t = 0.0f; P.hitDone = 0; P.blocking = 0;
            P.stam -= 18.0f; P.stamDelay = 0.7f;
        } else if ((pressed & PSP_CTRL_TRIANGLE) && P.flasks > 0 && P.hp < PL_MAX_HP) {
            P.state = P_HEAL; P.t = 0.0f; P.healed = 0; P.blocking = 0;
        }
    } break;

    case P_ROLL: {
        float sp;
        P.t += dt;
        sp = 9.0f * (1.0f - 0.45f * (P.t / 0.5f));
        P.x += P.rdx * sp * dt; P.z += P.rdz * sp * dt;
        moveTarget = 1.0f; P.walk += dt * 12.0f;
        if (P.t >= 0.5f) P.state = P_FREE;
    } break;

    case P_ATTACK: {
        P.t += dt;
        if (locked && B.state != B_DEAD) P.yaw = turnToward(P.yaw, bossYaw, 8.0f * dt);
        if (P.t < 0.25f) { P.x += sinf(P.yaw) * 2.0f * dt; P.z += cosf(P.yaw) * 2.0f * dt; }
        if (!P.hitDone && P.t >= 0.24f) { P.hitDone = 1; hitBoss(); }
        if (P.t >= 0.38f && (pressed & PSP_CTRL_CROSS)) { startRoll(dx, dz, mag); break; }
        if (P.t >= 0.55f) P.state = P_FREE;
    } break;

    case P_HEAL: {
        float spd = 1.3f * mag;
        P.t += dt;
        if (mag > 0.0f) { P.x += dx * spd * dt; P.z += dz * spd * dt; moveTarget = 0.4f; P.walk += spd * dt * 1.7f; }
        if (!P.healed && P.t >= 0.5f) {
            P.healed = 1; P.flasks--;
            P.hp = P.hp + 45.0f > PL_MAX_HP ? PL_MAX_HP : P.hp + 45.0f;
        }
        if (P.t >= 1.0f) P.state = P_FREE;
    } break;

    case P_STAGGER:
        P.t += dt;
        if (P.t >= 0.6f) P.state = P_FREE;
        break;

    case P_DEAD:
        P.t += dt;
        break;
    }

    P.stam = clampf(P.stam, 0.0f, PL_MAX_ST);
    clampArena(&P.x, &P.z);
    P.moveAmt += (moveTarget - P.moveAmt) * clampf(dt * 10.0f, 0.0f, 1.0f);
}

/* boss attack timings: 0 = overhead swing, 1 = slam, 2 = lunge */
static const float BW[3] = {0.85f, 1.15f, 0.70f};
static const float BA[3] = {0.22f, 0.25f, 0.40f};
static const float BR[3] = {0.80f, 1.10f, 0.90f};

static void startBossAtk(int a)
{
    B.state = B_WINDUP; B.atk = a; B.t = 0.0f; B.hitDone = 0;
}

static void updateBoss(float dt)
{
    float dx = P.x - B.x, dz = P.z - B.z, dist = sqrtf(dx * dx + dz * dz);
    float bdt = dt * (B.hp < BOSS_HP * 0.5f ? 1.25f : 1.0f);
    float spd = 0.0f;
    float facing;

    if (B.flash > 0.0f) B.flash -= dt;
    if (B.slamFx > 0.0f) B.slamFx -= dt;
    if (dist < 0.001f) dist = 0.001f;
    facing = atan2f(dx, dz);

    switch (B.state) {
    case B_DEAD:
        B.t += dt;
        break;

    case B_APPROACH:
        B.yaw = turnToward(B.yaw, facing, 4.5f * dt);
        B.cool -= bdt;
        if (P.state != P_DEAD && dist > 2.8f) {
            spd = dist > 8.0f ? 3.6f : 2.6f;
            if (B.hp < BOSS_HP * 0.5f) spd *= 1.2f;
            B.x += sinf(B.yaw) * spd * dt;
            B.z += cosf(B.yaw) * spd * dt;
        }
        if (B.cool <= 0.0f && P.state != P_DEAD) {
            if (dist < 4.0f) startBossAtk((rand() % 100) < 60 ? 0 : 1);
            else if (dist > 6.0f && (rand() % 100) < 40) startBossAtk(2);
            else B.cool = 0.4f;
        }
        break;

    case B_WINDUP:
        B.t += bdt;
        if (B.t < BW[B.atk] * 0.6f) B.yaw = turnToward(B.yaw, facing, 5.0f * dt);
        if (B.t >= BW[B.atk]) {
            B.state = B_ACTIVE; B.t = 0.0f;
            B.dirx = sinf(B.yaw); B.dirz = cosf(B.yaw);
        }
        break;

    case B_ACTIVE:
        B.t += bdt;
        if (B.atk == 0) {
            if (!B.hitDone && B.t >= 0.08f) {
                B.hitDone = 1;
                if (dist < 4.3f && (dx / dist) * sinf(B.yaw) + (dz / dist) * cosf(B.yaw) > 0.1f)
                    hurtPlayer(38.0f, B.x, B.z);
            }
        } else if (B.atk == 1) {
            if (!B.hitDone && B.t >= 0.05f) {
                B.hitDone = 1; B.slamFx = 0.4f;
                if (dist < 5.2f) hurtPlayer(50.0f, B.x, B.z);
            }
        } else {
            B.x += B.dirx * 16.0f * dt;
            B.z += B.dirz * 16.0f * dt;
            spd = 8.0f;
            if (!B.hitDone && dist < 2.4f) { B.hitDone = 1; hurtPlayer(34.0f, B.x, B.z); }
        }
        if (B.t >= BA[B.atk]) { B.state = B_RECOVER; B.t = 0.0f; }
        break;

    case B_RECOVER:
        B.t += bdt;
        if (B.t >= BR[B.atk]) {
            B.state = B_APPROACH;
            B.cool = 0.3f + (rand() % 5) * 0.1f;
        }
        break;
    }

    clampArena(&B.x, &B.z);
    B.walk += spd * dt * 1.2f;
    B.moveAmt += ((spd > 0.0f ? 1.0f : 0.0f) - B.moveAmt) * clampf(dt * 8.0f, 0.0f, 1.0f);
}

static void updateCamera(float dt, unsigned int held)
{
    if (!locked) {
        if (held & PSP_CTRL_LEFT)  camYaw += 2.4f * dt;
        if (held & PSP_CTRL_RIGHT) camYaw -= 2.4f * dt;
    } else {
        float want = atan2f(P.x - B.x, P.z - B.z);
        camYaw += wrapAng(want - camYaw) * (1.0f - expf(-6.0f * dt));
    }
    if (held & PSP_CTRL_UP)   camPitch -= 1.2f * dt;
    if (held & PSP_CTRL_DOWN) camPitch += 1.2f * dt;
    camPitch = clampf(camPitch, 0.12f, 1.1f);
}

/* ---------------------------------------------------------------- render */
static float bossArm(void)
{
    const float up = -3.1f;
    float a = -0.6f, u;
    if (B.state == B_WINDUP) {
        u = clampf(B.t / BW[B.atk], 0.0f, 1.0f);
        a = (B.atk == 2) ? lerpf(-0.6f, -1.6f, u) : lerpf(-0.6f, up, u);
    } else if (B.state == B_ACTIVE) {
        u = clampf(B.t / BA[B.atk], 0.0f, 1.0f);
        a = (B.atk == 0) ? lerpf(up, -0.2f, u) : (B.atk == 1 ? lerpf(up, 0.1f, u) : -1.6f);
    } else if (B.state == B_RECOVER) {
        float endA = (B.atk == 2) ? -1.6f : (B.atk == 0 ? -0.2f : 0.1f);
        u = clampf(B.t / BR[B.atk], 0.0f, 1.0f);
        a = lerpf(endA, -0.6f, u * u);
    }
    return a;
}

static void drawArena(void)
{
    int i, j;
    float fh;
    worldBox(0, -0.35f, 0, 36.6f, 0.3f, 36.6f, RGB(12, 12, 18));
    for (i = 0; i < 6; i++)
        for (j = 0; j < 6; j++)
            worldBox(-15.0f + 6.0f * i, -0.1f, -15.0f + 6.0f * j, 5.9f, 0.2f, 5.9f,
                     ((i + j) & 1) ? RGB(58, 56, 66) : RGB(74, 71, 82));
    /* low walls */
    worldBox(0, 0.7f, 18.3f, 37.2f, 1.4f, 0.6f, RGB(46, 44, 54));
    worldBox(0, 0.7f, -18.3f, 37.2f, 1.4f, 0.6f, RGB(46, 44, 54));
    worldBox(18.3f, 0.7f, 0, 0.6f, 1.4f, 37.2f, RGB(46, 44, 54));
    worldBox(-18.3f, 0.7f, 0, 0.6f, 1.4f, 37.2f, RGB(46, 44, 54));
    /* pillars */
    for (i = -1; i <= 1; i++)
        for (j = -1; j <= 1; j++) {
            float h;
            if (i == 0 && j == 0) continue;
            h = ((i + j + 2) & 1) ? 4.0f : 2.6f;
            worldBox(i * 18.0f, h * 0.5f, j * 18.0f, 1.5f, h, 1.5f, RGB(88, 85, 98));
            worldBox(i * 18.0f, h + 0.15f, j * 18.0f, 1.9f, 0.3f, 1.9f, RGB(104, 100, 114));
        }
    /* bonfire */
    worldBox(0, 0.15f, -16.0f, 1.6f, 0.3f, 1.6f, RGB(40, 36, 34));
    worldBox(0.3f, 0.45f, -16.0f, 0.3f, 0.6f, 1.2f, RGB(70, 50, 36));
    worldBox(-0.3f, 0.45f, -16.0f, 0.3f, 0.6f, 1.2f, RGB(70, 50, 36));
    fh = 0.9f + 0.2f * sinf(gTime * 13.0f) + 0.12f * sinf(gTime * 29.0f);
    worldBox(0, 0.75f + fh * 0.5f, -16.0f, 0.8f, fh, 0.8f, RGB(255, 120, 30));
    worldBox(0, 0.75f + fh * 0.35f, -16.0f, 0.45f, fh * 0.7f, 0.45f, RGB(255, 220, 90));
    /* slam shockwave */
    if (B.slamFx > 0.0f) {
        float r = 5.2f * (1.0f - B.slamFx / 0.4f);
        setBase(B.x, 0.08f, B.z, 0, 1.0f);
        part(r, 0, 0, 0, 0, 0, 0, 0.4f, 0.12f, 2 * r + 0.4f, RGB(255, 150, 40));
        part(-r, 0, 0, 0, 0, 0, 0, 0.4f, 0.12f, 2 * r + 0.4f, RGB(255, 150, 40));
        part(0, 0, r, 0, 0, 0, 0, 2 * r + 0.4f, 0.12f, 0.4f, RGB(255, 150, 40));
        part(0, 0, -r, 0, 0, 0, 0, 2 * r + 0.4f, 0.12f, 0.4f, RGB(255, 150, 40));
    }
}

static void drawPlayer(void)
{
    unsigned int armor = RGB(118, 128, 150), helm = RGB(150, 156, 172);
    unsigned int cloth = RGB(62, 50, 46), blade = RGB(215, 220, 232);
    float armR = -0.6f, armL = -0.3f, s = 1.0f, t = P.t;

    if (P.state == P_DEAD) {
        if (t > 0.7f) return;
        s = 1.0f - t;
    }
    if (P.state == P_ROLL) s = 0.72f;
    if (P.hurtFlash > 0.0f) armor = mixColor(armor, RGB(220, 40, 40), clampf(P.hurtFlash * 2.5f, 0.0f, 1.0f));

    if (P.state == P_ATTACK) {
        if (t < 0.18f) armR = lerpf(-0.6f, -3.0f, t / 0.18f);
        else if (t < 0.32f) armR = lerpf(-3.0f, -0.3f, (t - 0.18f) / 0.14f);
        else armR = lerpf(-0.3f, -0.6f, clampf((t - 0.32f) / 0.23f, 0.0f, 1.0f));
    } else if (P.state == P_HEAL) armL = -2.1f;
    else if (P.state == P_STAGGER || P.state == P_DEAD) { armR = 0.5f; armL = 0.5f; }
    else if (P.blocking) armL = -1.5f;

    drawHumanoid(P.x, 0.0f, P.z, P.yaw, s, armor, helm, cloth, blade, 1.1f, 1, RGB(96, 64, 44),
                 armR, armL, P.walk, P.moveAmt);
}

static void drawBoss(void)
{
    unsigned int armor = RGB(105, 38, 36), helm = RGB(60, 24, 24);
    unsigned int cloth = RGB(38, 26, 26), blade = RGB(235, 130, 70);
    float yoff = 0.0f;

    if (B.state == B_DEAD) {
        if (B.t > 3.5f) return;
        yoff = -clampf(B.t * 0.7f, 0.0f, 3.0f);
    }
    if (B.hp < BOSS_HP * 0.5f) armor = mixColor(armor, RGB(160, 40, 30), 0.6f);
    if (B.state == B_WINDUP) {
        unsigned int tint = (B.atk == 1) ? RGB(255, 150, 40) : (B.atk == 2 ? RGB(255, 230, 90) : RGB(255, 255, 255));
        float m = 0.35f + 0.25f * sinf(B.t * 28.0f);
        armor = mixColor(armor, tint, m);
        helm = mixColor(helm, tint, m);
        blade = mixColor(blade, RGB(255, 255, 255), 0.5f);
    }
    if (B.flash > 0.0f) armor = mixColor(armor, RGB(255, 255, 255), 0.6f);

    drawHumanoid(B.x, yoff, B.z, B.yaw, 1.7f, armor, helm, cloth, blade, 1.4f, 0, 0,
                 bossArm(), -0.3f, B.walk, B.moveAmt);

    if (locked && B.state != B_DEAD) {
        setBase(B.x, 3.9f, B.z, gTime * 3.0f, 1.0f);
        part(0, 0, 0, 0, 0, 0, 0, 0.28f, 0.28f, 0.28f, RGB(255, 80, 60));
    }
}

static void drawBar(int x, int y, int w, int h, float frac, unsigned int col)
{
    rect(x - 2, y - 2, x + w + 2, y + h + 2, RGBA(0, 0, 0, 200));
    rect(x, y, x + w, y + h, RGBA(50, 50, 50, 255));
    if (frac > 0.0f) rect(x, y, x + (int)(w * clampf(frac, 0.0f, 1.0f)), y + h, col);
}

static void drawHud(void)
{
    int i;
    drawBar(14, 14, 170, 9, P.hp / PL_MAX_HP, RGB(190, 30, 30));
    drawBar(14, 28, 130, 6, P.stam / PL_MAX_ST, RGB(70, 190, 80));
    for (i = 0; i < 3; i++)
        rect(14 + i * 12, 40, 22 + i * 12, 48, i < P.flasks ? RGB(240, 170, 40) : RGB(60, 50, 40));
    if (B.state != B_DEAD || B.t < 2.0f)
        drawBar(100, 248, 280, 8, B.hp / BOSS_HP, RGB(170, 25, 25));

    if (P.hurtFlash > 0.0f)
        rect(0, 0, SCR_W, SCR_H, RGBA(180, 0, 0, (int)(clampf(P.hurtFlash, 0.0f, 0.4f) * 150.0f)));

    if (P.state == P_DEAD && P.t > 0.8f) {
        float a = clampf((P.t - 0.8f) / 1.2f, 0.0f, 1.0f);
        rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, (int)(a * 150.0f)));
        if (a > 0.3f) {
            int al = (int)(a * 255.0f);
            drawText("YOU DIED", 49, 111, 8, RGBA(0, 0, 0, al));
            drawText("YOU DIED", 48, 110, 8, RGBA(170, 20, 20, al));
        }
    }
    if (B.state == B_DEAD && B.t > 1.0f) {
        float a = clampf((B.t - 1.0f) / 1.5f, 0.0f, 1.0f);
        int al = (int)(a * 255.0f);
        rect(0, 110, SCR_W, 170, RGBA(0, 0, 0, (int)(a * 110.0f)));
        drawText("ENEMY FELLED", 61, 121, 5, RGBA(0, 0, 0, al));
        drawText("ENEMY FELLED", 60, 120, 5, RGBA(235, 190, 70, al));
    }
}

static void render(void)
{
    ScePspFVector3 t0, t1;

    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(FOGC);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_FOG);
    sceGuDisable(GU_BLEND);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(70.0f, 16.0f / 9.0f, 0.4f, 120.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    t0.x = 0; t0.y = 0; t0.z = -(camDist + (locked ? 1.5f : 0.0f));
    sceGumTranslate(&t0);
    sceGumRotateX(camPitch);
    sceGumRotateY(-camYaw);
    t1.x = -P.x; t1.y = -1.3f; t1.z = -P.z;
    sceGumTranslate(&t1);

    drawArena();
    drawBoss();
    drawPlayer();

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    sceGuEnable(GU_BLEND);
    drawHud();

    sceGuFinish();
    sceGuSync(0, 0);
}

static void initGraphics(void)
{
    void *fbp0 = vramAlloc(BUF_WIDTH, SCR_H, GU_PSM_8888);
    void *fbp1 = vramAlloc(BUF_WIDTH, SCR_H, GU_PSM_8888);
    void *zbp  = vramAlloc(BUF_WIDTH, SCR_H, GU_PSM_4444);

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_WIDTH);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_WIDTH);
    sceGuDepthBuffer(zbp, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFog(6.0f, 45.0f, FOGC);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(int argc, char *argv[])
{
    SceCtrlData pad;
    unsigned int oldButtons = 0;
    u64 last, now;
    unsigned int res;

    setup_callbacks();
    initGraphics();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    resetFight();

    res = sceRtcGetTickResolution();
    sceRtcGetCurrentTick(&last);

    for (;;) {
        float dt, ax, ay;
        unsigned int pressed;

        sceRtcGetCurrentTick(&now);
        dt = (float)(now - last) / (float)res;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0f) dt = 0.0f;
        gTime += dt;

        sceCtrlReadBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~oldButtons;
        oldButtons = pad.Buttons;
        ax = ((int)pad.Lx - 128) / 128.0f;
        ay = ((int)pad.Ly - 128) / 128.0f;

        if ((pressed & PSP_CTRL_RTRIGGER) && B.state != B_DEAD && P.state != P_DEAD) locked = !locked;

        updateCamera(dt, pad.Buttons);
        updatePlayer(dt, pressed, pad.Buttons, ax, ay);
        updateBoss(dt);

        /* keep player out of the boss */
        if (B.state != B_DEAD && !(P.state == P_ROLL && P.t < 0.4f)) {
            float dx = P.x - B.x, dz = P.z - B.z, d = sqrtf(dx * dx + dz * dz);
            if (d < 1.7f && d > 0.001f) {
                P.x = B.x + dx / d * 1.7f;
                P.z = B.z + dz / d * 1.7f;
                clampArena(&P.x, &P.z);
            }
        }

        if (P.state == P_DEAD && P.t > 2.5f && ((pressed & PSP_CTRL_CROSS) || P.t > 6.0f)) resetFight();
        if (B.state == B_DEAD && B.t > 3.0f && ((pressed & PSP_CTRL_CROSS) || B.t > 9.0f)) resetFight();

        render();
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
    return 0;
}
