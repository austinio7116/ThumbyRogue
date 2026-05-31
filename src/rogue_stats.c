#include "rogue_stats.h"

static void add_gem(RogueStats *o, GemType g) {
    switch (g) {
        case GEM_RUBY:     o->max_life += 20; break;
        case GEM_SAPPHIRE: o->resist   += 8;  break;
        case GEM_EMERALD:  o->crit     += 4;  break;
        case GEM_TOPAZ:    o->armor    += 10; break;
        default: break;
    }
}

void rogue_stats_compute(RogueStats *o, const RogueItem equip[SLOT_COUNT]) {
    o->max_life = 100;
    o->armor = 0; o->flat_dmg = 0; o->dmg_pct = 0;
    o->crit = 5; o->crit_dmg = 150;
    o->atk_spd = 0; o->move_spd = 0; o->life_on_hit = 0; o->resist = 0;
    o->aspects = 0;

    for (int s = 0; s < SLOT_COUNT; s++) {
        const RogueItem *it = &equip[s];
        if (!rogue_item_is_equip(it)) continue;
        o->armor += it->armor;
        if (it->aspect) o->aspects |= (1u << it->aspect);
        for (int i = 0; i < it->n_affix; i++) {
            const Affix *a = &it->affix[i];
            switch (a->type) {
                case AFX_DMG:       o->flat_dmg    += a->val; break;
                case AFX_DMG_PCT:   o->dmg_pct     += a->val; break;
                case AFX_LIFE:      o->max_life    += a->val; break;
                case AFX_ARMOR:     o->armor       += a->val; break;
                case AFX_CRIT:      o->crit        += a->val; break;
                case AFX_CRITDMG:   o->crit_dmg    += a->val; break;
                case AFX_ATKSPD:    o->atk_spd     += a->val; break;
                case AFX_MOVESPD:   o->move_spd    += a->val; break;
                case AFX_LIFEONHIT: o->life_on_hit += a->val; break;
                case AFX_RESIST:    o->resist      += a->val; break;
                default: break;
            }
        }
        for (int g = 0; g < it->sockets && g < 2; g++)
            add_gem(o, (GemType)it->gem[g]);
    }
    if (o->resist > 75) o->resist = 75;
    if (o->atk_spd > 60) o->atk_spd = 60;
    if (o->crit > 75) o->crit = 75;
}

/* Diminishing-returns armor → damage reduction, capped at 80%. */
float rogue_stats_reduction(int armor) {
    if (armor <= 0) return 0.0f;
    float r = (float)armor / (float)(armor + 60);
    if (r > 0.8f) r = 0.8f;
    return r;
}
