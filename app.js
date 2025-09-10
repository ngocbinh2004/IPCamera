(function () {
  const API = {
    setLevel: "/api/logging/level",
    start: "/api/logging/start",
    stop: "/api/logging/stop",
    download: "/api/logging/download",
  };

  const toast = (msg, kind = "info") => {
    const el = document.getElementById("toast");
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
      return {
        ok: true,
        json: async () => ({ ok: true }),
        blob: async () => new Blob(),
      };
    }
  };

  function initSidebar() {
    const sidebarBtns = document.querySelectorAll(".sidebar-btn");
    const sections = document.querySelectorAll(".settings-section");
    if (!sidebarBtns.length || !sections.length) return;
    sidebarBtns.forEach((btn) => {
      btn.addEventListener("click", function () {
        sidebarBtns.forEach((b) => b.classList.remove("active"));
        this.classList.add("active");
        sections.forEach((s) => (s.style.display = "none"));
        const id = this.dataset.section;
        const target = document.getElementById(id);
        if (target) target.style.display = "";
      });
    });
  }

  function initLogging() {
    const levelSelect = document.getElementById("levelSelect");
    const levelDisplay = document.getElementById("levelDisplay");
    const btnStart = document.getElementById("btnStart");
    const btnStop = document.getElementById("btnStop");
    const btnSave = document.getElementById("btnSave");
    const preview = document.getElementById("logPreview");

    if (!levelSelect || !btnStart || !btnStop || !btnSave) return;

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

    levelSelect.addEventListener("change", async () => {
      const value = levelSelect.value;
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

    btnStart.addEventListener("click", async () => {
      if (isWriting) return;
      btnStart.disabled = true;
      btnStart.classList.add("loading");
      const res = await safeFetch(API.start, { method: "POST" });
      btnStart.classList.remove("loading");
      if (!res.ok) {
        btnStart.disabled = false;
        toast("Start failed", "error");
        return;
      }
      isWriting = true;
      btnStop.disabled = false;
      btnStop.classList.remove("btn-muted");
      writeLine("Start logging to debug.log");
      logTimer = setInterval(() => writeLine("IP camera heartbeat"), 1500);
      toast("Logging started", "success");
    });

    btnStop.addEventListener("click", async () => {
      if (!isWriting) return;
      btnStop.classList.add("loading");
      const res = await safeFetch(API.stop, { method: "POST" });
      btnStop.classList.remove("loading");
      if (!res.ok) {
        toast("Stop failed", "error");
        return;
      }
      isWriting = false;
      btnStart.disabled = false;
      btnStop.disabled = true;
      btnStop.classList.add("btn-muted");
      if (logTimer) clearInterval(logTimer);
      writeLine("Stop logging to debug.log");
      toast("Logging stopped", "success");
    });

    btnSave.addEventListener("click", async () => {
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

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", () => {
      initSidebar();
      initLogging();
    });
  } else {
    initSidebar();
    initLogging();
  }
})();
