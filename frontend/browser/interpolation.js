const lerp = (from, to, fraction) => from + (to - from) * fraction;

function interpolateVector(from, to, fraction) {
  return { x: lerp(from.x, to.x, fraction), y: lerp(from.y, to.y, fraction) };
}

export class ShipInterpolation {
  constructor() {
    this.states = new Map();
    this.lastSnapshotTime = null;
    this.animationEnd = 0;
  }

  update(entities, now) {
    const duration = this.lastSnapshotTime === null ? 0 : Math.max(1, now - this.lastSnapshotTime);
    const next = new Map();
    this.animationEnd = 0;
    for (const entity of entities) {
      if (entity.type !== 'craft' || !Number.isSafeInteger(entity.id)) continue;
      const previous = this.states.get(entity.id);
      // Start at the visible pose so an early or late packet never causes a jump.
      const from = previous && duration > 0 ? this.sample(previous.entity, now) : entity;
      next.set(entity.id, { entity, from, to: entity, startedAt: now, duration });
      if (previous && duration > 0) this.animationEnd = now + duration;
    }
    this.states = next;
    this.lastSnapshotTime = now;
  }

  sample(entity, now) {
    const state = this.states.get(entity.id);
    if (entity.type !== 'craft' || !state || state.entity !== entity) return entity;
    const fraction = state.duration > 0
      ? Math.max(0, Math.min(1, (now - state.startedAt) / state.duration)) : 1;
    if (fraction === 1) return state.to;
    const velocity = state.from.velocity && state.to.velocity
      ? interpolateVector(state.from.velocity, state.to.velocity, fraction) : state.to.velocity;
    return {
      position: interpolateVector(state.from.position, state.to.position, fraction),
      velocity
    };
  }

  isAnimating(now) {
    return now < this.animationEnd;
  }

  freeze(now) {
    for (const state of this.states.values()) {
      const pose = this.sample(state.entity, now);
      state.from = state.to = pose;
      state.duration = 0;
    }
    this.lastSnapshotTime = null;
    this.animationEnd = 0;
  }
}
