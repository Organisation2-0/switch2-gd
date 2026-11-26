// SPDX-License-Identifier: GPL-3.0-or-later
// Generates src/objchild.c (child sprites of multi-part objects, nested ones flattened, in draw order)
// from OpenGD's object.json.   node genchild.js object.json objchild.c
const fs = require('fs');
const [src, dst] = process.argv.slice(2);
const j = JSON.parse(fs.readFileSync(src, 'utf8'));
const ids = Object.keys(j).map(Number).filter(Number.isInteger).sort((a, b) => a - b);
const kindOf = c => c === 'detail' ? 1 : c === 'black' ? 2 : 0;
const fmt = v => { const r = Math.round(v * 1000) / 1000; return Object.is(r, -0) ? 0 : r; };
let odd = 0, max = 0, parent = '';
const out = [];
for (const id of ids) {
  const parts = [];
  // list: children of a node; base(c) maps a child position into the object's bottom-left space
  const walk = (list, map, rot, sx, sy, depth, side) => {
    const sorted = list.map((c, i) => [c, i]).sort((a, b) => a[0].z - b[0].z || a[1] - b[1]).map(a => a[0]);
    for (const c of sorted) {
      if (c.anchor_x !== 0.5 || c.anchor_y !== 0.5) odd++;   // (reported below)
      const s = depth ? side : c.z < 0 ? -1 : 1;
      const cr = rot + c.rot, csx = sx * c.scale_x, csy = sy * c.scale_y;
      const rad = cr * Math.PI / 180, cs = Math.cos(rad), sn = Math.sin(rad);
      // the stored position is that of the anchor point; the table wants the centre
      const [px, py] = map(c.x, c.y);
      const ax = (0.5 - c.anchor_x) * c.content_x * csx, ay = (0.5 - c.anchor_y) * c.content_y * csy;
      const x = px + ax * cs + ay * sn, y = py - ax * sn + ay * cs;
      const inner = (gx, gy) => { const dx = (gx - c.content_x / 2) * csx, dy = (gy - c.content_y / 2) * csy;
        return [x + dx * cs + dy * sn, y - dx * sn + dy * cs]; };
      const self = { tex: c.texture_name, x, y, rot: cr, sx: csx, sy: csy, fx: c.flip_x ? 1 : 0, fy: c.flip_y ? 1 : 0,
                     kind: kindOf(c.color_channel), z: s, pw: 0, ph: 0 };
      // mirrored halves (saw blades): the positions assume this size for the parent
      if (!depth && c.texture_name === parent) { self.pw = c.content_x; self.ph = c.content_y; }
      const kids = c.children || [];
      walk(kids.filter(k => k.z < 0), inner, cr, csx, csy, depth + 1, s);
      parts.push(self);
      walk(kids.filter(k => k.z >= 0), inner, cr, csx, csy, depth + 1, s);
    }
  };
  parent = j[id].texture_name;
  walk(j[id].children || [], (x, y) => [x, y], 0, 1, 1, 0, 0);
  max = Math.max(max, parts.length);
  for (const p of parts)
    out.push(`    {${id}, "${p.tex}", ${fmt(p.x)}, ${fmt(p.y)}, ${fmt(p.rot)}, ${fmt(p.sx)}, ${fmt(p.sy)}, ${p.fx}, ${p.fy}, ${p.kind}, ${p.z}, ${fmt(p.pw)}, ${fmt(p.ph)}},`);
}
let c = '// SPDX-License-Identifier: GPL-3.0-or-later\n// Child sprites of multi-part objects, generated from OpenGD (GPL-3.0) object.json\n// (tools/genchild.js). Listed per object in draw order, nested children flattened.\n';
c += "// x,y: centre inside the parent (parent bottom-left origin, level units); kind: 0 base, 1 detail, 2 black;\n// z: -1 behind the main sprite, 1 in front of it; pw,ph: parent size the position refers to (0: the frame).\n";
c += '#include <stddef.h>\n#include "objchild.h"\nconst GdChild gd_children[] = {\n' + out.join('\n') + '\n};\n';
c += `const int gd_children_count = (int)(sizeof gd_children / sizeof gd_children[0]);

const GdChild *gd_children_find(int id, int *count)
{
    int lo = 0, hi = gd_children_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (gd_children[mid].id < id) lo = mid + 1; else hi = mid - 1;
    }
    int n = 0;
    while (lo + n < gd_children_count && gd_children[lo + n].id == id) n++;
    *count = n;
    return n ? &gd_children[lo] : NULL;
}
`;
fs.writeFileSync(dst, c);
console.log('parts', out.length, 'max per object', max, 'non-central anchors', odd);
