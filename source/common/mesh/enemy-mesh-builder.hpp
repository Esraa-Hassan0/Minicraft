#pragma once
/*
    enemy-mesh-builder.hpp  —  UV-mapped Minecraft-style enemy meshes
    Textures: zombie.png, skeleton.png, creeper.png  (in assets/textures/)
    All textures follow the standard Minecraft 64x64 skin layout.
    Creeper uses 64x32.
*/

#include "../mesh/mesh.hpp"
#include "../mesh/vertex.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace our::enemy_mesh {

static constexpr float TW   = 64.0f;
static constexpr float TH64 = 64.0f;
static constexpr float TH32 = 32.0f;

// Push a textured quad (TL TR BR BL winding)
static void pushFace(
    std::vector<Vertex>& verts,
    std::vector<unsigned int>& idx,
    glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
    glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
    glm::vec3 normal)
{
    Color white = {255,255,255,255};
    unsigned int base = (unsigned int)verts.size();
    verts.push_back({p0, white, uv0, normal});
    verts.push_back({p1, white, uv1, normal});
    verts.push_back({p2, white, uv2, normal});
    verts.push_back({p3, white, uv3, normal});
    idx.insert(idx.end(), {base,base+1,base+2, base,base+2,base+3});
}

// Build a textured box with per-face UV rects (pixel coords on texture sheet)
static void pushBox(
    std::vector<Vertex>& verts, std::vector<unsigned int>& idx,
    glm::vec3 center, glm::vec3 half,
    float tx_top,float ty_top,float tw_top,float th_top,
    float tx_bot,float ty_bot,float tw_bot,float th_bot,
    float tx_fr, float ty_fr, float tw_fr, float th_fr,
    float tx_bk, float ty_bk, float tw_bk, float th_bk,
    float tx_lf, float ty_lf, float tw_lf, float th_lf,
    float tx_rt, float ty_rt, float tw_rt, float th_rt,
    float texH = TH64)
{
    float x=center.x,y=center.y,z=center.z;
    float hx=half.x,hy=half.y,hz=half.z;
    auto u=[](float px){ return px/TW; };
    auto v=[&](float py){ return 1.0f - (py/texH); };

    // +Y top
    pushFace(verts,idx, {x-hx,y+hy,z-hz},{x+hx,y+hy,z-hz},{x+hx,y+hy,z+hz},{x-hx,y+hy,z+hz},
        {u(tx_top),v(ty_top)},{u(tx_top+tw_top),v(ty_top)},
        {u(tx_top+tw_top),v(ty_top+th_top)},{u(tx_top),v(ty_top+th_top)},{0,1,0});
    // -Y bottom
    pushFace(verts,idx, {x-hx,y-hy,z+hz},{x+hx,y-hy,z+hz},{x+hx,y-hy,z-hz},{x-hx,y-hy,z-hz},
        {u(tx_bot),v(ty_bot)},{u(tx_bot+tw_bot),v(ty_bot)},
        {u(tx_bot+tw_bot),v(ty_bot+th_bot)},{u(tx_bot),v(ty_bot+th_bot)},{0,-1,0});
    // +Z front
    pushFace(verts,idx, {x-hx,y+hy,z+hz},{x+hx,y+hy,z+hz},{x+hx,y-hy,z+hz},{x-hx,y-hy,z+hz},
        {u(tx_fr),v(ty_fr)},{u(tx_fr+tw_fr),v(ty_fr)},
        {u(tx_fr+tw_fr),v(ty_fr+th_fr)},{u(tx_fr),v(ty_fr+th_fr)},{0,0,1});
    // -Z back
    pushFace(verts,idx, {x+hx,y+hy,z-hz},{x-hx,y+hy,z-hz},{x-hx,y-hy,z-hz},{x+hx,y-hy,z-hz},
        {u(tx_bk),v(ty_bk)},{u(tx_bk+tw_bk),v(ty_bk)},
        {u(tx_bk+tw_bk),v(ty_bk+th_bk)},{u(tx_bk),v(ty_bk+th_bk)},{0,0,-1});
    // -X left
    pushFace(verts,idx, {x-hx,y+hy,z-hz},{x-hx,y+hy,z+hz},{x-hx,y-hy,z+hz},{x-hx,y-hy,z-hz},
        {u(tx_lf),v(ty_lf)},{u(tx_lf+tw_lf),v(ty_lf)},
        {u(tx_lf+tw_lf),v(ty_lf+th_lf)},{u(tx_lf),v(ty_lf+th_lf)},{-1,0,0});
    // +X right
    pushFace(verts,idx, {x+hx,y+hy,z+hz},{x+hx,y+hy,z-hz},{x+hx,y-hy,z-hz},{x+hx,y-hy,z+hz},
        {u(tx_rt),v(ty_rt)},{u(tx_rt+tw_rt),v(ty_rt)},
        {u(tx_rt+tw_rt),v(ty_rt+th_rt)},{u(tx_rt),v(ty_rt+th_rt)},{1,0,0});
}

