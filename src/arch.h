/* CATHEDRA - architecture grammar. The CPU writes compact "parts" (frusta,
 * flying-buttress struts, pointed-arch frames, glass panels, tracery rings,
 * rocks); mesh_gen.comp expands them into dense triangle meshes on the GPU.
 * Part = 16 floats: A.xyz,type | B.xyz,material | c0..c3 | triOffset,e1,e2,0 */
enum { M_STONE, M_GOLD, M_GLASS, M_COPPER, M_SLATE, M_ROCK, M_METAL, M_TRIM };
#define PI_F 3.14159265f
static float *PRT; static unsigned npart, ntri, rs_;
static float rnd(void) { rs_ = rs_ * 1664525u + 1013904223u; return (rs_ >> 8) * (1.0f / 16777216.0f); }
static void part(int ty, int m, float ax, float ay, float az, float bx, float by, float bz, float c0, float c1, float c2, float c3, float e1, float e2) {
  static const unsigned short tc[6] = { 0, 80, 96, 17, 96, 320 };
  float *p = PRT + 16 * npart++;
  p[0] = ax; p[1] = ay; p[2] = az; p[3] = (float)ty; p[4] = bx; p[5] = by; p[6] = bz; p[7] = (float)m;
  p[8] = c0; p[9] = c1; p[10] = c2; p[11] = c3; p[12] = (float)ntri; p[13] = e1; p[14] = e2; p[15] = 0;
  ntri += ty ? tc[ty] : 4 * (unsigned)c2;
}
/* frustum along an arbitrary axis */
static void cone(int m, float ax, float ay, float az, float bx, float by, float bz, float r0, float r1, int n, float rot) {
  part(0, m, ax, ay, az, bx, by, bz, r0, r1, (float)n, rot, 1, 0);
}
/* vertical box at (x,z) from y0 to y1, facing angle th: half depth hd (along th), half width hw */
static void obox(int m, float x, float z, float y0, float y1, float th, float hd, float hw, float tap) {
  part(0, m, x, y0, z, x, y1, z, 1.41421f * hd, 1.41421f * hd * tap, 4, PI_F / 4, hw / hd, PI_F - th);
}
/* horizontal bar between two points (square section) */
static void bar(int m, float ax, float ay, float az, float bx, float by, float bz, float r) { cone(m, ax, ay, az, bx, by, bz, r, r, 4, PI_F / 4); }
/* pinnacle: shaft, crocketed pyramid, gold finial (base centre x,y,z; shaft half width s) */
static void pinnacle(float x, float y, float z, float s, int crk) {
  obox(M_TRIM, x, z, y, y + s * 3.2f, 0, s, s, 1);
  float y1 = y + s * 3.2f, h = s * 6.f;
  cone(M_TRIM, x, y1 - s * .3f, z, x, y1, z, s * 1.75f, s * 1.75f, 4, PI_F / 4);
  cone(M_STONE, x, y1, z, x, y1 + h, z, s * 1.5f, 0, 4, PI_F / 4);
  if (crk) for (int e = 0; e < 4; e++) for (int k = 1; k <= 3; k++) {
    float f = k * .24f, a = (e + .5f) * PI_F / 2, cx = cosf(a), cz = sinf(a), rr = s * 1.5f * 1.41421f * (1 - f);
    float px = x + cx * rr, py = y1 + h * f, pz = z + cz * rr;
    cone(M_STONE, px, py, pz, px + cx * s * .42f, py + s * .5f, pz + cz * s * .42f, s * .4f * (1 - f * .5f), 0, 4, 0);   /* crockets hug the edge */
  }
  cone(M_GOLD, x, y1 + h - s * .2f, z, x, y1 + h + s * 1.1f, z, s * .32f, 0, 6, 0);
}
/* pointed window on a wall: base centre (x,y,z), outward normal angle th */
static void window_(float x, float y, float z, float th, float ww, float wh, int style) {
  float nx = cosf(th), nz = sinf(th), tx = -nz, tz = nx;
  part(3, M_GLASS, x + nx * .012f, y, z + nz * .012f, x + nx, y, z + nz, ww, wh, 0, 0, 0, 1.6f);
  part(2, M_TRIM, x, y, z, x + nx, y, z + nz, ww * 1.12f, wh, ww * .26f, 0, ww * .55f, 1.6f);
  if (style & 1) {  /* mullion, transom and an oculus in the head */
    obox(M_TRIM, x + nx * .02f, z + nz * .02f, y, y + wh + ww * .2f, th, .022f, ww * .07f, 1);
    bar(M_TRIM, x - tx * ww + nx * .02f, y + wh * .62f, z - tz * ww + nz * .02f, x + tx * ww + nx * .02f, y + wh * .62f, z + tz * ww + nz * .02f, ww * .05f);
    part(4, M_TRIM, x + nx * .01f, y + wh + ww * .62f, z + nz * .01f, x + nx, y + wh + ww * .62f, z + nz, ww * .36f, ww * .08f, 0, 0, ww * .3f, 0);
  }
  if (style & 2) {  /* gable (wimperg) with crockets and finial */
    float gy = y + wh + ww * .9f, gh = ww * 2.6f;
    cone(M_TRIM, x, gy + gh * .32f, z, x + nx * ww * .3f, gy + gh * .32f, z + nz * ww * .3f, gh * .66f, gh * .66f, 3, PI_F / 2);
    for (int s = -1; s <= 1; s += 2) for (int k = 1; k <= 3; k++) {
      float f = k * .25f, px = x + tx * s * gh * .57f * (1 - f) + nx * ww * .3f, py = gy + gh * (f - .01f), pz = z + tz * s * gh * .57f * (1 - f) + nz * ww * .3f;
      cone(M_STONE, px, py, pz, px + tx * s * ww * .08f, py + ww * .16f, pz + tz * s * ww * .08f, ww * .08f, ww * .03f, 4, 0);
    }
    cone(M_GOLD, x + nx * ww * .2f, gy + gh * .98f, z + nz * ww * .2f, x + nx * ww * .2f, gy + gh * 1.25f, z + nz * ww * .2f, ww * .1f, 0, 6, 0);
  }
}
/* one gothic tower in units of its base radius; hb = height of the body, sp = spire style */
static void tower_(float hb, unsigned seed, int fly) {   /* fly: 1 flying buttresses, 2 ruined spire */
  rs_ = seed * 2654435761u + 7;
  int wst = (int)(rnd() * 4), sst = (int)(rnd() * 3), crk = rnd() < .8f, garg = rnd() < .7f, bal = rnd() < .6f;
  float yb = 0, step = .025f + rnd() * .05f;
  cone(M_STONE, 0, -5, 0, 0, 0, 0, 1.12f, 1.1f, 8, PI_F / 8);
  int tier = 0; float rlast = 1;
  while (yb < hb - .6f) {
    float th = tier == 0 ? 2.6f + rnd() * 1.4f : 1.9f + rnd() * 1.2f;
    if (yb + th > hb - .6f) th = hb - yb;
    float r = 1.f - .32f * yb / hb - step * tier, rt = r * .985f, yt = yb + th, rc = 1.0824f;
    cone(M_STONE, 0, yb, 0, 0, yt, 0, r * rc, rt * rc, 8, PI_F / 8);
    cone(M_TRIM, 0, yb - .02f, 0, 0, yb + .07f, 0, r * rc * 1.035f, r * rc * 1.035f, 8, PI_F / 8);
    cone(M_TRIM, 0, yt - .13f, 0, 0, yt, 0, rt * rc * 1.01f, rt * rc * 1.085f, 8, PI_F / 8);
    int top = yt > hb - .7f, ws = (wst + tier) & 3;
    for (int f = 0; f < 8; f++) {
      float a = f * PI_F / 4, ca = cosf(a), sa = sinf(a), ap = (r + rt) * .5f;
      float wh = th * (ws & 1 ? .5f : .42f), ww = (ws & 1 ? .2f : .13f) * r, sy = yb + th * .14f;
      if (ws & 1) window_(ca * ap, sy, sa * ap, a, ww, wh, (top || tier == 0) ? 3 : 1);
      else for (int k = -1; k <= 1; k += 2) {
        float o = k * ww * 1.35f;
        window_(ca * ap - sa * o, sy, sa * ap + ca * o, a, ww, wh, top && k < 0 ? 2 : 0);
        if (k > 0 && ws == 2) part(4, M_TRIM, ca * ap + ca * .01f, sy + wh + ww * 2.3f, sa * ap + sa * .01f, ca * 2, sy + wh + ww * 2.3f, sa * 2, ww * .7f, ww * .14f, 0, 0, ww * .4f, 0);
      }
      /* corner buttress with set-offs and a pinnacle */
      float b = a + PI_F / 8, cb = cosf(b), sb = sinf(b), R0 = r * rc;
      float d0 = .2f + .14f * (1 - yb / hb);
      obox(M_STONE, cb * (R0 + d0 * .5f - .04f), sb * (R0 + d0 * .5f - .04f), yb, yb + th * .55f, b, d0 * .5f, .075f, 1);
      obox(M_STONE, cb * (R0 + d0 * .3f - .04f), sb * (R0 + d0 * .3f - .04f), yb + th * .55f, yt - .1f, b, d0 * .3f, .065f, 1);
      cone(M_TRIM, cb * (R0 + d0 - .04f), yb + th * .55f, sb * (R0 + d0 - .04f), cb * (R0 + d0 * .55f), yb + th * .55f + d0 * .6f, sb * (R0 + d0 * .55f), .07f, .05f, 4, PI_F / 4);
      pinnacle(cb * (R0 + .02f), yt - .02f, sb * (R0 + .02f), .055f + .02f * (1 - yb / hb), crk);
      if (garg && (tier & 1) == 0) {
        float gx = cb * (R0 + .1f), gz = sb * (R0 + .1f), gy = yt - .2f;
        cone(M_STONE, gx, gy, gz, gx + cb * .38f, gy - .06f, gz + sb * .38f, .065f, .03f, 5, 0);
        cone(M_STONE, gx + cb * .36f, gy - .05f, gz + sb * .36f, gx + cb * .46f, gy - .1f, gz + sb * .46f, .045f, 0, 4, 0);
      }
      if (bal && top) {
        float a1 = a - PI_F / 8, R1 = rt * rc * 1.06f;
        float x0 = cosf(a1) * R1, z0 = sinf(a1) * R1, x1 = cb * R1, z1 = sb * R1;
        bar(M_TRIM, x0, yt + .2f, z0, x1, yt + .2f, z1, .025f);
        for (int k = 1; k < 6; k++) { float u = k / 6.f; obox(M_TRIM, x0 + (x1 - x0) * u, z0 + (z1 - z0) * u, yt, yt + .2f, a, .018f, .018f, .7f); }
      }
    }
    yb = yt; tier++; rlast = rt;
  }
  /* flying buttresses to four free-standing piers */
  if (fly & 1) for (int k = 0; k < 4; k++) {
    float a = PI_F / 8 + k * PI_F / 2 + PI_F / 4, ca = cosf(a), sa = sinf(a), D = 1.68f, yp = 2.6f + hb * .1f;
    obox(M_STONE, ca * D, sa * D, -5, yp, a, .16f, .16f, .9f);
    pinnacle(ca * D, yp, sa * D, .09f, 1);
    for (int j = 0; j < 2; j++) {
      float y0 = yp - .35f - j * 1.1f, y1 = y0 + .9f;
      part(1, M_STONE, ca * (D - .14f), y0, sa * (D - .14f), ca * 1.2f, y1, sa * 1.2f, .06f, .09f, .32f, 0, 0, 0);
    }
  }
  /* the spire */
  float y0 = hb, SH = 4.4f + rnd() * 4.f, R = rlast * (.5f + .35f * rnd());   /* needles to stout spires */
  cone(M_STONE, 0, y0, 0, 0, y0 + .55f, 0, R * 1.0824f, R * 1.0824f, 8, PI_F / 8);
  for (int f = 0; f < 8; f++) { float a = f * PI_F / 4; window_(cosf(a) * R, y0 + .06f, sinf(a) * R, a, R * .16f, .3f, 0); }
  y0 += .55f;
  cone(M_TRIM, 0, y0 - .05f, 0, 0, y0 + .05f, 0, R * 1.15f, R * 1.15f, 8, PI_F / 8);
  int sm = sst == 1 ? M_STONE : sst == 2 ? M_SLATE : M_COPPER;
  if (sst == 2) {   /* open lantern stage before a slender spire */
    for (int f = 0; f < 8; f++) { float a = f * PI_F / 4 + PI_F / 8, rr = R * 1.0824f; obox(M_TRIM, cosf(a) * rr, sinf(a) * rr, y0, y0 + 1.1f, a, .06f, .06f, 1); }
    cone(M_STONE, 0, y0, 0, 0, y0 + 1.1f, 0, R * .8f, R * .8f, 8, PI_F / 8);
    for (int f = 0; f < 8; f++) { float a = f * PI_F / 4; window_(cosf(a) * R * .8f, y0 + .1f, sinf(a) * R * .8f, a, R * .22f, .6f, 0); }
    cone(M_TRIM, 0, y0 + 1.1f, 0, 0, y0 + 1.22f, 0, R * 1.18f, R * 1.18f, 8, PI_F / 8);
    y0 += 1.22f; R *= .85f; SH -= 1.2f;
  }
  if (fly & 2) {   /* ruin: the spire broken off, jagged stumps around the break */
    float f = .25f + .3f * rnd(), r = R * 1.0824f * (1 - f);
    cone(sm, 0, y0, 0, .08f, y0 + SH * f, 0, R * 1.0824f, r, 8, PI_F / 8);
    for (int k = 0; k < 6; k++) { float a = rnd() * 6.2832f, cx = cosf(a) * r * .75f, cz = sinf(a) * r * .75f;
      cone(sm, cx, y0 + SH * f - .3f, cz, cx * 1.15f, y0 + SH * f + .2f + rnd() * .8f, cz * 1.15f, .1f + .1f * rnd(), 0, 3, rnd() * 6); }
    return;
  }
  cone(sm, 0, y0, 0, 0, y0 + SH, 0, R * 1.0824f, 0, 8, PI_F / 8);
  for (int b = 0; b < 2; b++) { float f = .3f + b * .3f; cone(M_GOLD, 0, y0 + SH * f, 0, 0, y0 + SH * f + .05f, 0, R * 1.0824f * (1 - f) + .03f, R * 1.0824f * (1 - f) + .03f, 8, PI_F / 8); }
  for (int e = 0; e < 8; e++) {
    float a = e * PI_F / 4 + PI_F / 8, ca = cosf(a), sa = sinf(a), rr = R * 1.0824f;
    for (int k = 1; crk && k < 16; k++) {
      float f = k / 16.f, px = ca * rr * (1 - f), py = y0 + SH * f, pz = sa * rr * (1 - f);
      cone(sm == M_COPPER ? M_GOLD : M_STONE, px, py, pz, px + ca * .05f, py + .07f, pz + sa * .05f, .04f * (1 - f * .5f), .015f, 4, 0);   /* crockets: blunt knobs up the edge */
    }
    if (sst == 1 && (e & 1)) { float px = ca * rr * 1.02f, pz = sa * rr * 1.02f; pinnacle(px, y0, pz, .07f, 1); }
  }
  for (int f = 0; f < 4; f++) {   /* lucarnes */
    float a = f * PI_F / 2, ca = cosf(a), sa = sinf(a), ap = R * .82f;
    window_(ca * ap, y0 + SH * .14f, sa * ap, a, R * .13f, .22f, 2);
  }
  y0 += SH;
  cone(M_GOLD, 0, y0 - .3f, 0, 0, y0 + .12f, 0, .05f, .14f, 8, 0);
  cone(M_GOLD, 0, y0 + .12f, 0, 0, y0 + .32f, 0, .14f, 0, 8, 0);
  bar(M_GOLD, 0, y0 + .3f, 0, 0, y0 + 1.f, 0, .02f);
  bar(M_GOLD, -.22f, y0 + .78f, 0, .22f, y0 + .78f, 0, .018f);
}
/* mining scaffolds around the rim of the planet's rift (planet frame, star units) */
static void scaffold_(void) {
  float ax[3] = { .1f, .55f, .83f }, l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
  for (int i = 0; i < 3; i++) ax[i] /= l;
  float u[3] = { ax[2], 0, -ax[0] }; l = sqrtf(u[0] * u[0] + u[2] * u[2]); u[0] /= l; u[2] /= l;
  float v[3] = { ax[1] * u[2], ax[2] * u[0] - ax[0] * u[2], -ax[1] * u[0] };
  float top[24][3];
  for (int k = 0; k < 24; k++) {
    float ph = k * PI_F / 12, th = .8f + .05f * sinf(k * 2.7f), d[3], b[3], t[3], c = cosf(th), s = sinf(th);
    for (int i = 0; i < 3; i++) d[i] = ax[i] * c + (u[i] * cosf(ph) + v[i] * sinf(ph)) * s;
    float H = .05f + .03f * (k % 3 == 0);
    for (int i = 0; i < 3; i++) { b[i] = d[i] * .69f; t[i] = d[i] * (.71f + H); top[k][i] = d[i] * (.71f + H * .7f); }
    cone(M_METAL, b[0], b[1], b[2], t[0], t[1], t[2], .006f, .004f, 4, 0);
    for (int j = 1; j < 4; j++) { float f = .7f + H * j / 4; cone(M_METAL, d[0] * f, d[1] * f, d[2] * f, d[0] * (f + .003f), d[1] * (f + .003f), d[2] * (f + .003f), .011f, .011f, 4, 0); }
    if (k & 1) {   /* crane boom over the pit and its cable */
      float e[3]; for (int i = 0; i < 3; i++) e[i] = t[i] + (ax[i] - d[i] * c) * .13f;
      bar(M_METAL, t[0], t[1], t[2], e[0], e[1], e[2], .0025f);
      bar(M_METAL, e[0], e[1], e[2], e[0] - d[0] * .09f, e[1] - d[1] * .09f, e[2] - d[2] * .09f, .0008f);
    }
  }
  for (int k = 0; k < 24; k++) { float *a = top[k], *b = top[(k + 1) % 24]; bar(M_METAL, a[0], a[1], a[2], b[0], b[1], b[2], .0025f); }
}
