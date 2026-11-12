// SPDX-License-Identifier: MIT (adapted from CHOPPAH's, same author)
/* fm1-violioliolol's own step around an install (fm1.toml install_app; fm1-base web/installer.html APP): an expression, an
   object with after(midi, ui, state), which the installer runs once the new firmware is in. Ours: confetti over
   the page, in the firmware's colours, for about five seconds (with reduced motion asked for: a still scatter that
   fades). Nothing here talks to the FM-1. */
(() => {
  const COLOURS = ["#F0C878", "#DF765F", "#FFFFFF", "#8AC6AD", "#B7A6E8", "#FFF6E2"];
  function confetti() {
    const still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const c = document.createElement("canvas");
    c.setAttribute("aria-hidden", "true");
    c.style.cssText = "position:fixed;inset:0;width:100vw;height:100vh;pointer-events:none;z-index:2147483647";
    document.body.appendChild(c);
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const W = () => window.innerWidth, H = () => window.innerHeight;
    const size = () => { c.width = Math.round(W() * dpr); c.height = Math.round(H() * dpr); };
    size();
    window.addEventListener("resize", size);
    const g = c.getContext("2d");
    const rnd = (a, b) => a + Math.random() * (b - a);
    const bits = [];
    const burst = (x, y, n, dir) => {         /* n pieces from (x, y), thrown up and to one side */
      for (let i = 0; i < n; i++) {
        const a = dir + rnd(-0.5, 0.5), v = rnd(14, 28) * Math.min(1.4, Math.max(0.7, H() / 800));
        bits.push({ x, y, vx: Math.cos(a) * v, vy: Math.sin(a) * v, w: rnd(6, 11), h: rnd(3, 6),
                    r: rnd(0, Math.PI * 2), vr: rnd(-0.3, 0.3), wob: rnd(0, Math.PI * 2),
                    col: COLOURS[(Math.random() * COLOURS.length) | 0] });
      }
    };
    burst(0, H() * 0.75, 110, -Math.PI / 3);    /* from both bottom corners, up into the page */
    burst(W(), H() * 0.75, 110, -Math.PI * 2 / 3);
    for (let i = 0; i < 90; i++)               /* and a shower from above, across the page */
      burst(rnd(0, W()), rnd(-H() * 0.4, -10), 1, Math.PI / 2);
    if (still) {                               /* (no motion: where they would land, then a fade) */
      for (const b of bits) { b.x = rnd(0, W()); b.y = rnd(0, H()); }
    }
    const LIFE = still ? 2500 : 5000, t0 = performance.now();
    let last = t0;
    const frame = (now) => {
      const t = now - t0, dt = Math.min(2.5, (now - last) / 16.7);
      last = now;
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, W(), H());
      g.globalAlpha = t > LIFE - 800 ? Math.max(0, (LIFE - t) / 800) : 1;
      for (const b of bits) {
        if (!still) {
          b.vy += 0.25 * dt;                   /* gravity, air, a flutter; paper falls slowly */
          b.vx *= Math.pow(0.975, dt);
          b.vy *= Math.pow(0.975, dt);
          if (b.vy > 3.2) b.vy = 3.2;
          b.wob += 0.12 * dt;
          b.x += (b.vx + Math.sin(b.wob) * 0.8) * dt;
          b.y += b.vy * dt;
          b.r += b.vr * dt;
        }
        g.save();
        g.translate(b.x, b.y);
        g.rotate(b.r);
        g.scale(1, Math.cos(b.wob));           /* (turning over as it falls) */
        g.fillStyle = b.col;
        g.fillRect(-b.w / 2, -b.h / 2, b.w, b.h);
        g.restore();
      }
      if (t < LIFE) requestAnimationFrame(frame);
      else { window.removeEventListener("resize", size); c.remove(); }
    };
    requestAnimationFrame(frame);
  }
  return {
    after: async () => { confetti(); },     /* (the installer's own words say it is done) */
  };
})()
