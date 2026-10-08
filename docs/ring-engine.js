// The ring animation engine, for the browser. A line-for-line copy of firmware/pebble-pager/ring.h: keep the two
// in step, and check them against each other after any change (see AGENTS.md). Used by ring-preview.html and
// virtual-pager.html.
const RING_PIXELS = 12;
const RING_STEP_MS = 5;
const RING_STEP_S = RING_STEP_MS / 1000;

const ringTune = {
  speed: 0.9, accel: 0.2, maxSpeed: 1.9, tail: 5.5, tailPower: 1.0, attackHz: 2.6, releaseHz: 1.5,
  leap: 1.2, recoil: 1.0, bounceHz: 3.0, bounceDamp: 0.35, stretch: 1.0, pastel: 0.2, whiteGain: 1.0, peak: 1.4, budgetMa: 180.0
};

const ring = {
  down: false, level: 0, vel: 0, hop: 0, hopVel: 0, angle: 0, held: 0, speed: 0, color: [0, 0, 0, 0],
  failT: -1, blipT: -1, blipColor: [0, 0, 0, 0], play: null, playCount: 0, playIndex: 0, playLeftMs: 0, clock: 0
};

function ringSplit(rgb, out) {
  const r = ((rgb >> 16) & 255) / 255, g = ((rgb >> 8) & 255) / 255, b = (rgb & 255) / 255;
  const w = Math.min(r, Math.min(g, b)), top = Math.max(r, Math.max(g, b)) - w;
  if (top < 0.001) { out[0] = out[1] = out[2] = 0; out[3] = 1; return; }
  out[0] = (r - w) / top; out[1] = (g - w) / top; out[2] = (b - w) / top;
  out[3] = w * ringTune.pastel;
}

function ringSetColor(rgb) { ringSplit(rgb, ring.color); }

function ringPress(down) {
  if (down === ring.down) return;
  ring.down = down;
  const kick = 6.2831853 * ringTune.bounceHz * 1.4;
  if (down) { ring.held = 0; ring.speed = ringTune.speed; ring.hopVel += ringTune.leap * kick; }
  else ring.hopVel -= ringTune.recoil * kick;
}

function ringPlay(ms, count) {
  ring.play = ms; ring.playCount = count; ring.playIndex = 0;
  ring.playLeftMs = count > 0 ? ms[0] : 0;
  if (count > 0) ringPress(true);
}

function ringFail() { ring.failT = 0; }
function ringBlip(rgb) { ringSplit(rgb, ring.blipColor); ring.blipT = 0; }

function ringActive() {
  return ring.down || ring.level > 0 || ring.failT >= 0 || ring.blipT >= 0 || ring.play !== null;
}

function ringStep() {
  const dt = RING_STEP_S, twoPi = 6.2831853;
  ring.clock += dt;

  if (ring.play) {
    ring.playLeftMs -= RING_STEP_MS;
    while (ring.play && ring.playLeftMs <= 0) {
      ring.playIndex++;
      if (ring.playIndex >= ring.playCount) { ring.play = null; ringPress(false); break; }
      ring.playLeftMs += ring.play[ring.playIndex];
      ringPress(ring.playIndex % 2 === 0);
    }
  }

  const w = twoPi * (ring.down ? ringTune.attackHz : ringTune.releaseHz);
  const target = ring.down ? 1.0 : 0.0;
  ring.vel += (w * w * (target - ring.level) - 2 * w * ring.vel) * dt;
  ring.level += ring.vel * dt;
  if (ring.level > 1) ring.level = 1;
  if (ring.level < 0 || (!ring.down && ring.level < 0.003 && ring.vel <= 0)) { ring.level = 0; ring.vel = 0; ring.hop = 0; ring.hopVel = 0; }

  const wb = twoPi * ringTune.bounceHz;
  ring.hopVel += (-wb * wb * ring.hop - 2 * ringTune.bounceDamp * wb * ring.hopVel) * dt;
  ring.hop += ring.hopVel * dt;

  if (ring.down) {
    ring.held += dt;
    ring.speed = Math.min(ringTune.maxSpeed, ringTune.speed + ringTune.accel * ring.held);
    ring.angle += ring.speed * dt;
  } else {
    ring.angle += ring.speed * Math.min(ring.level, 1.0) * dt;
  }
  if (ring.angle >= 1024) ring.angle -= 1024;

  if (ring.failT >= 0) { ring.failT += dt; if (ring.failT >= 1.35) ring.failT = -1; }
  if (ring.blipT >= 0) { ring.blipT += dt; if (ring.blipT >= 0.9) ring.blipT = -1; }
}

