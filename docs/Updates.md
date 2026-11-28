# Installing and updating MageCap Bellows

Each release ships two ways on Windows:

- **`MageCap-Bellows-<version>-Windows-Setup.exe`**, the setup program. It installs Bellows for the
  current user, with no administrator rights, in `%LOCALAPPDATA%\Programs\MageCap Bellows`, with a
  Start menu shortcut, a desktop shortcut (optional), an uninstaller in **Settings → Apps** and,
  optionally, `.bproject` files opening in Bellows. An installed copy updates itself.
- **`MageCap-Bellows-<version>-Windows.zip`**, to unzip anywhere (a portable copy). It looks for
  updates too, but you unzip each new version yourself.

## How updating works

- When Bellows starts it looks for a new version, at most once a day (turn that off in the
  **Updates** dialog: **Look for updates when Bellows starts**). **Help → Check for Updates...**, the
  Project Manager's **Check for Updates** button and the title bar's search (Ctrl+K, "Check for
  Updates") look right away.
- When there's a new version, the **Updates** dialog shows what's new, and an **Update** button stays
  in the title bar (and the Project Manager) until it's installed or skipped.
- **Download and install** (installed copies): downloads the setup program, checks its size and
  SHA-256 against the update information, saves the open scene and scripts, starts the setup
  program and closes Bellows. Setup waits for every open editor to close, installs over the old
  version (your projects, settings and the sample project's changes are kept) and opens Bellows
  again. If the scene was never saved, Bellows asks you to save it first.
- **Download the zip** (portable copies): downloads and checks the zip, then opens its folder.
- **Skip this version** stops the startup check offering that version; a newer one is offered
  again, and **Check for Updates** still finds it. **Later** closes the dialog.
- Builds between releases (`0.7.0-dev`) count as older than the release they lead to, so the
  release is offered to them.
- Headless and scripted runs (`--headless`, `--run-script`) never look for updates on their own.
- Until a release with update information is published (0.7.0 is the first), the update server
  answers 404 and **Check for Updates** says Bellows is up to date.

The update information comes from `https://updates.magecapgames.com/bellows/update.json`.
`UpdateUrl` in `EditorSettings.json` (in `%APPDATA%\MageCapBellows`) replaces that address, for
example with a `file:///C:/Updates/update.json` for testing; files it names must then be in the same
folder. Only `https://` and `file://` addresses are used.

## What CI makes for a release

The Windows job packages the editor, zips it, builds the setup program from
`Tools/Installer/MageCapBellows.iss` with Inno Setup 6, installs and uninstalls it silently as a
test, and writes `update.json` with `Tools/WriteUpdateManifest.py` (the version, the release notes,
and each file's address, size and SHA-256). For a version tag, the release job attaches the zip, the
setup program and `update.json` to the GitHub Release. Every build of `main` also uploads the setup
program and its `update.json` as an artifact.

## The update server

The releases repository is private, so the editor can't download from GitHub directly. A
Cloudflare Worker (`Tools/UpdateServer/worker.js`) at `updates.magecapgames.com` reads the releases
with a read-only token and serves:

| Address | What |
|---------|------|
| `/bellows/update.json` | The latest release's `update.json` (cached for 5 minutes) |
| `/bellows/download/<tag>/<file>` | A release's setup program, zip or `update.json` |
| `/bellows/latest/setup`, `/bellows/latest/zip` | The latest setup program or zip, for a Download link on a website |

Drafts and pre-releases are never served, not even by their tag. The Worker gives every published
release to anyone who asks, with no key: that matches the repository, which is public (Bellows is
free and open source), so it shows nothing GitHub doesn't. `node Tools/UpdateServer/worker.test.mjs` tests it
against a stand-in for GitHub (ctest runs it as `UpdateServerTests` when Node.js is installed).

### Setting it up (once)

**1. Make a read-only GitHub token** (on github.com):

1. Sign in to github.com. Click your profile picture (top right) → **Settings**.
2. In the left column, at the bottom, click **Developer settings**.
3. Click **Personal access tokens** → **Fine-grained tokens** → **Generate new token**.
4. **Token name**: `Bellows update server`. **Expiration**: choose **Custom** and pick a date a year
   away (put a reminder in your calendar to renew it).
5. **Repository access**: choose **Only select repositories**, then pick
   **theviktan/magecap-bellows**.
6. Under **Permissions** → **Repository permissions**, find **Contents** and set it to
   **Read-only** (Metadata turns Read-only by itself). Leave everything else at No access.
7. Click **Generate token**, then copy the token (it starts with `github_pat_`). It's shown only
   once; keep the page open until step 2.9.

**2. Create the Worker** (on dash.cloudflare.com):

1. Sign in to dash.cloudflare.com. In the left column click **Compute (Workers)** → **Workers &
   Pages** (older dashboards: **Workers & Pages**).
2. Click **Create** → on the **Workers** tab, **Start with Hello World!** (or **Create Worker**).
3. **Worker name**: `bellows-updates`. Click **Deploy**.
4. Click **Edit code**. In the editor, select everything in `worker.js` (Ctrl+A) and delete it.
5. Open `Tools/UpdateServer/worker.js` from the repository in a text editor (or on github.com: the
   file, then **Raw**), copy all of it, and paste it into the Cloudflare editor.
6. Click **Deploy** (top right), then the back arrow to return to the Worker.
7. Click the **Settings** tab → **Variables and Secrets** → **Add**.
8. **Type**: **Text**. **Variable name**: `GITHUB_REPO`. **Value**: `theviktan/magecap-bellows`.
   Click **Deploy** (or **Save**).
9. Click **Add** again. **Type**: **Secret**. **Variable name**: `GITHUB_TOKEN`. **Value**: paste
   the token from step 1.7. Click **Deploy**.

**3. Give it its address** (still on the Worker's **Settings** tab):

1. Under **Domains & Routes**, click **Add** → **Custom domain**.
2. Type `updates.magecapgames.com` and click **Add domain**. Cloudflare makes the DNS record and
   certificate itself (it can take a few minutes).

**4. Check it**: once a release with `update.json` exists, open
https://updates.magecapgames.com/bellows/update.json in a browser. It shows the release's
version and notes. Before the first such release it says "No release has update information yet."

To change the Worker later, repeat steps 2.4 to 2.6 with the new `worker.js`. When the token
expires, make a new one (step 1) and replace `GITHUB_TOKEN` (step 2.9, **Edit** on the existing
secret).
