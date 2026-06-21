// ─────────────────────────────────────────────────────────────────────────────
//  TetraMonitor — STAANDE case voor Pi 5 (scherm voorop, antenne boven, DC onder)
//
//  Je kijkt naar het scherm; de antenne komt BOVEN het scherm uit, de DC-jack
//  zit ONDERAAN. Twee delen die op elkaar schroeven:
//     • BODY  : doos met dongle + buck achterin; antenne-gat in de bovenwand,
//               DC-gat in de onderwand. Voorkant open.
//     • BEZEL : voorpaneel met schermvenster (zichtbaar gebied) + 2 mm lip.
//               De Pi 5 hangt met standoffs aan de achterkant van de bezel,
//               met het scherm naar voren door het venster.
//
//  Assemblage (voor→achter): bezel → scherm → Pi → cooler → schildplaat → dongle+buck.
//
//  Kies wat je rendert/print met  part  hieronder.
//
//  ⚠️  v1, niet op hardware getest. Krappe stapeling. Print eerst de BODY los om
//      de pasvorm (dongle, buck, dieptes, gaten) te checken. Maten = variabelen.
//
//  OpenSCAD: F5 preview, F6 render, F7 STL. PETG, 0.2 mm, geen support.
// ─────────────────────────────────────────────────────────────────────────────
$fn = 40;

/* [Wat renderen] */
part = "both";        // "body" | "bezel" | "both" (both = naast elkaar, preview)

/* [Algemeen] */
wall    = 2.4;
tol     = 0.4;
margin  = 3;
screw_d = 2.6;        // M2.5 tussen body en bezel

/* [Raspberry Pi 5] */
pi_l = 85;  pi_w = 56;                 // breedte x hoogte (voor het scherm)
pi_hx = [3.5, 61.5];  pi_hy = [3.5, 52.5];
pilot = 2.3;  post_d = 6;

/* [Dongle + buck — achterin] */
dl = 67;  dw = 27;  dh = 13;
bk_l = 63;  bk_w = 32;  bk_h = 18;

/* [3.5" scherm] */
scr_active = [73, 49];   // ZICHTBAAR gebied (= het venster)
lip = 2;                 // rand die de bezel over de schermrand legt (jouw 2 mm)

/* [Diepte] */
depth   = 44;            // binnen-diepte van de body
pi_gap  = 14;            // afstand bezel → Pi-board (scherm + GPIO-stapel)
pi_soh  = 4;             // standoff-lengte op de Pi-gaten

/* [Connectoren] */
dc_d  = 8;               // 5.5x2.1 paneelgat (check je jack: 8 of 11)
sma_d = 6.5;             // SMA-bulkhead (90°-adapter uit de dongle)

/* [Opties] */
vents = true;

// ── Afgeleide maten ──────────────────────────────────────────────────────────
inner_w = max(pi_l, dl, bk_l);          // X-breedte = 85
inner_h = max(pi_w, dw + bk_w);         // Y-hoogte = 59 (dongle 27 boven + buck 32 onder)
box_w = inner_w + 2*margin + 2*wall;
box_h = inner_h + 2*margin + 2*wall;
box_d = depth + wall;

// Pi gecentreerd op de voorkant
pi_x = (box_w - pi_l)/2;   pi_y = (box_h - pi_w)/2;
// dongle BOVENIN (hoge Y), buck ONDERIN (lage Y), beide achterin (lage Z)
dn_x = (box_w - dl)/2;     dn_y = box_h - wall - margin - dw;
bk_x = (box_w - bk_l)/2;   bk_y = wall + margin;
// antenne uit de bovenwand, recht boven het SMA-eind van de dongle
ant_x = dn_x + dl - 6;     ant_z = wall + dh/2;
// DC uit de onderwand, bij de buck
dc_x  = box_w/2;           dc_z  = wall + bk_h/2;
// hoekposten (in Z, voor de bezel)
cpost = [[wall+3, wall+3], [box_w-wall-3, wall+3],
         [wall+3, box_h-wall-3], [box_w-wall-3, box_h-wall-3]];

module rrect(x, y, h) { linear_extrude(h) offset(2) offset(-2) square([x, y]); }

// ── BODY: doos met dongle+buck, open voorkant ────────────────────────────────
module body() {
    difference() {
        union() {
            // schaal: bodem = achterwand (z=0), wanden rondom, voorkant open
            difference() {
                rrect(box_w, box_h, box_d);
                translate([wall, wall, wall]) rrect(box_w-2*wall, box_h-2*wall, box_d);
            }
            // hoekposten met schroefgat voor de bezel (aan de voorkant)
            for (p = cpost) translate([p[0], p[1], wall])
                difference() {
                    cylinder(d = 7, h = box_d - wall);
                    translate([0,0,box_d-wall-6]) cylinder(d = pilot, h = 6.2);
                }
            // cradle-ribben: houden dongle (boven) en buck (onder) achterin
            translate([dn_x-1, dn_y-1.5, wall]) cube([dl+2, 1.2, dh+2]);
            translate([bk_x-1, bk_y+bk_w+0.3, wall]) cube([bk_l+2, 1.2, bk_h+2]);
        }
        // antenne-gat in de BOVENWAND (y = box_h), 90°-adapter omhoog
        translate([ant_x, box_h-wall-1, ant_z]) rotate([-90,0,0]) cylinder(d=sma_d, h=wall+2);
        // DC-gat in de ONDERWAND (y = 0)
        translate([dc_x, wall+1, dc_z]) rotate([90,0,0]) cylinder(d=dc_d, h=wall+2);
        // ventilatie in de achterwand
        if (vents) for (gx=[box_w/2-20:10:box_w/2+20], gy=[box_h/2-15:10:box_h/2+15])
            translate([gx, gy, -0.1]) cylinder(d=5, h=wall+0.2);
    }
}

// ── BEZEL: voorpaneel met schermvenster + Pi-standoffs aan de achterkant ──────
module bezel() {
    difference() {
        union() {
            rrect(box_w, box_h, wall);
            // Pi-standoffs hangen naar achter (de Pi + scherm komen hierachter)
            for (hx=pi_hx, hy=pi_hy)
                translate([pi_x+hx, pi_y+hy, wall])
                    cylinder(d=post_d, h=pi_gap);
        }
        // schermvenster = zichtbaar gebied, gecentreerd (bezel legt 'lip' over de rand)
        translate([(box_w-scr_active[0])/2, (box_h-scr_active[1])/2, -0.1])
            rrect(scr_active[0], scr_active[1], wall+0.2);
        // schroefgaten naar de body (verzonken)
        for (p = cpost) translate([p[0], p[1], -0.1]) {
            cylinder(d=screw_d, h=wall+0.2);
            cylinder(d1=5.2, d2=screw_d, h=1.6);
        }
        // pilotgaatjes in de Pi-standoffs
        for (hx=pi_hx, hy=pi_hy)
            translate([pi_x+hx, pi_y+hy, wall-0.1]) cylinder(d=pilot, h=pi_gap+0.2);
    }
}

// ── Render ───────────────────────────────────────────────────────────────────
if (part == "body") body();
else if (part == "bezel") bezel();
else { body(); translate([box_w + 12, 0, 0]) bezel(); }