// ── ZOMBIE (zombie.png 64×64, standard player skin layout) ─────────────
inline Mesh* buildZombieMesh() {
    std::vector<Vertex> verts; std::vector<unsigned int> idx;
    // HEAD  (center Y=1.75, half=0.25 → bottom=1.5 = body top)
    pushBox(verts,idx, {0,1.75f,0},{.25f,.25f,.25f},
        8,0,8,8,  16,0,8,8,  8,8,8,8,  24,8,8,8,  0,8,8,8,  16,8,8,8);
    // BODY  (center Y=1.125, half=0.375 → top=1.5, bottom=0.75)
    pushBox(verts,idx, {0,1.125f,0},{.25f,.375f,.125f},
        20,16,8,4, 28,16,8,4, 20,20,8,12, 32,20,8,12, 16,20,4,12, 28,20,4,12);
    // RIGHT ARM  (X = -(body_hx + arm_hx) = -(0.25+0.125) = -0.375 → flush with body)
    pushBox(verts,idx, {-.375f,1.125f,0},{.125f,.375f,.125f},
        44,16,4,4, 48,16,4,4, 44,20,4,12, 52,20,4,12, 40,20,4,12, 48,20,4,12);
    // LEFT ARM   (X = +(body_hx + arm_hx) = 0.375 → flush with body)
    pushBox(verts,idx, {.375f,1.125f,0},{.125f,.375f,.125f},
        36,48,4,4, 40,48,4,4, 36,52,4,12, 44,52,4,12, 32,52,4,12, 40,52,4,12);
    // RIGHT LEG  (X = -leg_hx = -0.125 → inner face at X=0, outer at X=-0.25)
    pushBox(verts,idx, {-.125f,.375f,0},{.125f,.375f,.125f},
        4,16,4,4,  8,16,4,4,  4,20,4,12, 12,20,4,12, 0,20,4,12,  8,20,4,12);
    // LEFT LEG   (X = +leg_hx =  0.125 → inner face at X=0, outer at X=+0.25)
    pushBox(verts,idx, {.125f,.375f,0},{.125f,.375f,.125f},
        20,48,4,4, 24,48,4,4, 20,52,4,12, 28,52,4,12, 16,52,4,12, 24,52,4,12);
    return new Mesh(verts,idx);
}

// ── SKELETON (skeleton.png 64×64, same layout, thinner body) ───────────
inline Mesh* buildSkeletonMesh() {
    std::vector<Vertex> verts; std::vector<unsigned int> idx;
    // HEAD  (same as zombie)
    pushBox(verts,idx, {0,1.75f,0},{.25f,.25f,.25f},
        8,0,8,8,  16,0,8,8,  8,8,8,8,  24,8,8,8,  0,8,8,8,  16,8,8,8);
    // BODY (thinner: half_x=0.1875)
    pushBox(verts,idx, {0,1.125f,0},{.1875f,.375f,.09375f},
        20,16,8,4, 28,16,8,4, 20,20,8,12, 32,20,8,12, 16,20,4,12, 28,20,4,12);
    // RIGHT ARM  (X = -(body_hx + arm_hx) = -(0.1875+0.09375) = -0.28125 → flush)
    pushBox(verts,idx, {-.28125f,1.125f,0},{.09375f,.375f,.09375f},
        44,16,4,4, 48,16,4,4, 44,20,4,12, 52,20,4,12, 40,20,4,12, 48,20,4,12);
    // LEFT ARM   (X = +(body_hx + arm_hx) = 0.28125 → flush)
    pushBox(verts,idx, {.28125f,1.125f,0},{.09375f,.375f,.09375f},
        36,48,4,4, 40,48,4,4, 36,52,4,12, 44,52,4,12, 32,52,4,12, 40,52,4,12);
    // RIGHT LEG  (X = -leg_hx = -0.09375)
    pushBox(verts,idx, {-.09375f,.375f,0},{.09375f,.375f,.09375f},
        4,16,4,4,  8,16,4,4,  4,20,4,12, 12,20,4,12, 0,20,4,12,  8,20,4,12);
    // LEFT LEG   (X = +leg_hx = 0.09375)
    pushBox(verts,idx, {.09375f,.375f,0},{.09375f,.375f,.09375f},
        20,48,4,4, 24,48,4,4, 20,52,4,12, 28,52,4,12, 16,52,4,12, 24,52,4,12);
    return new Mesh(verts,idx);
}

