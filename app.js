(function () {
  const API = {
    setLevel: "/api/logging/level",
    start: "/api/logging/start",
    stop: "/api/logging/stop",
    download: "/api/logging/download",
    logout: "/api/logout",
    login: "/api/login", // để sau gắn backend thật
  };

  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  const toast = (msg, kind = "info") => {
    const el = $("#toast");
    if (!el) return;
    el.textContent = msg;
    el.className = `toast show ${kind}`;
    setTimeout(() => el.classList.remove("show"), 2200);
  };

  const safeFetch = async (url, options = {}) => {
    try {
      const res = await fetch(url, options);
      if (!res.ok) throw new Error(res.status + " " + res.statusText);
      return res;
    } catch (e) {
      console.warn("API fallback (demo):", url, e.message);
      // fallback giả lập để review UI
      return {
        ok: true,
        json: async () => ({ ok: true }),
        blob: async () => new Blob(),
      };
    }
  };

  const isAuthed = () => !!localStorage.getItem("token");

  function renderLogin() {
    const sidebar = $(".sidebar");
    const content = $(".content");
    if (sidebar) sidebar.style.display = "none";
    if (content) content.style.display = "none";

    let mount = $("#login-mount");
    if (!mount) {
      mount = document.createElement("div");
      mount.id = "login-mount";
      document.body.appendChild(mount);
    }
    mount.hidden = false;
    mount.innerHTML = `
      <div class="login-wrap">
        <div class="login-card">
          <h1>Đăng nhập</h1>
          <div class="field">
            <label>Username</label>
            <input id="login-u" type="text" placeholder="admin" />
          </div>
          <div class="field">
            <label>Password</label>
            <input id="login-p" type="password" placeholder="••••••••" />
          </div>
          <div class="actions">
            <button class="btn" id="btnLogin">Login</button>
          </div>
          <p id="login-msg" class="login-msg"></p>
        </div>
      </div>
    `;

    $("#btnLogin").addEventListener("click", async () => {
      const u = $("#login-u").value.trim();
      const p = $("#login-p").value;
      if (!u || !p) {
        $("#login-msg").textContent = "Vui lòng nhập đủ thông tin";
        return;
      }
      // Gọi API thật nếu có; hiện tại giả lập
      await safeFetch(API.login, {
        method: "POST",
        body: JSON.stringify({ u, p }),
      });
      localStorage.setItem("token", "dummy");
      location.hash = "#/home";
      mount.hidden = true;
      showApp();
    });
  }

  function showApp() {
    const sidebar = $(".sidebar");
    const content = $(".content");
    if (sidebar) sidebar.style.display = "";
    if (content) content.style.display = "";

    const mount = $("#login-mount");
    if (mount) mount.hidden = true;

    initLogout();
    initSidebar();
    initLogging();
  }

  function initLogout() {
    const btn = $("#btnLogout");
    if (!btn) return;

    btn.replaceWith(btn.cloneNode(true));
    const btn2 = $("#btnLogout");
    btn2.addEventListener("click", async () => {
      try {
        btn2.disabled = true;
        btn2.classList.add("loading");
      } catch {}
      try {
        await fetch(API.logout, { method: "POST" });
      } catch {}
      try {
        localStorage.clear();
        sessionStorage.clear();
      } catch {}
      location.hash = "#/login";
      renderLogin();
    });
  }

  function initSidebar() {
    const sidebarBtns = $$(".sidebar-btn");
    const sections = $$(".settings-section");
    if (!sidebarBtns.length || !sections.length) return;
    sidebarBtns.forEach((btn) => {
      btn.replaceWith(btn.cloneNode(true));
    });
    const freshBtns = $$(".sidebar-btn");
    freshBtns.forEach((btn) => {
      btn.addEventListener("click", function () {
        freshBtns.forEach((b) => b.classList.remove("active"));
        this.classList.add("active");
        sections.forEach((s) => (s.style.display = "none"));
        const id = this.dataset.section;
        const target = document.getElementById(id);
        if (target) target.style.display = "";
      });
    });
  }

  function initLogging() {
    const levelSelect = $("#levelSelect");
    const levelDisplay = $("#levelDisplay");
    const btnStart = $("#btnStart");
    const btnStop = $("#btnStop");
    const btnSave = $("#btnSave");
    const preview = $("#logPreview");

    if (!levelSelect || !btnStart || !btnStop || !btnSave) return;

    levelSelect.replaceWith(levelSelect.cloneNode(true));
    btnStart.replaceWith(btnStart.cloneNode(true));
    btnStop.replaceWith(btnStop.cloneNode(true));
    btnSave.replaceWith(btnSave.cloneNode(true));

    const ls = $("#levelSelect");
    const bs = $("#btnStart");
    const bo = $("#btnStop");
    const bv = $("#btnSave");

    let isWriting = false;
    let logTimer = null;
    let logLines = [];

    const writeLine = (line) => {
      const ts = new Date().toISOString();
      const lvl = levelDisplay.textContent.trim();
      const full = `[${ts}] [${lvl}] ${line}`;
      logLines.push(full);
      if (logLines.length > 500) logLines.shift();
      preview.textContent = logLines.slice(-20).join("\n");
    };

    ls.addEventListener("change", async () => {
      const value = ls.value;
      levelDisplay.textContent = value;
      writeLine(`Logging level set to ${value}`);
      const res = await safeFetch(API.setLevel, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ level: value }),
      });
      toast(
        res.ok ? "Set level OK" : "Set level failed",
        res.ok ? "success" : "error"
      );
    });

    bs.addEventListener("click", async () => {
      if (isWriting) return;
      bs.disabled = true;
      bs.classList.add("loading");
      const res = await safeFetch(API.start, { method: "POST" });
      bs.classList.remove("loading");
      if (!res.ok) {
        bs.disabled = false;
        toast("Start failed", "error");
        return;
      }
      isWriting = true;
      bo.disabled = false;
      bo.classList.remove("btn-muted");
      writeLine("Start logging to debug.log");
      logTimer = setInterval(() => writeLine("IP camera heartbeat"), 1500);
      toast("Logging started", "success");
    });

    bo.addEventListener("click", async () => {
      if (!isWriting) return;
      bo.classList.add("loading");
      const res = await safeFetch(API.stop, { method: "POST" });
      bo.classList.remove("loading");
      if (!res.ok) {
        toast("Stop failed", "error");
        return;
      }
      isWriting = false;
      bs.disabled = false;
      bo.disabled = true;
      bo.classList.add("btn-muted");
      if (logTimer) clearInterval(logTimer);
      writeLine("Stop logging to debug.log");
      toast("Logging stopped", "success");
    });

    bv.addEventListener("click", async () => {
      try {
        const res = await safeFetch(API.download);
        if (res && res.blob && !(res instanceof Response)) {
          const blob = new Blob([logLines.join("\n")], {
            type: "text/plain;charset=utf-8",
          });
          const url = URL.createObjectURL(blob);
          const a = Object.assign(document.createElement("a"), {
            href: url,
            download: "debug.log",
          });
          document.body.appendChild(a);
          a.click();
          a.remove();
          URL.revokeObjectURL(url);
        } else {
          const blob = await res.blob();
          const url = URL.createObjectURL(blob);
          const a = Object.assign(document.createElement("a"), {
            href: url,
            download: "debug.log",
          });
          document.body.appendChild(a);
          a.click();
          a.remove();
          URL.revokeObjectURL(url);
        }
        toast("Log saved", "success");
      } catch {
        toast("Save failed", "error");
      }
    });
  }

  function route() {
    if (!isAuthed() || location.hash === "#/login") {
      renderLogin();
    } else {
      showApp();
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", route);
  } else {
    route();
  }
  window.addEventListener("hashchange", route);
})();
