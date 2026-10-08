// Draws the ring on a canvas: a dim room, the device's shell, a frosted ring, and each pixel as a soft circle of
// light. Shared by ring-preview.html and virtual-pager.html. Only a picture: nothing here affects the firmware.
//   canvas    a square canvas
//   frame     12 pixels of [red, green, blue, white], 0 to 255, from ringRender()
//   shellRgb  the device's color, 0xRRGGBB
//   diffuse   0 = bare LEDs seen as small dots, 1 = wide soft circles that merge
// Returns how many pixels are lit.
let ringGlowCanvas = null;
function ringDraw(canvas, frame, shellRgb, diffuse) {
  const g = canvas.getContext("2d");
  const S = canvas.width, c = S / 2, R = S * 0.33, body = S * 0.47;
  if (!ringGlowCanvas || ringGlowCanvas.width !== S) { ringGlowCanvas = document.createElement("canvas"); ringGlowCanvas.width = ringGlowCanvas.height = S; }
  g.globalCompositeOperation = "source-over";
  g.clearRect(0, 0, S, S);
  const shell = [(shellRgb >> 16) & 255, (shellRgb >> 8) & 255, shellRgb & 255].map((v) => Math.round((v * 0.35 + 255 * 0.65) * 0.42));
  g.fillStyle = `rgb(${shell})`; g.beginPath(); g.arc(c, c, body, 0, 7); g.fill();
  g.strokeStyle = "rgba(255,255,255,0.07)"; g.lineWidth = S * 0.085; g.beginPath(); g.arc(c, c, R, 0, 7); g.stroke();   // the frosted ring, unlit
  g.fillStyle = `rgb(${shell.map((v) => Math.round(v * 1.18))})`; g.beginPath(); g.arc(c, c, S * 0.2, 0, 7); g.fill();   // the send button

  // A stronger diffuser makes each circle wider and softer, until neighbors merge; the same light over a bigger
  // circle is dimmer at its center. The glow is kept mostly inside the frosted ring, with a little spill around it.
  const rad = S * (0.045 + 0.19 * diffuse), spread = Math.pow(rad / (S * 0.045), 0.9);
  let lit = 0;
  const gg = ringGlowCanvas.getContext("2d");
  gg.globalCompositeOperation = "source-over";
  gg.clearRect(0, 0, S, S);
  gg.globalCompositeOperation = "lighter";
  for (let i = 0; i < frame.length; i++) {
    const [r, gr, b, w] = frame[i].map((v) => v / 255);                    // linear light from each LED
    const lin = [r + w, gr + w * 0.93, b + w * 0.8];                       // the white LED is a warm white
    const top = Math.max(lin[0], lin[1], lin[2]);
    if (top < 0.0005) continue;
    lit++;
    const a = Math.pow(Math.min(top * 2.4 / spread, 1), 1 / 2.2);          // back to what the eye sees
    const col = lin.map((v) => Math.round(255 * Math.pow(Math.min(v / top, 1), 1 / 2.2)));
    const ang = (i / frame.length) * 2 * Math.PI - Math.PI / 2;
    const x = c + R * Math.cos(ang), y = c + R * Math.sin(ang);
    const glow = gg.createRadialGradient(x, y, 0, x, y, rad);
    glow.addColorStop(0, `rgba(${col},${a})`);
    glow.addColorStop(0.45, `rgba(${col},${a * 0.5})`);
    glow.addColorStop(1, `rgba(${col},0)`);
    gg.fillStyle = glow; gg.beginPath(); gg.arc(x, y, rad, 0, 7); gg.fill();
  }
  if (!lit) return 0;
  g.globalCompositeOperation = "lighter";
  g.globalAlpha = 0.22; g.drawImage(ringGlowCanvas, 0, 0); g.globalAlpha = 1;   // the spill onto the shell
  const band = S * (0.05 + 0.02 * diffuse);                                    // then keep only what the frosted ring shows
  const mask = gg.createRadialGradient(c, c, R - band * 1.5, c, c, R + band * 1.5);
  mask.addColorStop(0, "rgba(0,0,0,0)"); mask.addColorStop(0.3, "rgba(0,0,0,1)"); mask.addColorStop(0.7, "rgba(0,0,0,1)"); mask.addColorStop(1, "rgba(0,0,0,0)");
  gg.globalCompositeOperation = "destination-in";
  gg.fillStyle = mask; gg.fillRect(0, 0, S, S);
  g.drawImage(ringGlowCanvas, 0, 0);
  g.globalCompositeOperation = "source-over";
  return lit;
}