function ringAdvance(nowS) {
  if (nowS - ring.clock > 1.0) ring.clock = nowS - 1.0;
  while (ring.clock + RING_STEP_S <= nowS) ringStep();
}

function ringSmooth(a, b, x) {
  let t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

function ringRender(out) {
  const px = [];
  const level = ring.level;
  let head = (ring.angle - Math.floor(ring.angle)) * RING_PIXELS + ring.hop;
  while (head < 0) head += RING_PIXELS;
  while (head >= RING_PIXELS) head -= RING_PIXELS;
  let tail = ringTune.tail * (0.35 + 0.65 * level) + ringTune.stretch * ring.hop;
  if (tail < 1) tail = 1;
  const fading = ring.down ? 0 : 1 - Math.min(level, 1.0);
  for (let i = 0; i < RING_PIXELS; i++) {
    let behind = head - i;
    if (behind < 0) behind += RING_PIXELS;
    const ahead = RING_PIXELS - behind;
    let bright = 0, white = 0;
    if (ahead < 1) bright = 1 - ahead;
    else if (behind <= tail) {
      const along = behind / tail;
      bright = Math.pow(1 - along, ringTune.tailPower);
      white = ringSmooth(0.12, 0.7, along);
    }
    white += (1 - white) * fading * 0.85;
    bright *= level * ringTune.peak;
    px.push([ring.color[0] * (1 - white) * bright, ring.color[1] * (1 - white) * bright, ring.color[2] * (1 - white) * bright,
             (ring.color[3] * (1 - white) + ringTune.whiteGain * white) * bright]);
  }

  if (ring.failT >= 0) {
    const t = ring.failT;
    let pulse = 0;
    if (t < 0.6) pulse = Math.sin(3.1415927 * t / 0.6);
    else if (t >= 0.75) pulse = Math.sin(3.1415927 * (t - 0.75) / 0.6);
    pulse = pulse * pulse * ringTune.peak;
    for (let i = 0; i < RING_PIXELS; i++) px[i][0] += 0.9 * pulse;
  }
  if (ring.blipT >= 0) {
    let breath = Math.sin(3.1415927 * ring.blipT / 0.9);
    breath = breath * breath * ringTune.peak * 0.8;
    const at = [RING_PIXELS - 1, 0, 1], share = [0.25, 1.0, 0.25];
    for (let k = 0; k < 3; k++)
      for (let c = 0; c < 4; c++) px[at[k]][c] += ring.blipColor[c] * breath * share[k];
  }

  let sum = 0;
  for (let i = 0; i < RING_PIXELS; i++)
    for (let c = 0; c < 4; c++) {
      const v = px[i][c] < 0 ? 0 : px[i][c] > 1 ? 1 : px[i][c];
      px[i][c] = Math.pow(v, 2.2);
      sum += px[i][c];
    }
  const idle = RING_PIXELS * 1.0, draw = sum * 18.0;
  const scale = draw > ringTune.budgetMa - idle ? (ringTune.budgetMa - idle) / draw : 1.0;
  for (let i = 0; i < RING_PIXELS; i++)
    for (let c = 0; c < 4; c++) out[i][c] = Math.floor(px[i][c] * scale * 255.0 + 0.5);
  return idle + draw * scale;
}
