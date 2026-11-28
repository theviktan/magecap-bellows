// Tests the update server Worker with a stand-in for GitHub and Cloudflare's cache:
//   node Tools/UpdateServer/worker.test.mjs
// Prints WORKER TESTS PASSED, or the first failure and exits with 1.

import assert from "node:assert/strict";
import worker from "./worker.js";

const manifest = JSON.stringify({ version: "0.8.0", installer: { url: "download/v0.8.0/MageCap-Bellows-0.8.0-Windows-Setup.exe" } });
const releases = {
	"v0.8.0": {
		tag_name: "v0.8.0", draft: false,
		assets: [
			{ id: 11, name: "MageCap-Bellows-0.8.0-Windows-Setup.exe" },
			{ id: 12, name: "MageCap-Bellows-0.8.0-Windows.zip" },
			{ id: 13, name: "update.json" },
			{ id: 14, name: "notes.txt" },
		],
	},
	"v0.9.0": { tag_name: "v0.9.0", draft: true, assets: [] },
};
const requests = [];

globalThis.caches = {
	default: {
		entries: new Map(),
		async match(request) { const hit = this.entries.get(request.url); return hit ? hit.clone() : undefined; },
		async put(request, response) { this.entries.set(request.url, response); },
	},
};

globalThis.fetch = async (url, options = {}) => {
	url = String(url);
	requests.push({ url, headers: options.headers || {} });
	if (url.startsWith("https://signed.example/"))
		return new Response(url.endsWith("/13") ? manifest : "file", { status: 200 });
	assert.ok(url.startsWith("https://api.github.com/repos/theviktan/magecap-bellows/"), url);
	assert.equal(options.headers.Authorization, "Bearer secret-token");
	const path = url.slice("https://api.github.com/repos/theviktan/magecap-bellows".length);
	if (path === "/releases/latest")
		return Response.json(releases["v0.8.0"]);
	const tag = path.match(/^\/releases\/tags\/(.+)$/);
	if (tag)
		return releases[decodeURIComponent(tag[1])] ? Response.json(releases[decodeURIComponent(tag[1])]) : new Response("", { status: 404 });
	const asset = path.match(/^\/releases\/assets\/(\d+)$/);
	if (asset)
		return new Response(null, { status: 302, headers: { Location: `https://signed.example/${asset[1]}` } });
	return new Response("", { status: 404 });
};

const env = { GITHUB_TOKEN: "secret-token", GITHUB_REPO: "theviktan/magecap-bellows" };
const waits = [];
const context = { waitUntil: (promise) => waits.push(promise) };
const get = (path, method = "GET") => worker.fetch(new Request(`https://updates.magecapgames.com${path}`, { method }), env, context);

// The manifest comes from the latest release, then from the cache.
let response = await get("/bellows/update.json");
assert.equal(response.status, 200);
assert.equal(await response.text(), manifest);
assert.match(response.headers.get("Content-Type"), /application\/json/);
await Promise.all(waits);
const before = requests.length;
response = await get("/bellows/update.json");
assert.equal(await response.text(), manifest);
assert.equal(requests.length, before, "the second request is served from the cache");
assert.equal((await get("/bellows/update.json", "HEAD")).status, 200);

// Release files redirect to GitHub's short-lived address; nothing else is given out.
response = await get("/bellows/download/v0.8.0/MageCap-Bellows-0.8.0-Windows-Setup.exe");
assert.equal(response.status, 302);
assert.equal(response.headers.get("Location"), "https://signed.example/11");
assert.equal((await get("/bellows/latest/setup")).headers.get("Location"), "https://signed.example/11");
assert.equal((await get("/bellows/latest/zip")).headers.get("Location"), "https://signed.example/12");
for (const path of [
	"/bellows/download/v0.8.0/notes.txt",						// Not an allowed kind of file
	"/bellows/download/v0.8.0/missing.exe",
	"/bellows/download/v0.9.0/anything.exe",					// A draft
	"/bellows/download/main/MageCap-Bellows-0.8.0-Windows-Setup.exe",	// Not a version tag
	"/bellows/download/v0.8.0/..%2Fsecret.exe",
	"/bellows/other",
	"/",
])
	assert.equal((await get(path)).status, 404, path);
assert.equal((await get("/bellows/update.json", "POST")).status, 405);

// The token never reaches anywhere but GitHub's API.
for (const request of requests)
	if (!request.url.startsWith("https://api.github.com/"))
		assert.equal(request.headers.Authorization, undefined, request.url);

// Unset, it says so instead of failing strangely.
assert.equal((await worker.fetch(new Request("https://updates.magecapgames.com/bellows/update.json"), {}, context)).status, 503);

console.log("WORKER TESTS PASSED");
