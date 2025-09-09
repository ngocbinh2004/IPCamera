(function () {
  function initSidebar() {
    const sidebarBtns = document.querySelectorAll(".sidebar-btn");
    const sections = document.querySelectorAll(".settings-section");
    if (!sidebarBtns.length || !sections.length) return;

    if (window.__SIDEBAR_BOUND__) return;
    window.__SIDEBAR_BOUND__ = true;

    sidebarBtns.forEach((btn) => {
      btn.addEventListener("click", function () {
        sidebarBtns.forEach((btn) => btn.classList.remove("active"));
        this.classList.add("active");
        sections.forEach((section) => (section.style.display = "none"));
        const sectionId = this.dataset.section;
        const target = document.getElementById(sectionId);
        if (target) target.style.display = "block";
      });
    });

    const firstSection = sections[0];
    if (firstSection && getComputedStyle(firstSection).display === "none") {
      firstSection.style.display = "block";
    }

    const logoutBtn = document.getElementById("logoutBtn");
    if (logoutBtn) {
      logoutBtn.addEventListener("click", function () {
        window.location.href = "index.html";
      });
    }
  }

  function initLogin() {
    const form = document.getElementById("loginForm");
    if (!form) return;
    const message = document.getElementById("message");

    form.addEventListener("submit", function (e) {
      e.preventDefault();
      const username = document.getElementById("username")?.value ?? "";
      const password = document.getElementById("password")?.value ?? "";
      if (username === "admin" && password === "123456") {
        if (message) {
          message.style.color = "green";
          message.textContent = "Đăng nhập thành công!";
        }
        setTimeout(() => (window.location.href = "home.html"), 1000);
      } else {
        if (message) {
          message.style.color = "red";
          message.textContent = "Sai tên đăng nhập hoặc mật khẩu!";
        }
      }
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", function () {
      initLogin();
      initSidebar();
    });
  } else {
    initLogin();
    initSidebar();
  }
})();
