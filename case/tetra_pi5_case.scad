// ─────────────────────────────────────────────────────────────────────────────
//  TetraMonitor — case voor Raspberry Pi 5 + RTL-SDR + buck-converter
//
//  Eén kastje met drie zones:
//    • Pi 5 (op standoffs, ruimte voor de Active Cooler)
//    • afgeschermd DONGLE-vak (gescheiden door een schot-sleuf voor een
//      metalen plaatje / koperfolie → houdt Pi-ruis bij de dongle weg)
//    • buck-converter-vak
//  Plus: DC-ingang in de zijwand, SMA-bulkhead voor de antenne, ventilatie,
//  hoekposten voor een deksel.
//
//  ⚠️  MATEN: de buck-converter, het scherm en de connector-gaten zijn op
//      gangbare maten gezet. METER JE EIGEN ONDERDELEN en pas de variabelen
//      hieronder aan vóór je print. Print eventueel eerst alleen de bodem om
//      de pasvorm te checken.
//
//  OpenSCAD:  F5 = preview, F6 = render, F7 = export STL.
//  Print: PETG (hittebestend), 0.2 mm, 3 perimeters, 20% infill, geen support.
// ─────────────────────────────────────────────────────────────────────────────
$fn = 40;

/* [Algemeen] */
wall    = 2.4;     // wanddikte
base_t  = 2.8;     // bodemdikte
tol     = 0.4;     // speling
margin  = 3.5;     // rand binnen de wanden
inner_h = 28;      // binnenhoogte — Pi + lage cooler + scherm erbovenop (CHECK
                   // je werkelijke stapelhoogte: Pi + cooler + scherm)

/* [Raspberry Pi 5] */
pi_l   = 85;  pi_w = 56;
pi_hx  = [3.5, 61.5];   pi_hy = [3.5, 52.5];   // gatraster 58 x 49
soh    = 5;            // standoff-hoogte
post_d = 6;  pilot = 2.3;

/* [RTL-SDR dongle] */
dl = 67;  dw = 27;  dh = 13;

/* [Buck-converter — 46(63) x 32 x 18 mm] */
bk_l = 63;  bk_w = 32;  bk_h = 18;   // 63 = inclusief schroefklemmen
bk_hx = [3, 60];  bk_hy = [3, 29];   // montagegaten buck (pas aan / bk_post=true)
bk_post = false;                     // true = standoffs voor de buck

/* [Connectoren] */
dc_d   = 8;      // paneelgat voor 5.5x2.1 DC-barrel-jack (schroefdraad ~8 mm —
                 // CHECK je jack; metalen paneeljacks zijn soms 11 mm)
dc_z   = 12;     // hoogte van DC-gat boven de bodem
sma_d  = 6.5;    // gat voor SMA-bulkhead (de antenne + 90°-adapter komt hierop)
sma_z  = 16;     // hoogte van SMA-gat boven de bodem (dongle ligt laag)

/* [Opties] */
gap   = 7;       // ruimte tussen zones
vents = true;
lid_posts = true;

// ── Afgeleide maten ──────────────────────────────────────────────────────────
inner_l = pi_l + gap + bk_l;                 // X-binnenmaat
inner_w = pi_w + gap + dw;                    // Y-binnenmaat
box_l = inner_l + 2*margin + 2*wall;
box_w = inner_w + 2*margin + 2*wall;
box_h = base_t + inner_h;

x0 = wall + margin;  y0 = wall + margin;      // binnenhoek
pi_x = x0;                 pi_y = y0;                       // Pi-zone
dn_x = x0;                 dn_y = y0 + pi_w + gap;          // dongle-zone (boven Pi)
bk_x = x0 + pi_l + gap;    bk_y = y0;                       // buck-zone (rechts)

// ── Bouwstenen ───────────────────────────────────────────────────────────────
module standoff(x, y, h) {
    translate([x, y, base_t]) difference() {
        cylinder(d = post_d, h = h);
        translate([0,0,-0.1]) cylinder(d = pilot, h = h + base_t + 0.2);
    }
}

module shell() {
    difference() {
        // buitenkant
        linear_extrude(box_h) offset(2) offset(-2) square([box_l, box_w]);
        // binnenuitholling
        translate([wall, wall, base_t])
            linear_extrude(box_h) offset(2) offset(-2) square([box_l-2*wall, box_w-2*wall]);
    }
}

module divider() {
    // schot tussen Pi en dongle, met een sleuf om een metalen schild/koperplaatje
    // in te schuiven (afscherming). Vol schot; sleuf is de gleuf bovenin.
    translate([dn_x - margin, dn_y - gap/2 - 0.6, base_t])
        cube([dl + 2*margin, 1.2, inner_h - 2]);
}

module vent_grid(x, y, lx, ly) {
    nx = max(1, floor(lx/10));  ny = max(1, floor(ly/10));
    for (i=[1:nx], j=[1:ny])
        translate([x + i*lx/(nx+1), y + j*ly/(ny+1), -0.1])
            cylinder(d = 5, h = base_t + 0.2);
}

module corner_posts() {
    for (cx=[wall+3, box_l-wall-3], cy=[wall+3, box_w-wall-3])
        translate([cx, cy, base_t]) difference() {
            cylinder(d = 7, h = inner_h);
            translate([0,0,inner_h-7]) cylinder(d = pilot, h = 7.2);
        }
}

// ── Samenstellen ─────────────────────────────────────────────────────────────
module tetra_pi5_case() {
    difference() {
        union() {
            shell();
            // Pi-standoffs
            for (hx=pi_hx, hy=pi_hy) standoff(pi_x + hx, pi_y + hy, soh);
            // buck-standoffs (optioneel)
            if (bk_post) for (hx=bk_hx, hy=bk_hy) standoff(bk_x + hx, bk_y + hy, soh);
            divider();
            if (lid_posts) corner_posts();
        }
        // DC-ingang in de rechter wand (bij de buck)
        translate([box_l - wall - 1, bk_y + bk_w/2, base_t + dc_z])
            rotate([0,90,0]) cylinder(d = dc_d, h = wall + 2);
        // SMA-bulkhead in de achter-wand (bij de dongle)
        translate([dn_x + dl/2, box_w - wall - 1, base_t + sma_z])
            rotate([-90,0,0]) cylinder(d = sma_d, h = wall + 2);
        // ventilatie onder Pi en dongle
        if (vents) {
            vent_grid(pi_x, pi_y, pi_l, pi_w);
            vent_grid(dn_x, dn_y, dl, dw);
        }
    }
}

tetra_pi5_case();

// ── Hint: scherm + deksel ────────────────────────────────────────────────────
// De bovenkant is open (deksel apart). Wil je een deksel met scherm-venster:
// geef me de schermmaat (actief gebied in mm) en het type, dan teken ik een
// deksel met uitsparing + schroefgaten op de hoekposten.