// ── CREEPER (creeper.png 64×32, 4 legs, no arms) ───────────────────────
inline Mesh* buildCreeperMesh() {
    std::vector<Vertex> verts; std::vector<unsigned int> idx;
    const float H = TH32;
    // HEAD  (center Y=1.65, half=0.3 → bottom=1.35 = body top; no overlap/gap)
    pushBox(verts,idx, {0,1.65f,0},{.3f,.3f,.3f},
        8,0,8,8,  16,0,8,8,  8,8,8,8,  24,8,8,8,  0,8,8,8,  16,8,8,8, H);
    // BODY  (center Y=0.9, half=0.45 → top=1.35, bottom=0.45 = leg tops)
    pushBox(verts,idx, {0,.9f,0},{.2f,.45f,.2f},
        20,0,4,8, 24,0,4,8, 20,8,4,12, 28,8,4,12, 16,8,4,12, 24,8,4,12, H);
    // 4 LEGS  (0.005 inset between adjacent legs to prevent Z-fighting)
    float legX[]={ -.105f, .105f, -.105f,  .105f };
    float legZ[]={  .105f,  .105f, -.105f, -.105f };
    for(int i=0;i<4;i++)
        pushBox(verts,idx, {legX[i],.225f,legZ[i]},{.1f,.225f,.1f},
            4,16,4,4, 8,16,4,4, 4,20,4,6, 12,20,4,6, 0,20,4,6, 8,20,4,6, H);
    return new Mesh(verts,idx);
}

// ── Projectile (colored box, no texture needed) ─────────────────────────
static void pushColorFace(std::vector<Vertex>& verts,std::vector<unsigned int>& idx,
    glm::vec3 p0,glm::vec3 p1,glm::vec3 p2,glm::vec3 p3,Color col,glm::vec3 n)
{
    glm::vec2 z{0,0};
    unsigned int base=(unsigned int)verts.size();
    verts.push_back({p0,col,z,n}); verts.push_back({p1,col,z,n});
    verts.push_back({p2,col,z,n}); verts.push_back({p3,col,z,n});
    idx.insert(idx.end(),{base,base+1,base+2,base,base+2,base+3});
}
inline Mesh* buildProjectileMesh() {
    std::vector<Vertex> verts; std::vector<unsigned int> idx;
    Color s={160,110,50,255}, t={200,200,200,255};
    // shaft
    pushColorFace(verts,idx,{-.03f,.03f,-.2f},{.03f,.03f,-.2f},{.03f,.03f,.2f},{-.03f,.03f,.2f},s,{0,1,0});
    pushColorFace(verts,idx,{-.03f,-.03f,.2f},{.03f,-.03f,.2f},{.03f,-.03f,-.2f},{-.03f,-.03f,-.2f},s,{0,-1,0});
    pushColorFace(verts,idx,{-.03f,.03f,.2f},{.03f,.03f,.2f},{.03f,-.03f,.2f},{-.03f,-.03f,.2f},s,{0,0,1});
    pushColorFace(verts,idx,{.03f,.03f,-.2f},{-.03f,.03f,-.2f},{-.03f,-.03f,-.2f},{.03f,-.03f,-.2f},s,{0,0,-1});
    // tip
    pushColorFace(verts,idx,{-.04f,.04f,.2f},{.04f,.04f,.2f},{.04f,.04f,.24f},{-.04f,.04f,.24f},t,{0,1,0});
    pushColorFace(verts,idx,{-.04f,.04f,.24f},{.04f,.04f,.24f},{.04f,-.04f,.24f},{-.04f,-.04f,.24f},t,{0,0,1});
    return new Mesh(verts,idx);
}

} // namespace our::enemy_mesh
