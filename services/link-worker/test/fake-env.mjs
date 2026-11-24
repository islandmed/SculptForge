// A stand-in for the Workers runtime pieces the relay uses: a Durable Object namespace whose objects keep their
// storage in memory, with alarms run by hand (fireAlarms). Enough to test src/index.js under plain Node.
import { Topic } from "../src/index.js";

class MemoryStorage {
    constructor() {
        this.map = new Map();
        this.alarmAt = null;
    }
    async get(key) { return structuredClone(this.map.get(key)); }
    async put(key, value) { this.map.set(key, structuredClone(value)); }
    async deleteAll() { this.map.clear(); this.alarmAt = null; }
    async getAlarm() { return this.alarmAt; }
    async setAlarm(at) { this.alarmAt = at; }
}

export function fakeEnv(allowedOrigins = "https://sculptforge.app") {
    const objects = new Map();
    const env = { ALLOWED_ORIGINS: allowedOrigins };
    env.UPDATE_CHECKS = { points: [], writeDataPoint(point) { this.points.push(structuredClone(point)); } }; // Analytics Engine
    env.TOPICS = {
        idFromName: (name) => name,
        get(id) {
            if (!objects.has(id)) objects.set(id, new Topic({ storage: new MemoryStorage() }, env));
            return objects.get(id);
        },
    };
    env.fireAlarms = async (now = Date.now()) => {
        for (const object of objects.values()) {
            const at = object.ctx.storage.alarmAt;
            if (at !== null && at <= now) await object.alarm();
        }
    };
    return env;
}
