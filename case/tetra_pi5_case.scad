// ─────────────────────────────────────────────────────────────────────────────
//  TetraMonitor — compacte GESTAPELDE case voor Pi 5 + RTL-SDR + buck
//
//  Voorkant blijft ~schermformaat; het groeit in de DIEPTE, niet in de breedte.
//  Twee delen die op elkaar schroeven:
//     • ONDERBAK  : dongle + buck naast elkaar op de bodem (afgeschermd vak)
//     • BOVENPLAAT: Pi 5 op standoffs, lage cooler + 3.5"-scherm erbovenop
//
//  Kies wat je rendert/print met de variabele  part  hieronder.
//
//  ⚠️  Dit is een v1 die ik niet op echte hardware kon testen. Het is een krappe
//      stapeling — print eerst de ONDERBAK los om de pasvorm van dongle+buck en
//      de schroefposten te checken vóór je alles print. Maten = variabelen.
//
//  OpenSCAD: F5 preview, F6 render, F7 export STL. PETG, 0.2 mm, geen support.
// ─────────────────────────────────────────────────────────────────────────────
$fn = 40;

/* [Wat renderen] */
part = "both";       // "bottom" | "top" | "both" (both = naast elkaar, alleen preview)

/* [Algemeen] */
wall    = 2.4;
base_t  = 2.8;
tol     = 0.4;
margin  = 3;
screw_d = 2.6;       // M2.5-schroeven tussen de twee delen

/* [Raspberry Pi 5] */
pi_l = 85;  pi_w = 56;
pi_hx = [3.5, 61.5];  pi_hy = [3.5, 52.5];
pi_soh = 4;  post_d = 6;  pilot = 2.3;

/* [Dongle + buck — naast elkaar op de bodem] */
dl = 67;  dw = 27;  dh = 13;
bk_l = 63;  bk_w = 32;  bk_h = 18;

/* [3.5" scherm (standaard)] */
scr_active = [73, 49];    // zichtbaar gebied (venster in een eventueel bezeltje)

/* [Hoogtes] */
bottom_h = bk_h + 4;      // onderbak: hoog genoeg voor de buck (18) + lucht
plate_t  = 3;             // dikte bovenplaat

/* [Connectoren] */
dc_d = 8;    dc_z = 9;    // 5.5x2.1 barrel-jack paneelgat (check je jack: 8 of 11)
sma_d = 6.5; sma_z = 9;   // SMA-bulkhead (antenne + 90°-adapter)

/* [Opties] */
vents = true;

// ── Afgeleide maten ──────────────────────────────────────────────────────────
inner_l = max(pi_l, dl, bk_l);          // 85
inner_w = max(pi_w, dw + bk_w);         // 59 (dongle 27 + buck 32)
box_l = inner_l + 2*margin + 2*wall;
box_w = inner_w + 2*margin + 2*wall;

// hoekposten (binnen de wanden)
cpost = [[wall+3, wall+3], [box_l-wall-3, wall+3],
         [wall+3, box_w-wall-3], [box_l-wall-3, box_w-wall-3]];

// Pi gecentreerd op de bovenplaat
pi_x = (box_l - pi_l)/2;
pi_y = (box_w - pi_w)/2;
// dongle + buck naast elkaar op de bodem
dn_x = wall + margin;            dn_y = wall + margin;
bk_x = wall + margin;            bk_y = dn_y + dw + 2;

module rrect(l, w, h) { linear_extrude(h) offset(2) offset(-2) square([l, w]); }

module vent_grid(x, y, lx, ly) {
    nx = max(1, floor(lx/12));  ny = max(1, floor(ly/12));
    for (i=[1:nx], j=[1:ny])
        translate([x + i*lx/(nx+1), y + j*ly/(ny+1), -0.1])
            cylinder(d = 5, h = base_t + 0.2);
}

// ── Onderbak: dongle + buck ──────────────────────────────────────────────────
module bottom() {
    difference() {
        union() {
            // schaal met wanden
            difference() {
                rrect(box_l, box_w, base_t + bottom_h);
                translate([wall, wall, base_t]) rrect(box_l-2*wall, box_w-2*wall, bottom_h+1);
            }
            // hoekposten met schroefgat
            for (p = cpost) translate([p[0], p[1], base_t])
                difference() {
                    cylinder(d = 7, h = bottom_h);
                    translate([0,0,bottom_h-6]) cylinder(d = pilot, h = 6.2);
                }
            // lichte geleiders voor dongle en buck (klemt ze op hun plek)
            translate([dn_x-1, dn_y+dw+0.5, base_t]) cube([dl+2, 1.5, 5]);
        }
        // DC-ingang (rechterwand, bij de buck)
        translate([box_l-wall-1, bk_y+bk_w/2, base_t+dc_z])
            rotate([0,90,0]) cylinder(d=dc_d, h=wall+2);
        // SMA-bulkhead (achterwand, bij de dongle)
        translate([dn_x+dl/2, box_w-wall-1, base_t+sma_z])
            rotate([-90,0,0]) cylinder(d=sma_d, h=wall+2);
        if (vents) { vent_grid(dn_x, dn_y, dl, dw); vent_grid(bk_x, bk_y, bk_l, bk_w); }
    }
}

// ── Bovenplaat: Pi + scherm ──────────────────────────────────────────────────
module top() {
    difference() {
        union() {
            rrect(box_l, box_w, plate_t);
            // Pi-standoffs bovenop
            for (hx=pi_hx, hy=pi_hy) translate([pi_x+hx, pi_y+hy, plate_t])
                difference() {
                    cylinder(d=post_d, h=pi_soh);
                    translate([0,0,-0.1]) cylinder(d=pilot, h=pi_soh+plate_t+0.2);
                }
        }
        // schroefgaten naar de onderbak (verzonken)
        for (p = cpost) translate([p[0], p[1], -0.1]) {
            cylinder(d=screw_d, h=plate_t+0.2);
            cylinder(d1=5.2, d2=screw_d, h=1.6);
        }
        // luchtgaten rond de Pi (cooler-airflow)
        if (vents) for (gx=[8,box_l-8], gy=[box_w/2-12:8:box_w/2+12])
            translate([gx, gy, -0.1]) cylinder(d=4, h=plate_t+0.2);
    }
}

// ── Render ───────────────────────────────────────────────────────────────────
if (part == "bottom") bottom();
else if (part == "top") translate([0,0,base_t+bottom_h+plate_t]) rotate([180,0,0]) top();
else { bottom(); translate([box_l + 12, 0, 0]) top(); }   // both = naast elkaar
