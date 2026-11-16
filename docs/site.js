// Hodeia website: light/dark toggle and direct download links.
(function () {
  "use strict";

  // ── Theme toggle ──────────────────────────────────────────────
  // The initial theme is applied by an inline script in <head> (no flash);
  // this only wires the button.
  var root = document.documentElement;
  var toggle = document.getElementById("theme-toggle");

  function currentTheme() {
    var t = root.getAttribute("data-theme");
    if (t) return t;
    return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  }

  if (toggle) {
    toggle.addEventListener("click", function () {
      var next = currentTheme() === "dark" ? "light" : "dark";
      root.setAttribute("data-theme", next);
      try { localStorage.setItem("hodeia-theme", next); } catch (e) { /* private mode */ }
    });
  }

  // ── Latest release ────────────────────────────────────────────
  // Point the download buttons at the latest release's installer. Any
  // failure (rate limit, offline) leaves the default links to the releases
  // page untouched.
  var REPO = "hancito/Hodeia";
  var t = window.HODEIA_I18N || {};
  var isWindows = /Windows/i.test(navigator.userAgent);
  var isLinux = /Linux/i.test(navigator.userAgent) && !/Android/i.test(navigator.userAgent);

  var hero = document.getElementById("hero-download");
  if (hero && isLinux) {
    hero.href = "#download";
    hero.textContent = t.downloadLinux || hero.textContent;
  }

  fetch("https://api.github.com/repos/" + REPO + "/releases?per_page=10", {
    headers: { Accept: "application/vnd.github+json" }
  })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (releases) {
      var rel = releases.find(function (r) { return !r.draft; });
      if (!rel) return;
      var exe = (rel.assets || []).find(function (a) { return /x64\.exe$/i.test(a.name); });
      var version = rel.tag_name + (rel.prerelease ? " (" + (t.preview || "preview") + ")" : "");

      document.querySelectorAll("[data-release-version]").forEach(function (el) {
        el.textContent = version;
        el.hidden = false;
      });

      if (exe) {
        var win = document.getElementById("windows-download");
        if (win) {
          win.href = exe.browser_download_url;
          win.textContent = (t.downloadWindows || "Download for Windows") + " — " + rel.tag_name;
        }
        if (hero && isWindows) {
          hero.href = exe.browser_download_url;
          hero.textContent = (t.downloadWindows || "Download for Windows") + " — " + rel.tag_name;
        }
      }
    })
    .catch(function () { /* keep default links */ });
})();
