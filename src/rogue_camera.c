#include "rogue_camera.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#define HALF_PI (M_PI * 0.5f)

#define CAM_PITCH   (-0.62f)   /* ~ -35.5°, classic iso 3/4 tilt */
#define CAM_DIST    14.0f      /* distance from focus along view dir */
#define CAM_FOV     0.95f
#define FOCUS_LERP  9.0f       /* focus catch-up rate (1/s) */
#define YAW_LERP    11.0f      /* yaw snap-tween rate (1/s) */

static Vec3  s_focus;          /* smoothed look-at point */
static int   s_yaw_idx;        /* 0..3 */
static float s_yaw_cur;        /* animated yaw, radians */

static float lerp_angle(float a, float target, float k) {
    /* shortest-arc lerp */
    float d = target - a;
    while (d >  M_PI) d -= 2.0f * M_PI;
    while (d < -M_PI) d += 2.0f * M_PI;
    return a + d * k;
}

void rogue_camera_init(Vec3 target) {
    s_focus = target;
    s_yaw_idx = 0;
    s_yaw_cur = 0.0f;
}

void rogue_camera_follow(Vec3 target, float dt) {
    float k = FOCUS_LERP * dt;
    if (k > 1.0f) k = 1.0f;
    s_focus.x += (target.x - s_focus.x) * k;
    s_focus.y += (target.y - s_focus.y) * k;
    s_focus.z += (target.z - s_focus.z) * k;
}

void rogue_camera_rotate(int dir) {
    s_yaw_idx = (s_yaw_idx + dir) & 3;
}

int   rogue_camera_yaw_index(void) { return s_yaw_idx; }
float rogue_camera_yaw(void)        { return s_yaw_cur; }
float rogue_camera_snapped_yaw(void){ return s_yaw_idx * HALF_PI; }

void rogue_camera_update(float dt) {
    float k = YAW_LERP * dt;
    if (k > 1.0f) k = 1.0f;
    s_yaw_cur = lerp_angle(s_yaw_cur, s_yaw_idx * HALF_PI, k);
}

void rogue_camera_get(CraftCamera *out) {
    /* Full pitched forward vector; camera sits back along it so the look
     * direction lands exactly on the focus point. */
    float cp = cosf(CAM_PITCH), sp = sinf(CAM_PITCH);
    float cy = cosf(s_yaw_cur), sy = sinf(s_yaw_cur);
    Vec3 fwd = v3(sy * cp, sp, cy * cp);
    /* The camera sits at its full iso offset and may be OUTSIDE the world
     * window — the raycaster advances each ray to the world entry point
     * (ROGUE_FULLFRAME_RENDER patch in craft_render.c), so the hero stays
     * perfectly centred even in edge rooms. No clamping. */
    out->pos.x = s_focus.x - fwd.x * CAM_DIST;
    out->pos.y = s_focus.y - fwd.y * CAM_DIST + 0.6f; /* aim a touch above feet */
    out->pos.z = s_focus.z - fwd.z * CAM_DIST;
    out->yaw   = s_yaw_cur;
    out->pitch = CAM_PITCH;
    out->fov   = CAM_FOV;
}
