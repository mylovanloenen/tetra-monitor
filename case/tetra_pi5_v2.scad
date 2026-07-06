// ─────────────────────────────────────────────────────────────────────────────
//  TetraMonitor — RUIME staande case v2 (Pi 5 + scherm + buck; dongle EXTERN)
//
//  Scherm voorop met bezel, DC-jack onderaan, USB-slot in de zijwand (dongle
//  prikt daar in een Pi-USB-poort en steekt naar buiten — antenne zit dus op de
//  externe dongle). Buck-converter zit binnenin. Bewust RUIM zodat de rechte
//  USB-C-kabel van de buck naar de Pi past en alles fout-tolerant is.
//
//  Twee delen:  BODY (doos, open voorkant, buck + Pi-USB-slot + DC) +
//               BEZEL (voorpaneel met schermvenster; Pi hangt eraan).
//
//  ⚠️  v2, niet op hardware getest. Print eerst de BODY los om de pasvorm te
//      checken. Alles staat als variabele — meet & pas aan waar nodig.
//
//  OpenSCAD:  F5 preview, F6 render, F7 STL.  PETG, 0.2 mm, geen support.
// ─────────────────────────────────────────────────────────────────────────────
$fn = 40;

/* [Wat renderen] */
part = "both";        // "body" | "bezel" | "both"

/* [Algemeen — RUIM] */
wall    = 2.6;
tol     = 0.5;
margin  = 6;          // ruime rand rondom
screw_d = 2.6;

/* [Raspberry Pi 5] */
pi_l = 85;  pi_w = 56;
pi_hx = [3.5, 61.5];  pi_hy = [3.5, 52.5];
pilot = 2.3;  post_d = 6;

/* [Scherm — zichtbaar 75x52, rand 1.3 rondom + 6.5 rechts] */
scr_active = [75, 52];
brd_l = 1.3;  brd_r = 6.5;  brd_tb = 1.3;      // randen: links / rechts / boven-onder
scr_off_x = (brd_l - brd_r) / 2;               // venster t.o.v. midden (naar links)

/* [Buck-converter — 63x32x18, binnenin] */
bk_l = 63;  bk_w = 32;  bk_h = 18;

/* [Diepte] */
screen_gap = 15;       // bezel → Pi-board (scherm + GPIO-stapel; jouw 25 mm totaal)
depth      = 60;       // RUIME binnen-diepte: Pi + cooler + buck + rechte USB-C-kabel

/* [Connectoren] */
dc_d   = 11;           // 5.5x2.1 DC-jack (jouw 10.7 mm draad)
usb_w  = 40;  usb_h = 16;   // ruim USB-slot voor de dongle-stekker in de zijwand

/* [Opties] */
vents = true;

// ── Afgeleide maten ──────────────────────────────────────────────────────────
brd_w = brd_l + scr_active[0] + brd_r;         // ~82.8
brd_h = brd_tb*2 + scr_active[1];              // ~54.6
inner_w = max(pi_l, brd_w) + 6;                // ruim
inner_h = max(pi_w, brd_h) + 6;
box_w = inner_w + 2*margin + 2*wall;
box_h = inner_h + 2*margin + 2*wall;
box_d = depth + wall;

pi_x = (box_w - pi_l)/2;   pi_y = (box_h - pi_w)/2;
// buck: onderin-achterin (lage Y, tegen de achterwand)
bk_x = (box_w - bk_l)/2;   bk_y = wall + margin;
// schermvenster: gecentreerd, iets naar links (offset)
win_cx = box_w/2 + scr_off_x;   win_cy = box_h/2;
// hoekposten
cpost = [[wall+4, wall+4], [box_w-wall-4, wall+4],
         [wall+4, box_h-wall-4], [box_w-wall-4, box_h-wall-4]];

module rrect(x, y, h) { linear_extrude(h) offset(2) offset(-2) square([x, y]); }

// ── BODY ─────────────────────────────────────────────────────────────────────
module body() {
    difference() {
        union() {
            difference() {
                rrect(box_w, box_h, box_d);
                translate([wall, wall, wall]) rrect(box_w-2*wall, box_h-2*wall, box_d);
            }
            // hoekposten met schroefgat voor de bezel
            for (p = cpost) translate([p[0], p[1], wall])
                difference() {
                    cylinder(d = 7.5, h = box_d - wall);
                    translate([0,0,box_d-wall-6]) cylinder(d = pilot, h = 6.2);
                }
            // cradle-ribben voor de buck (achterin, onderin)
            translate([bk_x-1, bk_y-1.5, wall]) cube([bk_l+2, 1.4, bk_h+2]);
            translate([bk_x-1, bk_y+bk_w+0.3, wall]) cube([bk_l+2, 1.4, bk_h+2]);
        }
        // DC-jack in de ONDERWAND (y=0), bij de buck
        translate([box_w/2, wall+1, wall+bk_h/2]) rotate([90,0,0]) cylinder(d=dc_d, h=wall+2);
        // USB-slot in de RECHTER zijwand (x=box_w): dongle prikt hier in een Pi-poort
        translate([box_w-wall-1, box_h/2-usb_w/2, box_d-screen_gap-usb_h])
            cube([wall+2, usb_w, usb_h]);
        // ventilatie in de achterwand
        if (vents) for (gx=[box_w/2-24:12:box_w/2+24], gy=[box_h/2-18:12:box_h/2+18])
            translate([gx, gy, -0.1]) cylinder(d=5, h=wall+0.2);
    }
}

// ── BEZEL ────────────────────────────────────────────────────────────────────
module bezel() {
    difference() {
        union() {
            rrect(box_w, box_h, wall);
            for (hx=pi_hx, hy=pi_hy)
                translate([pi_x+hx, pi_y+hy, wall]) cylinder(d=post_d, h=screen_gap);
        }
        // schermvenster = zichtbaar gebied, iets naar links
        translate([win_cx - scr_active[0]/2, win_cy - scr_active[1]/2, -0.1])
            rrect(scr_active[0], scr_active[1], wall+0.2);
        // schroefgaten (verzonken) naar de body
        for (p = cpost) translate([p[0], p[1], -0.1]) {
            cylinder(d=screw_d, h=wall+0.2);
            cylinder(d1=5.2, d2=screw_d, h=1.6);
        }
        // pilotgaatjes in de Pi-standoffs
        for (hx=pi_hx, hy=pi_hy)
            translate([pi_x+hx, pi_y+hy, wall-0.1]) cylinder(d=pilot, h=screen_gap+0.2);
    }
}

// ── Render ───────────────────────────────────────────────────────────────────
if (part == "body") body();
else if (part == "bezel") bezel();
else { body(); translate([box_w + 12, 0, 0]) bezel(); }
