// The MageCap Bellows update server: a Cloudflare Worker at updates.magecapgames.com.
//
// Releases live on the private GitHub repository; this Worker reads them with a read-only token and
// hands the editor what it needs, so nobody needs access to the repository:
//
//   GET /bellows/update.json                  the latest release's update.json (what the editor checks)
//   GET /bellows/download/<tag>/<file>        a release file: the setup program, the zip or update.json
//   GET /bellows/latest/setup                 the latest setup program (for a "Download" link on a site)
//   GET /bellows/latest/zip                   the latest zip
//
// Settings (Worker > Settings > Variables and Secrets):
//   GITHUB_TOKEN   secret: a fine-grained token with Contents: Read-only on the repository
//   GITHUB_REPO    text: owner/name, for example theviktan/magecap-bellows
//
// Files are served by sending the client on to GitHub's short-lived download address, so large
// files never pass through the Worker. Only release files with these endings are given out.

const ALLOWED_ENDINGS = [".exe", ".zip", ".json"];
const TAG_PATTERN = /^v\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;
const FILE_PATTERN = /^[0-9A-Za-z._-]+$/;
const MANIFEST_CACHE_SECONDS = 300;

export default {
	async fetch(request, env, context) {
		if (request.method !== "GET" && request.method !== "HEAD")
			return text("Only GET and HEAD are served.", 405, { Allow: "GET, HEAD" });
		if (!env.GITHUB_TOKEN || !env.GITHUB_REPO)
			return text("The update server isn't set up yet (GITHUB_TOKEN and GITHUB_REPO).", 503);

		const path = new URL(request.url).pathname.replace(/\/+$/, "");
		try {
			if (path === "/bellows/update.json")
				return await latestManifest(request, env, context);
			if (path === "/bellows/latest/setup" || path === "/bellows/latest/zip")
				return await latestFile(env, path.endsWith("setup") ? "-Setup.exe" : ".zip");
			const download = path.match(/^\/bellows\/download\/([^/]+)\/([^/]+)$/);
			if (download)
				return await releaseFile(env, decodeURIComponent(download[1]), decodeURIComponent(download[2]));
			return text("Not found.", 404);
		} catch (error) {
			return text("The update server couldn't reach the releases. Try again later.", 502);
		}
	},
};

function text(body, status, headers = {}) {
	return new Response(body + "\n", { status, headers: { "Content-Type": "text/plain; charset=utf-8", ...headers } });
}

async function github(env, path, accept = "application/vnd.github+json") {
	return fetch(`https://api.github.com/repos/${env.GITHUB_REPO}${path}`, {
		headers: {
			Accept: accept,
			Authorization: `Bearer ${env.GITHUB_TOKEN}`,
			"User-Agent": "MageCap-Bellows-Update-Server",
			"X-GitHub-Api-Version": "2022-11-28",
		},
		redirect: "manual",
	});
}

// The newest published release (drafts and pre-releases are never offered), or one by its tag.
async function getRelease(env, tag) {
	const response = await github(env, tag ? `/releases/tags/${encodeURIComponent(tag)}` : "/releases/latest");
	if (response.status === 404)
		return null;
	if (!response.ok)
		throw new Error(`GitHub answered ${response.status}`);
	const release = await response.json();
	return release.draft ? null : release;
}

// GitHub's short-lived address for a private release file: the asset API answers with a redirect.
async function assetLocation(env, asset) {
	const response = await github(env, `/releases/assets/${asset.id}`, "application/octet-stream");
	const location = response.headers.get("Location");
	if ((response.status === 302 || response.status === 301 || response.status === 307) && location)
		return location;
	throw new Error(`GitHub answered ${response.status} for ${asset.name}`);
}

async function latestManifest(request, env, context) {
	const cache = caches.default;
	const key = new Request(new URL("/bellows/update.json", request.url).toString(), { method: "GET" });
	const cached = await cache.match(key);
	if (cached)
		return request.method === "HEAD" ? new Response(null, cached) : cached;

	const release = await getRelease(env, null);
	const asset = release && release.assets.find((item) => item.name === "update.json");
	if (!asset)
		return text("No release has update information yet.", 404);
	const file = await fetch(await assetLocation(env, asset));
	if (!file.ok)
		throw new Error(`The update.json download answered ${file.status}`);
	const body = await file.text();
	JSON.parse(body);	// Never pass on something that isn't JSON
	const response = new Response(body, {
		headers: {
			"Content-Type": "application/json; charset=utf-8",
			"Cache-Control": `public, max-age=${MANIFEST_CACHE_SECONDS}`,
		},
	});
	context.waitUntil(cache.put(key, response.clone()));
	return request.method === "HEAD" ? new Response(null, response) : response;
}

async function releaseFile(env, tag, name) {
	if (!TAG_PATTERN.test(tag) || !FILE_PATTERN.test(name) || !ALLOWED_ENDINGS.some((ending) => name.endsWith(ending)))
		return text("Not found.", 404);
	const release = await getRelease(env, tag);
	const asset = release && release.assets.find((item) => item.name === name);
	if (!asset)
		return text("Not found.", 404);
	return Response.redirect(await assetLocation(env, asset), 302);
}

async function latestFile(env, ending) {
	const release = await getRelease(env, null);
	const asset = release && release.assets.find((item) => item.name.endsWith(ending) && FILE_PATTERN.test(item.name));
	if (!asset)
		return text("Not found.", 404);
	return Response.redirect(await assetLocation(env, asset), 302);
}
